// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/sdk/ingest_completion.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"

namespace PJ::sdk::testing {

/// Fake toolbox/write + data-source/runtime host pair for one provisional
/// dataset per fixture. Configure failure knobs/vtable truncation before use;
/// snapshot() and requestStopFromHost() are safe during ingest. Join producers
/// and StopPoller before destroying the fixture or releasing its context.
/// Reuse source::ProviderJob and testing/provider_job_probe.hpp to drive jobs;
/// this fixture supplies no job wrapper or transport simulation.
class DelegatedIngestFixture {
 public:
  struct Binding {
    std::string topic;
    std::string encoding;
    std::string type_name;
    std::vector<std::uint8_t> schema;
    std::string config;
  };
  struct Push {
    std::uint32_t binding;
    std::int64_t timestamp_ns;
    std::vector<std::uint8_t> bytes;
  };
  struct ProgressStart {
    std::string label;
    std::uint64_t total;
    bool cancellable;
  };
  struct Recording {
    std::vector<std::string> events;
    std::vector<Binding> bindings;
    std::vector<Push> pushes;
    std::vector<std::string> attached_records;
    std::vector<IngestCompletionRecord> completions;
    std::vector<ProgressStart> progress_starts;
    std::vector<std::uint64_t> progress_updates;
    std::vector<std::pair<PJ_data_source_state_t, std::string>> stop_requests;
    std::vector<std::string> notifications;
    bool dataset_survives = false;
    bool ingest_live = false;
  };

  bool refuse_create = false;
  bool refuse_binding = false;
  bool refuse_push = false;
  bool refuse_progress_start = false;
  bool block_push_until_stop = false;

  DelegatedIngestFixture() {
    write_vtable_.abi_version = PJ_PLUGIN_DATA_API_VERSION;
    write_vtable_.struct_size = sizeof(write_vtable_);
    write_vtable_.create_data_source = &createDataSource;
    runtime_vtable_.protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION;
    runtime_vtable_.struct_size = sizeof(runtime_vtable_);
    runtime_vtable_.create_parser_ingest = &createIngest;
    runtime_vtable_.release_parser_ingest = &releaseIngest;
    runtime_vtable_.discard_parser_ingest = &discardIngest;
    runtime_vtable_.notify_data_changed = &notifyDataChanged;
    runtime_vtable_.report_message = &reportToolboxMessage;
    ingest_vtable_.protocol_version = 1;
    ingest_vtable_.struct_size = sizeof(ingest_vtable_);
    ingest_vtable_.ensure_parser_binding = &ensureBinding;
    ingest_vtable_.push_message = &pushMessage;
    ingest_vtable_.progress_start = &progressStart;
    ingest_vtable_.progress_update = &progressUpdate;
    ingest_vtable_.progress_finish = &progressFinish;
    ingest_vtable_.is_stop_requested = &isStopRequested;
    ingest_vtable_.request_stop = &requestStop;
    ingest_vtable_.attach_source_record = &attachSourceRecord;
    ingest_vtable_.complete_ingest = &completeIngest;
    ingest_vtable_.report_message = &reportSourceMessage;
    ingest_vtable_.notify_state = &notifyState;
    ingest_vtable_.notify_available_topics = &notifyAvailableTopics;
  }

  [[nodiscard]] ToolboxHostView writeView() {
    return ToolboxHostView({this, &write_vtable_});
  }
  [[nodiscard]] ToolboxRuntimeHostView runtimeView() {
    return ToolboxRuntimeHostView({this, &runtime_vtable_});
  }
  /// The live context's independent Stop surface; use only inside its ingest lifetime.
  [[nodiscard]] DataSourceRuntimeHostView dataSourceView() {
    return DataSourceRuntimeHostView({this, &ingest_vtable_});
  }
  [[nodiscard]] Recording snapshot() const {
    std::lock_guard lock(record_mutex_);
    return recording_;
  }

