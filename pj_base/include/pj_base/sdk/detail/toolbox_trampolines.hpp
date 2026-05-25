/**
 * @file detail/toolbox_trampolines.hpp
 * @brief Out-of-line C ABI trampolines for ToolboxPluginBase (v4).
 *
 * Every trampoline is `noexcept` — the v4 vtable requires it.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace PJ {

inline void ToolboxPluginBase::trampoline_destroy(void* ctx) noexcept {
  try {
    delete static_cast<ToolboxPluginBase*>(ctx);
  } catch (...) {}
}

inline uint64_t ToolboxPluginBase::trampoline_capabilities(void* ctx) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    return self->capabilities();
  } catch (...) {
    return 0;
  }
}

inline bool ToolboxPluginBase::trampoline_bind(
    void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    auto status = self->bind(sdk::ServiceRegistry(registry));
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("bind threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in bind");
    return false;
  }
}

inline bool ToolboxPluginBase::trampoline_save_config(
    void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  if (out_json == nullptr) {
    self->storeError(out_error, 2, "plugin", "save_config called with null out_json");
    return false;
  }
  try {
    self->config_buf_ = self->saveConfig();
    out_json->data = self->config_buf_.data();
    out_json->size = self->config_buf_.size();
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("save_config threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in save_config");
    return false;
  }
}

inline bool ToolboxPluginBase::trampoline_load_config(
    void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    std::string_view sv =
        config_json.data == nullptr ? std::string_view{} : std::string_view(config_json.data, config_json.size);
    auto status = self->loadConfig(sv);
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("load_config threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in load_config");
    return false;
  }
}

inline PJ_borrowed_dialog_t ToolboxPluginBase::trampoline_get_dialog(void* ctx) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    return self->getDialog();
  } catch (...) {
    return PJ_borrowed_dialog_t{nullptr, nullptr};
  }
}

inline void ToolboxPluginBase::trampoline_on_data_changed(void* ctx) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    self->onDataChanged();
  } catch (...) {}
}

inline const void* ToolboxPluginBase::trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    std::string_view sv = id.data == nullptr ? std::string_view{} : std::string_view(id.data, id.size);
    return self->pluginExtension(sv);
  } catch (...) {
    return nullptr;
  }
}

}  // namespace PJ
