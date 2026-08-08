/**
 * @file message_parser_handle.hpp
 * @brief RAII wrapper around a single MessageParser plugin instance (v4).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_base/builtin_object_abi.h>
#include <pj_base/message_parser_protocol.h>
#include <pj_base/parser_functional_protocol.h>

#include <cassert>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <pj_base/builtin/builtin_object.hpp>
#include <pj_base/builtin/builtin_object_codec.hpp>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/data_source_host_views.hpp>
#include <pj_base/span.hpp>
#include <pj_base/types.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// RAII handle owning a MessageParser plugin instance.
class MessageParserHandle {
 public:
  using ScalarRecordSink = std::function<Status(std::optional<Timestamp>, Span<const PJ_named_field_value_t>)>;

  explicit MessageParserHandle(const PJ_message_parser_vtable_t* vt, std::shared_ptr<void> library_owner = {})
      : vt_(vt), library_owner_(std::move(library_owner)) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_MESSAGE_PARSER_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~MessageParserHandle() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
  }

  MessageParserHandle(MessageParserHandle&& other) noexcept
      : vt_(other.vt_), ctx_(other.ctx_), library_owner_(std::move(other.library_owner_)) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  MessageParserHandle& operator=(MessageParserHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(library_owner_, other.library_owner_);
    }
    return *this;
  }

  MessageParserHandle(const MessageParserHandle&) = delete;
  MessageParserHandle& operator=(const MessageParserHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
  }

  [[nodiscard]] std::string manifest() const {
    return vt_->manifest_json != nullptr ? std::string(vt_->manifest_json) : std::string();
  }

  [[nodiscard]] Status bind(PJ_service_registry_t registry) {
    PJ_error_t err{};
    if (!vt_->bind(ctx_, registry, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    PJ_string_view_t tn{type_name.data(), type_name.size()};
    PJ_bytes_view_t sc{schema.data(), schema.size()};
    PJ_error_t err{};
    if (!vt_->bind_schema(ctx_, tn, sc, &err)) {
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

  [[nodiscard]] Status parse(Timestamp timestamp_ns, Span<const uint8_t> payload) {
    PJ_bytes_view_t bytes{payload.data(), payload.size()};
    PJ_error_t err{};
    if (!vt_->parse(ctx_, timestamp_ns, bytes, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// True when this parser exposes the 0.21 pure-functional C extension.
  /// Older parsers return false and can be handled through the deprecated
  /// in-process C++ compatibility bridge until the next SDK major version.
  [[nodiscard]] bool supportsFunctionalParsing() const {
    return functionalExtension() != nullptr;
  }

  /// Parse one scalar record and synchronously deliver borrowed C ABI values
  /// to @p sink. The sink must copy names/string values it retains.
  [[nodiscard]] Status parseScalarsFunctional(
      Timestamp timestamp_ns, Span<const uint8_t> payload, const ScalarRecordSink& sink) const {
    const auto* extension = functionalExtension();
    if (extension == nullptr) {
      return unexpected(std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
    }
    if (!sink) {
      return unexpected(std::string("scalar record sink is empty"));
    }

    ScalarSinkState state{.sink = &sink};
    PJ_parser_scalar_sink_v1_t abi_sink{
        .struct_size = sizeof(PJ_parser_scalar_sink_v1_t),
        .ctx = &state,
        .accept_record = acceptScalarRecord,
    };
    PJ_error_t error{};
    const PJ_bytes_view_t bytes{payload.data(), payload.size()};
    if (!extension->parse_scalars(ctx_, timestamp_ns, bytes, &abi_sink, &error)) {
      return unexpected(sdk::errorToString(error));
    }
    if (state.call_count != 1) {
      return unexpected(std::string("functional parser returned success without exactly one scalar record"));
    }
    return okStatus();
  }

  /// Parse one canonical object through the C extension. Canonical wire bytes
  /// are decoded synchronously into host-owned SDK storage before returning.
  [[nodiscard]] Expected<sdk::ObjectRecord> parseObjectFunctional(
      Timestamp timestamp_ns, Span<const uint8_t> payload) const {
    const auto* extension = functionalExtension();
    if (extension == nullptr) {
      return unexpected(std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
    }
    return parseObjectFunctionalAbi(
        extension, timestamp_ns, PJ_payload_t{.data = payload.data(), .size = payload.size(), .anchor = {}});
  }

  /// Anchored overload for zero-copy input into the plugin. Ownership of one
  /// anchor reference is transferred to the extension and released after the
  /// plugin's object has been serialized, including every failure path.
  [[nodiscard]] Expected<sdk::ObjectRecord> parseObjectFunctional(
      Timestamp timestamp_ns, sdk::PayloadView payload) const {
    const auto* extension = functionalExtension();
    if (extension == nullptr) {
      return unexpected(std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
    }

    PJ_payload_t abi_payload{.data = payload.bytes.data(), .size = payload.bytes.size(), .anchor = {}};
    if (payload.anchor) {
      auto* held = new sdk::BufferAnchor(std::move(payload.anchor));
      abi_payload.anchor.ctx = held;
      abi_payload.anchor.release = [](void* ctx) noexcept { delete static_cast<sdk::BufferAnchor*>(ctx); };
    }
    return parseObjectFunctionalAbi(extension, timestamp_ns, abi_payload);
  }

  /// A priori classification of the bound schema. Tail-slot gated; when
  /// the plugin doesn't expose classify_schema (older protocol header)
  /// returns kNone, matching the host contract documented in
  /// message_parser_protocol.h.
  [[nodiscard]] sdk::BuiltinObjectType classifySchema(std::string_view type_name, Span<const uint8_t> schema) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_message_parser_vtable_t, vt_, classify_schema)) {
      return sdk::BuiltinObjectType::kNone;
    }
    PJ_string_view_t tn{type_name.data(), type_name.size()};
    PJ_bytes_view_t sc{schema.data(), schema.size()};
    PJ_schema_classification_t out{};
    PJ_error_t err{};
    if (!vt_->classify_schema(ctx_, tn, sc, &out, &err)) {
      return sdk::BuiltinObjectType::kNone;
    }
    return static_cast<sdk::BuiltinObjectType>(out.object_type);
  }

  /// Query a plugin-exposed extension by reverse-DNS id. Tail-slot gated.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_message_parser_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    PJ_string_view_t sv{id.data(), id.size()};
    return vt_->get_plugin_extension(ctx_, sv);
  }

  [[nodiscard]] const PJ_message_parser_vtable_t* vtable() const {
    return vt_;
  }

  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  [[nodiscard]] Expected<sdk::ObjectRecord> parseObjectFunctionalAbi(
      const PJ_parser_functional_v1_t* extension, Timestamp timestamp_ns, PJ_payload_t payload) const {
    ObjectSinkState state;
    PJ_parser_object_sink_v1_t abi_sink{
        .struct_size = sizeof(PJ_parser_object_sink_v1_t),
        .ctx = &state,
        .accept_object = acceptObject,
    };
    PJ_error_t error{};
    if (!extension->parse_object(ctx_, timestamp_ns, payload, &abi_sink, &error)) {
      return unexpected(sdk::errorToString(error));
    }
    if (state.call_count != 1 || !state.record.has_value()) {
      return unexpected(std::string("functional parser returned success without exactly one canonical object"));
    }
    return std::move(*state.record);
  }

  struct ScalarSinkState {
    const ScalarRecordSink* sink = nullptr;
    uint32_t call_count = 0;
  };

  struct ObjectSinkState {
    std::optional<sdk::ObjectRecord> record;
    uint32_t call_count = 0;
  };

  [[nodiscard]] const PJ_parser_functional_v1_t* functionalExtension() const {
    const auto* extension =
        static_cast<const PJ_parser_functional_v1_t*>(getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
    if (extension == nullptr || extension->struct_size < PJ_PARSER_FUNCTIONAL_V1_MIN_SIZE ||
        extension->parse_scalars == nullptr || extension->parse_object == nullptr) {
      return nullptr;
    }
    return extension;
  }

  static bool acceptScalarRecord(
      void* ctx, bool has_timestamp, int64_t timestamp_ns, const PJ_named_field_value_t* fields, uint64_t field_count,
      PJ_error_t* out_error) noexcept {
    auto* state = static_cast<ScalarSinkState*>(ctx);
    if (state == nullptr || state->sink == nullptr) {
      sdk::fillError(out_error, 2, "host", "scalar callback received invalid context");
      return false;
    }
    ++state->call_count;
    if (state->call_count != 1) {
      sdk::fillError(out_error, 2, "host", "functional parser emitted more than one scalar record");
      return false;
    }
    if (fields == nullptr && field_count != 0) {
      sdk::fillError(out_error, 2, "host", "scalar callback received invalid fields");
      return false;
    }
    try {
      const std::optional<Timestamp> timestamp = has_timestamp ? std::optional<Timestamp>(timestamp_ns) : std::nullopt;
      auto status = (*state->sink)(timestamp, Span<const PJ_named_field_value_t>(fields, field_count));
      if (!status) {
        sdk::fillError(out_error, 1, "host", std::move(status).error());
        return false;
      }
      return true;
    } catch (const std::exception& e) {
      sdk::fillError(out_error, 1, "host", std::string("scalar sink threw: ") + e.what());
      return false;
    } catch (...) {
      sdk::fillError(out_error, 1, "host", "unknown exception in scalar sink");
      return false;
    }
  }

  static bool acceptObject(
      void* ctx, bool has_timestamp, int64_t timestamp_ns, uint16_t object_type, PJ_bytes_view_t canonical_wire,
      PJ_error_t* out_error) noexcept {
    auto* state = static_cast<ObjectSinkState*>(ctx);
    if (state == nullptr) {
      sdk::fillError(out_error, 2, "host", "object callback received invalid context");
      return false;
    }
    ++state->call_count;
    if (state->call_count != 1) {
      sdk::fillError(out_error, 2, "host", "functional parser emitted more than one canonical object");
      return false;
    }
    if (canonical_wire.data == nullptr && canonical_wire.size != 0) {
      sdk::fillError(out_error, 2, "host", "object callback received invalid canonical wire bytes");
      return false;
    }
    try {
      auto object = deserializeBuiltinObject(
          static_cast<sdk::BuiltinObjectType>(object_type), canonical_wire.data, canonical_wire.size);
      if (!object) {
        sdk::fillError(out_error, 1, "host", std::move(object).error());
        return false;
      }
      state->record = sdk::ObjectRecord{
          .ts = has_timestamp ? std::optional<Timestamp>(timestamp_ns) : std::nullopt,
          .object = std::move(*object),
      };
      return true;
    } catch (const std::exception& e) {
      sdk::fillError(out_error, 1, "host", std::string("canonical object sink threw: ") + e.what());
      return false;
    } catch (...) {
      sdk::fillError(out_error, 1, "host", "unknown exception in canonical object sink");
      return false;
    }
  }

  const PJ_message_parser_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  std::shared_ptr<void> library_owner_;
};

}  // namespace PJ
