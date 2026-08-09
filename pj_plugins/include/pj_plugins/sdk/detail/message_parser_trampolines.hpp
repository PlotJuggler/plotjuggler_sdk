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
    // Once a schema is bound, advertise the functional route only if THIS
    // schema has a handler. A mixed-model plugin may register handlers for
    // some schemas and keep legacy parse() for the rest; gating on "any
    // handler exists" would make the host prefer a functional route that
    // parseScalars/parseObject then reject for the unhandled schema. Before
    // binding, any registered handler still advertises the capability, since
    // no schema-specific answer exists yet and hosts query again after
    // binding without caching an earlier absence.
    const bool functional_route_available = self->bound_type_name_.empty()
                                                ? !self->handlers_.empty()
                                                : self->findSchemaHandler(self->bound_type_name_) != nullptr;
    if (sv == PJ_PARSER_FUNCTIONAL_EXTENSION_V1 && functional_route_available) {
      static const PJ_parser_functional_v1_t extension{
          .struct_size = sizeof(PJ_parser_functional_v1_t),
          .parse_scalars = trampoline_parse_scalars_functional,
          .parse_object = trampoline_parse_object_functional,
      };
      return &extension;
    }
    return self->pluginExtension(sv);
  } catch (...) {
    return nullptr;
  }
}

inline bool MessageParserPluginBase::trampoline_parse_scalars_functional(
    void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, const PJ_parser_scalar_sink_v1_t* sink,
    PJ_error_t* out_error) noexcept {
  if (ctx == nullptr) {
    storeError(out_error, 2, "plugin", "parse_scalars called with null plugin context");
    return false;
  }
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  if (sink == nullptr || sink->struct_size < PJ_PARSER_SCALAR_SINK_V1_MIN_SIZE || sink->accept_record == nullptr) {
    self->storeError(out_error, 2, "plugin", "parse_scalars called with invalid scalar sink");
    return false;
  }
  if (payload.data == nullptr && payload.size != 0) {
    self->storeError(out_error, 2, "plugin", "parse_scalars called with invalid payload");
    return false;
  }

  try {
    auto record = self->parseScalars(timestamp_ns, Span<const uint8_t>(payload.data, payload.size));
    if (!record) {
      self->storeError(out_error, 1, "plugin", std::move(record).error());
      return false;
    }
    const auto fields = sdk::toAbiNamed(Span<const sdk::NamedFieldValue>(record->fields));
    return sink->accept_record(
        sink->ctx, record->ts.has_value(), record->ts.value_or(0), fields.data(), fields.size(), out_error);
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("parse_scalars threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in parse_scalars");
    return false;
  }
}

inline bool MessageParserPluginBase::trampoline_parse_object_functional(
    void* ctx, int64_t timestamp_ns, PJ_payload_t payload, const PJ_parser_object_sink_v1_t* sink,
    PJ_error_t* out_error) noexcept {
  if (payload.anchor.ctx != nullptr && payload.anchor.release == nullptr) {
    storeError(out_error, 2, "plugin", "parse_object payload anchor has context without release callback");
    return false;
  }

  // parse_object consumes the one C anchor reference on every path. Copies of
  // this shared_ptr remain entirely inside the plugin DSO and keep the host
  // payload alive until the returned object has been serialized.
  std::shared_ptr<void> payload_owner;
  if (payload.anchor.release != nullptr) {
    try {
      payload_owner = std::shared_ptr<void>(payload.anchor.ctx, payload.anchor.release);
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("payload anchor adoption failed: ") + e.what());
      return false;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception adopting payload anchor");
      return false;
    }
  }
  if (ctx == nullptr) {
    storeError(out_error, 2, "plugin", "parse_object called with null plugin context");
    return false;
  }
  auto* self = static_cast<MessageParserPluginBase*>(ctx);
  if (sink == nullptr || sink->struct_size < PJ_PARSER_OBJECT_SINK_V1_MIN_SIZE || sink->accept_object == nullptr) {
    self->storeError(out_error, 2, "plugin", "parse_object called with invalid object sink");
    return false;
  }
  if (payload.data == nullptr && payload.size != 0) {
    self->storeError(out_error, 2, "plugin", "parse_object called with invalid payload");
    return false;
  }

  try {
    sdk::PayloadView payload_view{Span<const uint8_t>(payload.data, payload.size), std::move(payload_owner)};
    auto record = self->parseObject(timestamp_ns, std::move(payload_view));
    if (!record) {
      self->storeError(out_error, 1, "plugin", std::move(record).error());
      return false;
    }
    const auto object_type = sdk::typeOf(record->object);
    auto canonical_wire = serializeBuiltinObject(record->object);
    if (!canonical_wire) {
      self->storeError(out_error, 1, "plugin", std::move(canonical_wire).error());
      return false;
    }
    return sink->accept_object(
        sink->ctx, record->ts.has_value(), record->ts.value_or(0), static_cast<uint16_t>(object_type),
        PJ_bytes_view_t{canonical_wire->data(), canonical_wire->size()}, out_error);
  } catch (const std::exception& e) {
    self->storeError(out_error, 1, "plugin", std::string("parse_object threw: ") + e.what());
    return false;
  } catch (...) {
    self->storeError(out_error, 1, "plugin", "unknown exception in parse_object");
    return false;
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

}  // namespace PJ
