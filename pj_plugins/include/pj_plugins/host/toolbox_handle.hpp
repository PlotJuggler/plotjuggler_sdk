/**
 * @file toolbox_handle.hpp
 * @brief RAII wrapper around a single Toolbox plugin instance (protocol v4).
 */
#pragma once

#include <pj_base/toolbox_protocol.h>

#include <cassert>
#include <cstddef>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/data_source_host_views.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// RAII handle owning a Toolbox plugin instance.
class ToolboxHandle {
 public:
  explicit ToolboxHandle(const PJ_toolbox_vtable_t* vt) : vt_(vt) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~ToolboxHandle() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
  }

  ToolboxHandle(ToolboxHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  ToolboxHandle& operator=(ToolboxHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
    }
    return *this;
  }

  ToolboxHandle(const ToolboxHandle&) = delete;
  ToolboxHandle& operator=(const ToolboxHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
  }

  [[nodiscard]] std::string manifest() const {
    return vt_->manifest_json != nullptr ? std::string(vt_->manifest_json) : std::string();
  }

  [[nodiscard]] uint64_t capabilities() const {
    return vt_->capabilities(ctx_);
  }

  [[nodiscard]] Status bind(PJ_service_registry_t registry) {
    PJ_error_t err{};
    if (!vt_->bind(ctx_, registry, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

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

  [[nodiscard]] PJ_borrowed_dialog_t getDialog() const {
    return vt_->get_dialog != nullptr ? vt_->get_dialog(ctx_) : PJ_borrowed_dialog_t{nullptr, nullptr};
  }

  void onDataChanged() const {
    if (vt_ == nullptr || ctx_ == nullptr || vt_->on_data_changed == nullptr) {
      return;
    }
    vt_->on_data_changed(ctx_);
  }

  /// Query a plugin-exposed extension by reverse-DNS id. Tail-slot gated.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    PJ_string_view_t sv{id.data(), id.size()};
    return vt_->get_plugin_extension(ctx_, sv);
  }

  [[nodiscard]] const PJ_toolbox_vtable_t* vtable() const {
    return vt_;
  }

  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  const PJ_toolbox_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
};

}  // namespace PJ
