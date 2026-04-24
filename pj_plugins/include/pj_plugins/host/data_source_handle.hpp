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
#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/data_source_host_views.hpp"

namespace PJ {

/// RAII handle owning a DataSource plugin instance.
class DataSourceHandle {
 public:
  explicit DataSourceHandle(const PJ_data_source_vtable_t* vt) : vt_(vt) {
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

  DataSourceHandle(DataSourceHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  DataSourceHandle& operator=(DataSourceHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
    }
    return *this;
  }

  DataSourceHandle(const DataSourceHandle&) = delete;
  DataSourceHandle& operator=(const DataSourceHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
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
  /// returns nullptr if the plugin was compiled against a v3.0 header that
  /// didn't have this slot, or if the plugin doesn't know the id.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_data_source_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    PJ_string_view_t sv{id.data(), id.size()};
    return vt_->get_plugin_extension(ctx_, sv);
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
};

}  // namespace PJ
