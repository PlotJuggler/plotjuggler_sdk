/**
 * @file detail/message_parser_trampolines.hpp
 * @brief Out-of-line definitions for MessageParserPluginBase C ABI trampolines.
 *
 * Included automatically by message_parser_plugin_base.hpp — do not include directly.
 * Each trampoline wraps a virtual call with try-catch for full exception safety
 * across the C ABI boundary.
 */
#pragma once

namespace PJ {

inline void MessageParserPluginBase::trampoline_destroy(void* ctx) {
  try {
    delete static_cast<MessageParserPluginBase*>(ctx);
  } catch (...) {
  }
}

inline bool MessageParserPluginBase::trampoline_bind_write_host(
    void* ctx, PJ_parser_write_host_t write_host) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_bind_schema(
    void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  try {
    auto status = self->bindSchema(
        std::string_view(type_name.data, type_name.size),
        Span<const uint8_t>(schema.data, schema.size));
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in bind_schema";
    return false;
  }
}

inline const char* MessageParserPluginBase::trampoline_save_config(void* ctx) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_load_config(void* ctx, const char* config_json) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_parse(
    void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  try {
    auto status = self->parse(
        Timestamp{timestamp_ns}, Span<const uint8_t>(payload.data, payload.size));
    if (!status) {
      self->last_error_ = std::move(status).error();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->last_error_ = e.what();
    return false;
  } catch (...) {
    self->last_error_ = "Unknown exception in parse";
    return false;
  }
}

inline const char* MessageParserPluginBase::trampoline_get_last_error(void* ctx) {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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
