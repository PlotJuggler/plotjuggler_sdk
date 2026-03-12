/**
 * @file detail/data_source_trampolines.hpp
 * @brief Out-of-line definitions for DataSourcePluginBase C ABI trampolines.
 *
 * Included automatically by data_source_plugin_base.hpp — do not include directly.
 * Each trampoline wraps a virtual call with try-catch for full exception safety
 * across the C ABI boundary.
 */
#pragma once

namespace PJ {

inline void DataSourcePluginBase::trampoline_destroy(void* ctx) {
  try {
    delete static_cast<DataSourcePluginBase*>(ctx);
  } catch (...) {
  }
}

inline uint64_t DataSourcePluginBase::trampoline_capabilities(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline bool DataSourcePluginBase::trampoline_bind_write_host(
    void* ctx, PJ_source_write_host_t write_host) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->bindWriteHost(write_host);
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in bind_write_host";
    return false;
  }
}

inline bool DataSourcePluginBase::trampoline_bind_runtime_host(
    void* ctx, PJ_data_source_runtime_host_t runtime_host) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline const char* DataSourcePluginBase::trampoline_save_config(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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

inline bool DataSourcePluginBase::trampoline_load_config(void* ctx, const char* config_json) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->loadConfig(
        config_json == nullptr ? std::string_view{} : std::string_view(config_json));
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

inline bool DataSourcePluginBase::trampoline_start(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->start();
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in start";
    return false;
  }
}

inline void DataSourcePluginBase::trampoline_stop(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    self->stop();
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
  } catch (...) {
    self->last_error_ = "Unknown exception in stop";
  }
}

inline bool DataSourcePluginBase::trampoline_pause(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->pause();
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in pause";
    return false;
  }
}

inline bool DataSourcePluginBase::trampoline_resume(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->resume();
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in resume";
    return false;
  }
}

inline bool DataSourcePluginBase::trampoline_poll(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    auto status = self->poll();
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in poll";
    return false;
  }
}

inline PJ_data_source_state_t DataSourcePluginBase::trampoline_current_state(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    return static_cast<PJ_data_source_state_t>(self->currentState());
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return PJ_DATA_SOURCE_STATE_FAILED;
  } catch (...) {
    self->last_error_ = "Unknown exception in current_state";
    return PJ_DATA_SOURCE_STATE_FAILED;
  }
}

inline void* DataSourcePluginBase::trampoline_get_dialog_context(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
  try {
    return self->dialogContext();
  } catch (...) {
    return nullptr;
  }
}

inline const char* DataSourcePluginBase::trampoline_get_last_error(void* ctx) {
  auto* self = static_cast<DataSourcePluginBase*>(ctx);
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