  void emulateHostWithoutCompleteIngest() {
    ingest_vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, complete_ingest);
  }
  void emulateRuntimeWithoutDiscard() {
    runtime_vtable_.struct_size = offsetof(PJ_toolbox_runtime_host_vtable_t, discard_parser_ingest);
  }
  void emulateHostWithoutProgress() {
    ingest_vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, progress_start);
    // These original slots use pointer availability, not the tail-slot macro.
    ingest_vtable_.progress_start = nullptr;
    ingest_vtable_.progress_update = nullptr;
    ingest_vtable_.progress_finish = nullptr;
    ingest_vtable_.is_stop_requested = nullptr;
  }

  void requestStopFromHost() {
    recordEvent("host_stop");
    signalStop();
  }
  [[nodiscard]] bool waitUntilPushBlocked(std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
    std::unique_lock lock(stop_mutex_);
    return stop_wake_.wait_for(lock, timeout, [this] { return push_blocked_; });
  }

 private:
  static std::string string(PJ_string_view_t value) {
    return value.data ? std::string(value.data, static_cast<std::size_t>(value.size)) : std::string{};
  }
  static bool fail(PJ_error_t* error, std::string_view message) {
    fillError(error, 1, "delegated_ingest_fixture", message);
    return false;
  }
  void recordEvent(std::string event) {
    std::lock_guard lock(record_mutex_);
    recording_.events.push_back(std::move(event));
  }
  void signalStop() {
    {
      std::lock_guard lock(stop_mutex_);
      stop_requested_.store(true);
    }
    stop_wake_.notify_all();
  }
  static bool createDataSource(void* context, PJ_string_view_t, PJ_data_source_handle_t* out, PJ_error_t*) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("create_dataset");
    self.recording_.dataset_survives = true;
    *out = {1};
    return true;
  }
  static bool createIngest(
      void* context, std::uint32_t id, PJ_data_source_runtime_host_t* out, PJ_error_t* error) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("create_ingest");
    if (self.refuse_create || id != 1 || !self.recording_.dataset_survives) {
      return fail(error, "parser ingest unavailable");
    }
    self.recording_.ingest_live = true;
    *out = {&self, &self.ingest_vtable_};
    return true;
  }
  static bool releaseIngest(void* context, std::uint32_t, PJ_error_t*) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("release_ingest");
    self.recording_.ingest_live = false;
    return true;
  }
  static bool discardIngest(void* context, std::uint32_t, PJ_error_t*) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("discard_ingest");
    self.recording_.ingest_live = false;
    self.recording_.dataset_survives = false;
    return true;
  }
  static void notifyDataChanged(void* context) noexcept {
    static_cast<DelegatedIngestFixture*>(context)->recordEvent("notify_data_changed");
  }
  static void reportSourceMessage(void* context, PJ_data_source_message_level_t, PJ_string_view_t message) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("notification");
    self.recording_.notifications.push_back(string(message));
  }
  static void reportToolboxMessage(void* context, PJ_toolbox_message_level_t, PJ_string_view_t message) noexcept {
    reportSourceMessage(context, PJ_DATA_SOURCE_MESSAGE_INFO, message);
  }
  static void notifyState(void* context, PJ_data_source_state_t) noexcept {
    static_cast<DelegatedIngestFixture*>(context)->recordEvent("notify_state");
  }
  static bool notifyAvailableTopics(void* context, const PJ_available_topic_t*, std::uint64_t, PJ_error_t*) noexcept {
    static_cast<DelegatedIngestFixture*>(context)->recordEvent("notify_available_topics");
    return true;
  }
  static bool progressStart(
      void* context, PJ_string_view_t label, std::uint64_t total, bool cancellable, PJ_error_t* error) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("progress_start");
    self.recording_.progress_starts.push_back({string(label), total, cancellable});
    return !self.refuse_progress_start || fail(error, "progress start refused");
  }
  static bool progressUpdate(void* context, std::uint64_t current) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("progress_update");
    self.recording_.progress_updates.push_back(current);
    return !self.stop_requested_.load();
  }
  static void progressFinish(void* context) noexcept {
    static_cast<DelegatedIngestFixture*>(context)->recordEvent("progress_finish");
  }
  static bool isStopRequested(void* context) noexcept {
    return static_cast<DelegatedIngestFixture*>(context)->stop_requested_.load();
  }
  static void requestStop(void* context, PJ_data_source_state_t state, PJ_string_view_t reason) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    {
      std::lock_guard lock(self.record_mutex_);
      self.recording_.events.push_back("request_stop");
      self.recording_.stop_requests.emplace_back(state, string(reason));
    }
    self.signalStop();
  }
  static bool attachSourceRecord(void* context, PJ_string_view_t bytes, PJ_error_t*) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("attach_source_record");
    self.recording_.attached_records.push_back(string(bytes));
    return true;
  }
  static bool completeIngest(void* context, const PJ_ingest_completion_t* completion, PJ_error_t* error) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    auto copied = copyIngestCompletion(completion);
    self.recordEvent("complete_ingest");
    if (!copied) {
      return fail(error, copied.error());
    }
    std::lock_guard lock(self.record_mutex_);
    self.recording_.completions.push_back(std::move(*copied));
    return true;
  }
  static bool ensureBinding(
      void* context, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out,
      PJ_error_t* error) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    std::lock_guard lock(self.record_mutex_);
    self.recording_.events.push_back("binding");
    if (self.refuse_binding) {
      return fail(error, "binding refused");
    }
    Binding binding{
        string(request->topic_name),
        string(request->parser_encoding),
        string(request->type_name),
        {},
        string(request->parser_config_json)};
    if (request->schema.size != 0) {
      binding.schema.assign(request->schema.data, request->schema.data + request->schema.size);
    }
    self.recording_.bindings.push_back(std::move(binding));
    out->id = static_cast<std::uint32_t>(self.recording_.bindings.size());
    return true;
  }
  static bool pushMessage(
      void* context, PJ_parser_binding_handle_t handle, std::int64_t timestamp_ns, PJ_message_data_fetcher_t fetch,
      PJ_error_t* error) noexcept {
    auto& self = *static_cast<DelegatedIngestFixture*>(context);
    self.recordEvent("push_enter");
    if (self.block_push_until_stop) {
      std::unique_lock lock(self.stop_mutex_);
      self.push_blocked_ = true;
      self.stop_wake_.notify_all();
      self.stop_wake_.wait(lock, [&self] { return self.stop_requested_.load(); });
    }
    bool bound;
    {
      std::lock_guard lock(self.record_mutex_);
      bound = handle.id > 0 && handle.id <= self.recording_.bindings.size();
    }
    PJ_payload_t first{};
    PJ_payload_t second{};
    const bool fetched_first = bound && fetch.fetchMessageData && fetch.fetchMessageData(fetch.ctx, &first, error);
    const bool fetched_second = fetched_first && fetch.fetchMessageData(fetch.ctx, &second, error);
    const bool valid_data = (first.size == 0 || first.data) && (second.size == 0 || second.data);
    const bool identical =
        fetched_first && fetched_second && valid_data && first.size == second.size &&
        (first.size == 0 || std::memcmp(first.data, second.data, static_cast<std::size_t>(first.size)) == 0);
    // The anchor must keep bytes alive after the fetcher (and its captures) die.
    if (fetch.release) {
      fetch.release(fetch.ctx);
    }
    self.recordEvent("fetcher_release");
    const bool accepted = identical && !self.refuse_push && !self.stop_requested_.load();
    if (accepted) {
      Push push{handle.id, timestamp_ns, {}};
      if (first.size != 0) {
        push.bytes.assign(first.data, first.data + first.size);
      }
      std::lock_guard lock(self.record_mutex_);
      self.recording_.pushes.push_back(std::move(push));
      self.recording_.events.push_back("push");
    }
    if (first.anchor.release) {
      first.anchor.release(first.anchor.ctx);
    }
    if (second.anchor.release) {
      second.anchor.release(second.anchor.ctx);
    }
    self.recordEvent("anchors_release");
    return accepted || fail(error, "push refused, cancelled, invalid binding, or non-idempotent fetcher");
  }

  PJ_toolbox_host_vtable_t write_vtable_{};
  PJ_toolbox_runtime_host_vtable_t runtime_vtable_{};
  PJ_data_source_runtime_host_vtable_t ingest_vtable_{};
  mutable std::mutex record_mutex_;
  Recording recording_;
  std::mutex stop_mutex_;
  std::condition_variable stop_wake_;
  std::atomic<bool> stop_requested_{false};
  bool push_blocked_ = false;
};

}  // namespace PJ::sdk::testing
