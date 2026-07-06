/**
 * @file data_source_handle.hpp
 * @brief RAII wrapper around a single DataSource plugin instance (protocol v4).
 *
 * Obtained from `DataSourceLibrary::createHandle()`. Owns the plugin context
 * and destroys it on scope exit. Move-only; not copyable.
 *
 * Typical host usage:
 * @code
 *   auto handle = library.createHandle();
 *   if (auto s = handle.bind(registry.view()); !s) { ... }
 *   if (auto s = handle.loadConfig(json); !s) { ... }
 *   if (auto s = handle.start(); !s) { ... }
 *   while (handle.currentState() == PJ::DataSourceState::kRunning) {
 *     if (auto s = handle.poll(); !s) { ... }
 *   }
 *   handle.stop();
 * @endcode
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/data_source_topic_subscription.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/data_source_host_views.hpp"

namespace PJ {

/// RAII handle owning a DataSource plugin instance.
class DataSourceHandle {
 public:
  explicit DataSourceHandle(const PJ_data_source_vtable_t* vt, std::shared_ptr<void> library_owner = {})
      : vt_(vt), library_owner_(std::move(library_owner)) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_DATA_SOURCE_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~DataSourceHandle() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
  }

  DataSourceHandle(DataSourceHandle&& other) noexcept
      : vt_(other.vt_), ctx_(other.ctx_), library_owner_(std::move(other.library_owner_)) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  DataSourceHandle& operator=(DataSourceHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(library_owner_, other.library_owner_);
    }
    return *this;
  }

  DataSourceHandle(const DataSourceHandle&) = delete;
  DataSourceHandle& operator=(const DataSourceHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
  }

  // The shared library token that keeps this plugin's DSO mapped. Capture a copy
  // anywhere a callback or payload anchor PRODUCED BY the plugin may outlive this
  // handle (e.g. lazy ObjectStore payloads): the .so must not be dlclosed while
  // plugin code (a payload anchor's release fn) can still run.
  [[nodiscard]] std::shared_ptr<void> libraryOwner() const {
    return library_owner_;
  }

  [[nodiscard]] std::string manifest() const {
    return vt_->manifest_json != nullptr ? std::string(vt_->manifest_json) : std::string();
  }

  [[nodiscard]] uint64_t capabilities() const {
    return vt_->capabilities(ctx_);
  }

  /// Bind host-provided services. Acquired exactly once between create and start.
  [[nodiscard]] Status bind(PJ_service_registry_t registry) {
    PJ_error_t err{};
    if (!vt_->bind(ctx_, registry, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Serialize the plugin's config. Writes the JSON into @p out_json on
  /// success. The output reference is only touched when the returned
  /// Status is ok. Uses an out-parameter rather than `Expected<std::string>`
  /// because `PJ::Expected` defaults its error type to `std::string`, which
  /// would produce a degenerate `variant<string, string>`.
  [[nodiscard]] Status saveConfig(std::string& out_json) {
    PJ_string_view_t sv{};
    PJ_error_t err{};
    if (!vt_->save_config(ctx_, &sv, &err)) {
      return unexpected(errorToString(err));
    }
    out_json.assign(sv.data == nullptr ? "" : sv.data, sv.size);
    return okStatus();
  }

  [[nodiscard]] Status loadConfig(std::string_view config_json) {
    PJ_string_view_t sv{config_json.data(), config_json.size()};
    PJ_error_t err{};
    if (!vt_->load_config(ctx_, sv, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status start() {
    PJ_error_t err{};
    if (!vt_->start(ctx_, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  void stop() {
    vt_->stop(ctx_);
  }

  [[nodiscard]] Status pause() {
    PJ_error_t err{};
    if (!vt_->pause(ctx_, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status resume() {
    PJ_error_t err{};
    if (!vt_->resume(ctx_, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status poll() {
    PJ_error_t err{};
    if (!vt_->poll(ctx_, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] DataSourceState currentState() const {
    return static_cast<DataSourceState>(vt_->current_state(ctx_));
  }

  /// Return the typed borrowed-dialog handle. `{nullptr, nullptr}` if no dialog.
  [[nodiscard]] PJ_borrowed_dialog_t getDialog() const {
    return vt_->get_dialog != nullptr ? vt_->get_dialog(ctx_) : PJ_borrowed_dialog_t{nullptr, nullptr};
  }

  /// Query a plugin-exposed extension by reverse-DNS id. Tail-slot gated —
  /// returns nullptr if the plugin was compiled against an older v4 header that
  /// didn't have this slot, or if the plugin doesn't know the id.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_data_source_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    PJ_string_view_t sv{id.data(), id.size()};
    return vt_->get_plugin_extension(ctx_, sv);
  }

  /// The "pj.topic_subscription.v1" extension, or nullptr when the plugin
  /// doesn't offer lazy subscription (no capability, older SDK, or a
  /// malformed extension struct). Valid for the plugin-instance lifetime.
  [[nodiscard]] const PJ_topic_subscription_extension_t* topicSubscriptionExtension() const {
    const void* raw = getPluginExtension(PJ_TOPIC_SUBSCRIPTION_EXTENSION_ID);
    if (raw == nullptr) {
      return nullptr;
    }
    const auto* ext = static_cast<const PJ_topic_subscription_extension_t*>(raw);
    constexpr size_t kMinSize =
        offsetof(PJ_topic_subscription_extension_t, set_active_topics) + sizeof(ext->set_active_topics);
    if (ext->struct_size < kMinSize || ext->set_active_topics == nullptr) {
      return nullptr;
    }
    return ext;
  }

  /// Send the full desired-active topic set through the subscription
  /// extension. MUST be called on the same thread as poll(), serialized
  /// with poll()/start()/stop() — see data_source_topic_subscription.h.
  [[nodiscard]] Status setActiveTopics(Span<const std::string> topic_names) {
    const auto* ext = topicSubscriptionExtension();
    if (ext == nullptr) {
      return unexpected(std::string("plugin does not support topic subscription"));
    }
    std::vector<PJ_string_view_t> raw;
    raw.reserve(topic_names.size());
    for (const auto& name : topic_names) {
      raw.push_back(PJ_string_view_t{name.data(), name.size()});
    }
    PJ_error_t err{};
    if (!ext->set_active_topics(ctx_, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_data_source_vtable_t* vtable() const {
    return vt_;
  }

  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  const PJ_data_source_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  std::shared_ptr<void> library_owner_;
};

}  // namespace PJ
