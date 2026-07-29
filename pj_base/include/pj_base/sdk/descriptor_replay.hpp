/**
 * @file descriptor_replay.hpp
 * @brief C++ wrappers for the descriptor-replay v1 extension (consumer
 * side): DescriptorReplayProviderView, the RAII JoinableJob, and their
 * supporting value types. See pj_base/descriptor_replay_protocol.h for the
 * full ABI/lifetime/threading contract this header wraps.
 *
 * This is the extension-CONSUMER half only (a host reading a plugin's
 * "pj.descriptor_replay.v1" extension). The service half
 * (MaterializedSourceHostView + trait for "pj.materialized_source.v1") is a
 * separate, later addition.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/descriptor_replay_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/types.hpp"

namespace PJ {

/// Fail-closed C++ mirror of PJ_descriptor_trust_t. Unknown/future values map
/// to kRefused (see PJ_descriptor_trust_t doc-comment). Initialized directly
/// from the C enumerators so the mirror can never drift.
enum class DescriptorTrust : int32_t {
  kRefused = PJ_DESCRIPTOR_TRUST_REFUSED,
  kNeedsConfirmation = PJ_DESCRIPTOR_TRUST_NEEDS_CONFIRMATION,
  kTrusted = PJ_DESCRIPTOR_TRUST_TRUSTED,
};

/// Fail-closed C++ mirror of PJ_descriptor_replay_outcome_t. Unknown/future
/// values map to kFailed. Initialized directly from the C enumerators so the
/// mirror can never drift.
enum class ReplayOutcome : int32_t {
  kFailed = PJ_DESCRIPTOR_REPLAY_FAILED,
  kCancelled = PJ_DESCRIPTOR_REPLAY_CANCELLED,
  kSucceededUnmaterialized = PJ_DESCRIPTOR_REPLAY_SUCCEEDED_UNMATERIALIZED,
  kSucceededMaterialized = PJ_DESCRIPTOR_REPLAY_SUCCEEDED_MATERIALIZED,
};

/// queryDescriptor result, copied immediately out of the C ABI's borrowed
/// views (which are only valid until the next query_descriptor call on the
/// same plugin instance — see the protocol header's lifetime contract).
struct DescriptorQueryResult {
  DescriptorTrust trust = DescriptorTrust::kRefused;
  bool is_materialized = false;
  std::string source_identity;
  std::string local_path_utf8;
  std::string message;
  uint64_t estimated_bytes = 0;  ///< 0 = unknown
};

struct ReplayStartRequest {
  std::string descriptor_json;
  uint64_t flags = PJ_DESCRIPTOR_REPLAY_START_FLAG_NONE;
  uint64_t max_transfer_bytes = 0;  ///< 0 = no caller ceiling
};

/// RAII owner of a PJ_joinable_job_t plus the callback closures backing it.
/// Move-only.
///
/// The destructor (and any reset via move-assignment) destroys the job
/// (cancel+join) BEFORE releasing the closures, so a late callback can never
/// touch freed state. NEVER let a JoinableJob be destroyed, reset, or
/// assigned-over from inside one of its own callbacks (on_dataset/
/// on_terminal) — destroy()/join() block until the callback thread quiesces,
/// so doing so from that same thread is a self-join deadlock (or, per the
/// underlying pthread/std::thread implementation, a terminate()). Discarding
/// the Expected<JoinableJob> returned by startReplay (e.g. ignoring the
/// return value) destroys it immediately, which cancels the replay.
///
/// cancel() and join() are thread-safe with respect to the underlying JOB
/// (the provider's vtable slots are documented [thread-safe] /
/// [blocking, not-callback-thread]), but the C++ handle itself is NOT
/// synchronized: the owner must keep the JoinableJob alive for the duration
/// of any concurrent cancel()/join() call (e.g. via shared_ptr<JoinableJob>
/// when multiple threads may reach for it) and must never race a call
/// against this object's own destruction or move-assignment.
class JoinableJob {
 public:
  JoinableJob() = default;

  JoinableJob(const JoinableJob&) = delete;
  JoinableJob& operator=(const JoinableJob&) = delete;

  JoinableJob(JoinableJob&& other) noexcept : job_(other.job_), callback_ctx_(std::move(other.callback_ctx_)) {
    other.job_ = PJ_joinable_job_t{};
  }

  JoinableJob& operator=(JoinableJob&& other) noexcept {
    if (this != &other) {
      reset();
      job_ = other.job_;
      callback_ctx_ = std::move(other.callback_ctx_);
      other.job_ = PJ_joinable_job_t{};
    }
    return *this;
  }

  ~JoinableJob() {
    reset();
  }

  [[nodiscard]] bool valid() const noexcept {
    return job_.vtable != nullptr;
  }

  /// [thread-safe] Non-blocking, idempotent, best-effort cancellation. See
  /// the class doc-comment for the synchronization contract on this handle.
  void cancel() const noexcept {
    if (valid() && hasSlot(job_.vtable, offsetof(PJ_joinable_job_vtable_t, cancel), sizeof(job_.vtable->cancel)) &&
        job_.vtable->cancel != nullptr) {
      job_.vtable->cancel(job_.ctx);
    }
  }

  /// [blocking, not-callback-thread] Idempotent. Returns after on_terminal
  /// has returned.
  void join() const noexcept {
    if (valid() && hasSlot(job_.vtable, offsetof(PJ_joinable_job_vtable_t, join), sizeof(job_.vtable->join)) &&
        job_.vtable->join != nullptr) {
      job_.vtable->join(job_.ctx);
    }
  }

 private:
  friend class DescriptorReplayProviderView;

  struct CallbackContext {
    std::function<void(DatasetId)> on_dataset;
    std::function<void(ReplayOutcome, std::string)> on_terminal;
  };

  JoinableJob(PJ_joinable_job_t job, std::unique_ptr<CallbackContext> callback_ctx)
      : job_(job), callback_ctx_(std::move(callback_ctx)) {}

  /// Tail-slot capacity check mirroring PJ_HAS_TAIL_SLOT, applied to
  /// PJ_joinable_job_vtable_t's cancel/join/destroy members.
  static bool hasSlot(const PJ_joinable_job_vtable_t* vt, std::size_t offset, std::size_t size) noexcept {
    return vt->struct_size >= offset + size;
  }

  static bool hasDestroy(const PJ_joinable_job_vtable_t* vt) noexcept {
    return hasSlot(vt, offsetof(PJ_joinable_job_vtable_t, destroy), sizeof(vt->destroy)) && vt->destroy != nullptr;
  }

  /// Destroy the job (cancel+join if necessary) BEFORE releasing the
  /// closures it may still be calling into. Idempotent.
  void reset() noexcept {
    if (valid()) {
      if (hasDestroy(job_.vtable)) {
        job_.vtable->destroy(job_.ctx);
      } else {
        // A job with no usable destroy slot can't be safely stopped: its
        // callback thread may still call into callback_ctx_ at any future
        // time, and we have no way to join it. Leak the context rather than
        // risk a use-after-free — this only happens when a provider violates
        // the ABI (see DescriptorReplayProviderView::startReplay).
        (void)callback_ctx_.release();
      }
    }
    job_ = PJ_joinable_job_t{};
    callback_ctx_.reset();
  }

  PJ_joinable_job_t job_{};
  std::unique_ptr<CallbackContext> callback_ctx_;
};

/// Typed consumer of the "pj.descriptor_replay.v1" extension.
///
/// Non-owning: this view is just a (pointer, ctx) pair borrowed from the
/// plugin instance and must not outlive it. Construct from the raw pointer
/// returned by get_plugin_extension / pluginExtension plus the SAME
/// plugin-instance ctx that hook was called with. Per the protocol header,
/// the plugin instance and its DSO must stay alive until every job obtained
/// from it has been destroyed — this view holds no keep-alive of its own, so
/// hosts must independently hold their ToolboxHandle/library owner for as
/// long as any JoinableJob obtained through it is still alive.
class DescriptorReplayProviderView {
 public:
  DescriptorReplayProviderView() = default;

  DescriptorReplayProviderView(const void* extension, void* plugin_ctx)
      : ext_(static_cast<const PJ_descriptor_replay_provider_v1_t*>(extension)), plugin_ctx_(plugin_ctx) {}

  [[nodiscard]] bool valid() const noexcept {
    return ext_ != nullptr &&
           ext_->struct_size >=
               offsetof(PJ_descriptor_replay_provider_v1_t, start_replay) + sizeof(ext_->start_replay) &&
           ext_->query_descriptor != nullptr && ext_->start_replay != nullptr;
  }

  /// [main-thread, strictly bounded] See PJ_descriptor_replay_provider_v1_t::query_descriptor.
  [[nodiscard]] Expected<DescriptorQueryResult> queryDescriptor(std::string_view descriptor_json) const {
    if (!valid()) {
      return unexpected("descriptor replay provider extension is not available");
    }
    PJ_descriptor_query_result_v1_t raw{};
    raw.struct_size = sizeof(raw);
    PJ_error_t err{};
    if (!ext_->query_descriptor(plugin_ctx_, sdk::toAbiString(descriptor_json), &raw, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    DescriptorQueryResult result;
    switch (raw.trust) {
      case PJ_DESCRIPTOR_TRUST_NEEDS_CONFIRMATION:
        result.trust = DescriptorTrust::kNeedsConfirmation;
        break;
      case PJ_DESCRIPTOR_TRUST_TRUSTED:
        result.trust = DescriptorTrust::kTrusted;
        break;
      case PJ_DESCRIPTOR_TRUST_REFUSED:
      default:
        result.trust = DescriptorTrust::kRefused;
        break;
    }
    result.is_materialized = raw.is_materialized != 0;
    result.source_identity = std::string(sdk::toStringView(raw.source_identity));
    result.local_path_utf8 = std::string(sdk::toStringView(raw.local_path_utf8));
    result.message = std::string(sdk::toStringView(raw.message));
    result.estimated_bytes = raw.estimated_bytes;
    return result;
  }

  /// [main-thread] See PJ_descriptor_replay_provider_v1_t::start_replay. On
  /// success, on_dataset fires zero-or-one time and on_terminal fires
  /// exactly once, both on a job-callback thread — never before this call
  /// returns. @p on_dataset and @p on_terminal must not throw: an escaping
  /// exception is swallowed at the ABI boundary (see onTerminalThunk), and
  /// for on_terminal specifically this means the completion notification is
  /// lost — the caller will never observe that replay's outcome.
  [[nodiscard]] Expected<JoinableJob> startReplay(
      const ReplayStartRequest& request, std::function<void(DatasetId)> on_dataset,
      std::function<void(ReplayOutcome, std::string)> on_terminal) const {
    if (!valid()) {
      return unexpected("descriptor replay provider extension is not available");
    }

    PJ_descriptor_replay_start_request_v1_t raw_request{};
    raw_request.struct_size = sizeof(raw_request);
    raw_request.descriptor_json = sdk::toAbiString(request.descriptor_json);
    raw_request.flags = request.flags;
    raw_request.max_transfer_bytes = request.max_transfer_bytes;

    PJ_descriptor_replay_callbacks_v1_t raw_callbacks{};
    raw_callbacks.struct_size = sizeof(raw_callbacks);
    raw_callbacks.on_dataset = &DescriptorReplayProviderView::onDatasetThunk;
    raw_callbacks.on_terminal = &DescriptorReplayProviderView::onTerminalThunk;

    auto callback_ctx = std::make_unique<JoinableJob::CallbackContext>(
        JoinableJob::CallbackContext{std::move(on_dataset), std::move(on_terminal)});

    PJ_joinable_job_t raw_job{};
    PJ_error_t err{};
    if (!ext_->start_replay(plugin_ctx_, &raw_request, &raw_callbacks, callback_ctx.get(), &raw_job, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    // Contract check (PJ_descriptor_replay_provider_v1_t::start_replay):
    // "On true: out_job is valid". A provider that returns true but hands
    // back a job we cannot destroy would otherwise leave callback_ctx freed
    // out from under a callback thread that may still be running — a
    // use-after-free. We cannot safely reclaim callback_ctx in that case
    // (there is no way to join a job we were never validly handed), so leak
    // it deliberately rather than risk the UAF; this only happens when a
    // provider violates the ABI.
    if (raw_job.vtable == nullptr || !JoinableJob::hasDestroy(raw_job.vtable)) {
      (void)callback_ctx.release();
      return unexpected(
          "descriptor replay provider returned true from start_replay with an unusable job (violates ABI contract)");
    }
    return JoinableJob(raw_job, std::move(callback_ctx));
  }

 private:
  static ReplayOutcome mapOutcome(PJ_descriptor_replay_outcome_t outcome) noexcept {
    switch (outcome) {
      case PJ_DESCRIPTOR_REPLAY_CANCELLED:
        return ReplayOutcome::kCancelled;
      case PJ_DESCRIPTOR_REPLAY_SUCCEEDED_UNMATERIALIZED:
        return ReplayOutcome::kSucceededUnmaterialized;
      case PJ_DESCRIPTOR_REPLAY_SUCCEEDED_MATERIALIZED:
        return ReplayOutcome::kSucceededMaterialized;
      case PJ_DESCRIPTOR_REPLAY_FAILED:
      default:
        return ReplayOutcome::kFailed;
    }
  }

  static void onDatasetThunk(void* callback_ctx, PJ_data_source_handle_t dataset) noexcept {
    if (callback_ctx == nullptr) {
      return;
    }
    auto* ctx = static_cast<JoinableJob::CallbackContext*>(callback_ctx);
    try {
      if (ctx->on_dataset) {
        ctx->on_dataset(dataset.id);
      }
    } catch (...) {
      // Job-callback thread crossing the ABI boundary: never let an
      // exception from the host's closure unwind into the plugin.
    }
  }

  static void onTerminalThunk(
      void* callback_ctx, PJ_descriptor_replay_outcome_t outcome, PJ_string_view_t message) noexcept {
    if (callback_ctx == nullptr) {
      return;
    }
    auto* ctx = static_cast<JoinableJob::CallbackContext*>(callback_ctx);
    try {
      if (ctx->on_terminal) {
        ctx->on_terminal(mapOutcome(outcome), std::string(sdk::toStringView(message)));
      }
    } catch (...) {
      // Job-callback thread crossing the ABI boundary: never let an
      // exception from the host's closure unwind into the plugin. Unlike
      // on_dataset (zero-or-one, non-terminal), on_terminal is exactly-once
      // and last — if it throws, the exception is swallowed here AND the
      // completion notification is lost; the caller's on_terminal callable
      // will never observe this replay's outcome.
    }
  }

  const PJ_descriptor_replay_provider_v1_t* ext_ = nullptr;
  void* plugin_ctx_ = nullptr;
};

}  // namespace PJ
