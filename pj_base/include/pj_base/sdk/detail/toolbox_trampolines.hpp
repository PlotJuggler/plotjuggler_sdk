/**
 * @file detail/toolbox_trampolines.hpp
 * @brief Out-of-line definitions for ToolboxPluginBase C ABI trampolines.
 *
 * Included automatically by toolbox_plugin_base.hpp — do not include directly.
 * Each trampoline wraps a virtual call with try-catch for full exception safety
 * across the C ABI boundary.
 */
#pragma once

namespace PJ {

inline void ToolboxPluginBase::trampoline_destroy(void* ctx) {
  try {
    delete static_cast<ToolboxPluginBase*>(ctx);
  } catch (...) {}
}

inline uint64_t ToolboxPluginBase::trampoline_capabilities(void* ctx) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    return self->capabilities();
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return 0;
  } catch (...) {
    self->last_error_ = "Unknown exception in capabilities";
    return 0;
  }
}

inline bool ToolboxPluginBase::trampoline_bind_toolbox_host(void* ctx, PJ_toolbox_host_t toolbox_host) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    auto status = self->bindToolboxHost(toolbox_host);
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in bind_toolbox_host";
    return false;
  }
}

inline bool ToolboxPluginBase::trampoline_bind_runtime_host(void* ctx, PJ_toolbox_runtime_host_t runtime_host) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    auto status = self->bindRuntimeHost(runtime_host);
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in bind_runtime_host";
    return false;
  }
}

inline bool ToolboxPluginBase::trampoline_bind_colormap_registry(void* ctx, PJ_colormap_registry_t registry) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    auto status = self->bindColorMapRegistry(registry);
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in bind_colormap_registry";
    return false;
  }
}

inline const char* ToolboxPluginBase::trampoline_save_config(void* ctx) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    self->config_buf_ = self->saveConfig();
    return self->config_buf_.c_str();
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return "{}";
  } catch (...) {
    self->last_error_ = "Unknown exception in save_config";
    return "{}";
  }
}

inline bool ToolboxPluginBase::trampoline_load_config(void* ctx, const char* config_json) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    auto status = self->loadConfig(config_json == nullptr ? std::string_view{} : std::string_view(config_json));
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in load_config";
    return false;
  }
}

inline void* ToolboxPluginBase::trampoline_get_dialog_context(void* ctx) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    return self->dialogContext();
  } catch (...) {
    return nullptr;
  }
}

inline const char* ToolboxPluginBase::trampoline_get_last_error(void* ctx) {
  auto* self = static_cast<ToolboxPluginBase*>(ctx);
  try {
    self->last_error_ = self->lastError();
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
  } catch (...) {
    self->last_error_ = "Unknown exception in get_last_error";
  }
  return self->last_error_.empty() ? nullptr : self->last_error_.c_str();
}

}  // namespace PJ
