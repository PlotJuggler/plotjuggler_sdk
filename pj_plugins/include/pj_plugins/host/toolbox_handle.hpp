/**
 * @file toolbox_handle.hpp
 * @brief RAII wrapper around a single Toolbox plugin instance.
 *
 * Obtained from ToolboxLibrary::createHandle(). Owns the plugin context
 * and destroys it on scope exit. Move-only; not copyable.
 *
 * Typical usage:
 * @code
 *   auto handle = library.createHandle();
 *   handle.bindToolboxHost(toolbox_host);
 *   handle.bindRuntimeHost(runtime_host);
 *   handle.loadConfig(json);
 *   // user interacts with dialog
 *   auto config = handle.saveConfig();
 * @endcode
 */
#pragma once

#include <pj_base/toolbox_protocol.h>

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/**
 * RAII handle owning a Toolbox plugin instance.
 *
 * Each method delegates to the corresponding vtable function pointer.
 * The destructor calls vt_->destroy(ctx_).
 */
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
    return safeString(vt_->manifest_json);
  }

  [[nodiscard]] uint64_t capabilities() const {
    return vt_->capabilities(ctx_);
  }

  [[nodiscard]] bool bindToolboxHost(PJ_toolbox_host_t toolbox_host) {
    return vt_->bind_toolbox_host(ctx_, toolbox_host);
  }

  [[nodiscard]] bool bindRuntimeHost(PJ_toolbox_runtime_host_t runtime_host) {
    return vt_->bind_runtime_host(ctx_, runtime_host);
  }

  /// Bind the optional colormap registry service. Returns true when the plugin
  /// accepts the registry (plugins that don't use it still return true as a
  /// no-op) or when the plugin does not implement the binding at all.
  [[nodiscard]] bool bindColorMapRegistry(PJ_colormap_registry_t registry) {
    if (vt_->bind_colormap_registry == nullptr) return true;
    return vt_->bind_colormap_registry(ctx_, registry);
  }

  [[nodiscard]] std::string saveConfig() const {
    return safeString(vt_->save_config(ctx_));
  }

  [[nodiscard]] bool loadConfig(std::string_view config_json) {
    return vt_->load_config(ctx_, std::string(config_json).c_str());
  }

  [[nodiscard]] void* dialogContext() const {
    return vt_->get_dialog_context ? vt_->get_dialog_context(ctx_) : nullptr;
  }

  [[nodiscard]] std::string lastError() const {
    return safeString(vt_->get_last_error(ctx_));
  }

  /// Notify the plugin that new records have been appended to the datastore.
  /// No-op for plugins compiled against an older SDK revision whose vtable
  /// does not include the `on_data_changed` slot.
  void onDataChanged() const {
    if (vt_ == nullptr || ctx_ == nullptr) {
      return;
    }
    constexpr size_t required_size =
        offsetof(PJ_toolbox_vtable_t, on_data_changed) + sizeof(vt_->on_data_changed);
    if (vt_->struct_size < required_size || vt_->on_data_changed == nullptr) {
      return;
    }
    vt_->on_data_changed(ctx_);
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

  static std::string safeString(const char* str) {
    return str != nullptr ? std::string(str) : std::string();
  }
};

}  // namespace PJ
