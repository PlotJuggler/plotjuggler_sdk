/**
 * @file detail/data_source_trampolines.hpp
 * @brief Out-of-line definitions for DataSourcePluginBase C ABI trampolines (v4).
 *
 * Included automatically by data_source_plugin_base.hpp — do not include directly.
 * Each trampoline wraps a virtual call with try-catch for full exception
 * safety across the C ABI boundary and populates `PJ_error_t*` out-params
 * via the plugin's per-instance error buffer. Every trampoline is `noexcept`
 * — the v4 vtable requires it.
 */
#pragma once

namespace PJ {

inline void DataSourcePluginBase::trampoline_destroy(void* ctx) noexcept {
  try {
    delete static_cast<DataSourcePluginBase*>(ctx);
  } catch (...) {}
}

inline uint64_t DataSourcePluginBase::trampoline_capabilities(void* ctx) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    return self->capabilities();
  } catch (const std::exception& e) {
    self->storeError(nullptr, 1, "plugin", std::string("capabilities threw: ") + e.what());
    return 0;
  } catch (...) {
    self->storeError(nullptr, 1, "plugin", "unknown exception in capabilities");
    return 0;
  }
}

inline bool DataSourcePluginBase::trampoline_bind(
    void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline bool DataSourcePluginBase::trampoline_save_config(
    void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline bool DataSourcePluginBase::trampoline_load_config(
    void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline bool DataSourcePluginBase::trampoline_start(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->start();
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("start threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in start");
    return false;
  }
}

inline void DataSourcePluginBase::trampoline_stop(void* ctx) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    self->stop();
  } catch (const std::exception& e) {
    self->storeError(nullptr, 1, "plugin", std::string("stop threw: ") + e.what());
  } catch (...) {
    self->storeError(nullptr, 1, "plugin", "unknown exception in stop");
  }
}

inline bool DataSourcePluginBase::trampoline_pause(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->pause();
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("pause threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in pause");
    return false;
  }
}

inline bool DataSourcePluginBase::trampoline_resume(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->resume();
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("resume threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in resume");
    return false;
  }
}

inline bool DataSourcePluginBase::trampoline_poll(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->poll();
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("poll threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in poll");
    return false;
  }
}

inline PJ_data_source_state_t DataSourcePluginBase::trampoline_current_state(void* ctx) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    return static_cast<PJ_data_source_state_t>(self->currentState());
  } catch (...) {
    return PJ_DATA_SOURCE_STATE_FAILED;
  }
}

inline PJ_borrowed_dialog_t DataSourcePluginBase::trampoline_get_dialog(void* ctx) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    return self->getDialog();
  } catch (...) {
    return PJ_borrowed_dialog_t{nullptr, nullptr};
  }
}

inline const void* DataSourcePluginBase::trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    std::string_view sv = id.data == nullptr ? std::string_view{} : std::string_view(id.data, id.size);
    return self->pluginExtension(sv);
  } catch (...) {
    return nullptr;
  }
}

}  // namespace PJ
