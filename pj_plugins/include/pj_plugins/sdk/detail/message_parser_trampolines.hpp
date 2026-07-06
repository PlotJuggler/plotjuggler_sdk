/**
 * @file detail/message_parser_trampolines.hpp
 * @brief Out-of-line C ABI trampolines for MessageParserPluginBase (v4).
 *
 * Included automatically by message_parser_plugin_base.hpp.
 * Every trampoline is `noexcept` — the v4 vtable requires it.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace PJ {

inline void MessageParserPluginBase::trampoline_destroy(void* ctx) noexcept {
  try {
    delete static_cast<MessageParserPluginBase*>(ctx);
  } catch (...) {}
}

inline bool MessageParserPluginBase::trampoline_bind(
    void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_bind_schema(
    void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  try {
    auto name_sv = type_name.data == nullptr ? std::string_view{} : std::string_view(type_name.data, type_name.size);
    Span<const uint8_t> schema_span(schema.data, schema.size);
    auto status = self->bindSchema(name_sv, schema_span);
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("bind_schema threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in bind_schema");
    return false;
  }
}

inline bool MessageParserPluginBase::trampoline_save_config(
    void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_load_config(
    void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
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

inline bool MessageParserPluginBase::trampoline_parse(
    void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  try {
    Span<const uint8_t> payload_span(payload.data, payload.size);
    auto status = self->parse(timestamp_ns, payload_span);
    if (!status) {
      self->storeError(out_error, 1, "plugin", std::move(status).error());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("parse threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in parse");
    return false;
  }
}

inline const void* MessageParserPluginBase::trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  try {
    std::string_view sv = id.data == nullptr ? std::string_view{} : std::string_view(id.data, id.size);
    return self->pluginExtension(sv);
  } catch (...) {
    return nullptr;
  }
}

// -----------------------------------------------------------------------------
// Pure-functional API trampolines (builtin-object tail of the vtable)
// -----------------------------------------------------------------------------

inline bool MessageParserPluginBase::trampoline_classify_schema(
    void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_schema_classification_t* out_classification,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  if (out_classification == nullptr) {
    self->storeError(out_error, 2, "plugin", "classify_schema called with null out_classification");
    return false;
  }
  try {
    auto name_sv = type_name.data == nullptr ? std::string_view{} : std::string_view(type_name.data, type_name.size);
    Span<const uint8_t> schema_span(schema.data, schema.size);
    const auto cls = self->classifySchema(name_sv, schema_span);
    out_classification->object_type = static_cast<uint16_t>(cls.object_type);
    out_classification->reserved = 0;
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("classify_schema threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in classify_schema");
    return false;
  }
}

inline bool MessageParserPluginBase::trampoline_describe_schema_columns(
    void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_string_view_t* out_columns_json,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  if (out_columns_json == nullptr) {
    self->storeError(out_error, 2, "plugin", "describe_schema_columns called with null out_columns_json");
    return false;
  }
  try {
    auto name_sv = type_name.data == nullptr ? std::string_view{} : std::string_view(type_name.data, type_name.size);
    Span<const uint8_t> schema_span(schema.data, schema.size);
    auto columns = self->describeSchemaColumns(name_sv, schema_span);
    if (!columns) {
      self->storeError(out_error, 1, "plugin", std::move(columns).error());
      return false;
    }
    // Hand-rolled serialization: paths are the only free-form strings, so a
    // minimal JSON string escape keeps pj_plugins' SDK headers dependency-free
    // for plugin authors.
    std::string& json = self->columns_json_buf_;
    json.clear();
    json.push_back('[');
    bool first = true;
    for (const auto& col : *columns) {
      if (!first) {
        json.push_back(',');
      }
      first = false;
      json.append("{\"path\":\"");
      for (const char c : col.path) {
        switch (c) {
          case '"':
            json.append("\\\"");
            break;
          case '\\':
            json.append("\\\\");
            break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              constexpr char kHex[] = "0123456789abcdef";
              json.append("\\u00");
              json.push_back(kHex[(c >> 4) & 0xF]);
              json.push_back(kHex[c & 0xF]);
            } else {
              json.push_back(c);
            }
        }
      }
      json.append("\",\"type\":\"");
      json.append(sdk::primitiveTypeJsonName(col.type));
      json.append("\"}");
    }
    json.push_back(']');
    out_columns_json->data = json.data();
    out_columns_json->size = json.size();
    return true;
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("describe_schema_columns threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in describe_schema_columns");
    return false;
  }
}

}  // namespace PJ
