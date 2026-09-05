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
#include <limits>
#include <memory>
#include <optional>
#include <pj_base/builtin/builtin_object.hpp>
#include <pj_base/builtin/builtin_object_codec.hpp>
#include <pj_base/builtin/grid_map_codec.hpp>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/data_source_host_views.hpp>
#include <pj_base/span.hpp>
#include <pj_base/types.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
      : vt_(other.vt_),
        ctx_(other.ctx_),
        library_owner_(std::move(other.library_owner_)),
        expected_object_type_(other.expected_object_type_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  MessageParserHandle& operator=(MessageParserHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(library_owner_, other.library_owner_);
      std::swap(expected_object_type_, other.expected_object_type_);
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
    expected_object_type_.reset();
    PJ_string_view_t tn{type_name.data(), type_name.size()};
    PJ_bytes_view_t sc{schema.data(), schema.size()};
    PJ_error_t err{};
    if (!vt_->bind_schema(ctx_, tn, sc, &err)) {
      return unexpected(errorToString(err));
    }
    if (PJ_HAS_TAIL_SLOT(PJ_message_parser_vtable_t, vt_, classify_schema)) {
      PJ_schema_classification_t classification{};
      err = {};
      if (vt_->classify_schema(ctx_, tn, sc, &classification, &err)) {
        expected_object_type_ = static_cast<sdk::BuiltinObjectType>(classification.object_type);
      }
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
    const auto extensions = functionalExtensions();
    return extensions.v2 != nullptr || extensions.v1 != nullptr;
  }

  /// Parse one scalar record and synchronously deliver borrowed C ABI values
  /// to @p sink. The sink must copy names/string values it retains.
  [[nodiscard]] Status parseScalarsFunctional(
      Timestamp timestamp_ns, Span<const uint8_t> payload, const ScalarRecordSink& sink) const {
    const auto extensions = functionalExtensions();
    if (extensions.v2 == nullptr && extensions.v1 == nullptr) {
      return unexpected(
          std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V2 + " or " +
          PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
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
    const auto parse_scalars = extensions.v2 != nullptr ? extensions.v2->parse_scalars : extensions.v1->parse_scalars;
    if (!parse_scalars(ctx_, timestamp_ns, bytes, &abi_sink, &error)) {
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
    const auto extensions = functionalExtensions();
    if (extensions.v2 == nullptr && extensions.v1 == nullptr) {
      return unexpected(
          std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V2 + " or " +
          PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
    }
    return parseObjectFunctionalAbi(
        extensions, timestamp_ns, PJ_payload_t{.data = payload.data(), .size = payload.size(), .anchor = {}}, payload,
        {});
  }

  /// Anchored overload for zero-copy input into the plugin. Ownership of one
  /// anchor reference is transferred to the extension and released after the
  /// plugin's object has been serialized, including every failure path.
  [[nodiscard]] Expected<sdk::ObjectRecord> parseObjectFunctional(
      Timestamp timestamp_ns, sdk::PayloadView payload) const {
    const auto extensions = functionalExtensions();
    if (extensions.v2 == nullptr && extensions.v1 == nullptr) {
      return unexpected(
          std::string("message parser does not expose ") + PJ_PARSER_FUNCTIONAL_EXTENSION_V2 + " or " +
          PJ_PARSER_FUNCTIONAL_EXTENSION_V1);
    }

    PJ_payload_t abi_payload{.data = payload.bytes.data(), .size = payload.bytes.size(), .anchor = {}};
    sdk::BufferAnchor splice_anchor = payload.anchor;
    if (payload.anchor) {
      auto* held = new sdk::BufferAnchor(std::move(payload.anchor));
      abi_payload.anchor.ctx = held;
      abi_payload.anchor.release = [](void* ctx) noexcept { delete static_cast<sdk::BufferAnchor*>(ctx); };
    }
    return parseObjectFunctionalAbi(extensions, timestamp_ns, abi_payload, payload.bytes, std::move(splice_anchor));
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
    const auto result = static_cast<sdk::BuiltinObjectType>(out.object_type);
    expected_object_type_ = result;
    return result;
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
  struct FunctionalExtensions {
    const PJ_parser_functional_v2_t* v2 = nullptr;
    const PJ_parser_functional_v1_t* v1 = nullptr;
  };

  [[nodiscard]] Expected<sdk::ObjectRecord> parseObjectFunctionalAbi(
      FunctionalExtensions extensions, Timestamp timestamp_ns, PJ_payload_t payload, Span<const uint8_t> input_payload,
      sdk::BufferAnchor input_anchor) const {
    ObjectSinkState state{
        .record = {},
        .input_payload = input_payload,
        .input_anchor = std::move(input_anchor),
        .expected_object_type = expected_object_type_,
        .call_count = 0,
    };
    PJ_error_t error{};
    if (extensions.v2 != nullptr) {
      PJ_parser_object_sink_v2_t abi_sink{
          .struct_size = sizeof(PJ_parser_object_sink_v2_t),
          .ctx = &state,
          .accept_object = acceptObject,
          .accept_object_spliced = acceptObjectSpliced,
      };
      if (!extensions.v2->parse_object(ctx_, timestamp_ns, payload, &abi_sink, &error)) {
        return unexpected(sdk::errorToString(error));
      }
    } else {
      PJ_parser_object_sink_v1_t abi_sink{
          .struct_size = sizeof(PJ_parser_object_sink_v1_t),
          .ctx = &state,
          .accept_object = acceptObject,
      };
      if (!extensions.v1->parse_object(ctx_, timestamp_ns, payload, &abi_sink, &error)) {
        return unexpected(sdk::errorToString(error));
      }
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
    Span<const uint8_t> input_payload;
    sdk::BufferAnchor input_anchor;
    std::optional<sdk::BuiltinObjectType> expected_object_type;
    uint32_t call_count = 0;
  };

  [[nodiscard]] FunctionalExtensions functionalExtensions() const {
    const auto* v2 =
        static_cast<const PJ_parser_functional_v2_t*>(getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2));
    if (v2 == nullptr || v2->struct_size < PJ_PARSER_FUNCTIONAL_V2_MIN_SIZE || v2->parse_scalars == nullptr ||
        v2->parse_object == nullptr) {
      v2 = nullptr;
    }
    const auto* v1 =
        static_cast<const PJ_parser_functional_v1_t*>(getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
    if (v1 == nullptr || v1->struct_size < PJ_PARSER_FUNCTIONAL_V1_MIN_SIZE || v1->parse_scalars == nullptr ||
        v1->parse_object == nullptr) {
      v1 = nullptr;
    }
    return {.v2 = v2, .v1 = v1};
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
      fillContractViolation(out_error, "object callback received invalid canonical wire bytes");
      return false;
    }
    if (!validateExpectedObjectType(*state, object_type, out_error)) {
      return false;
    }
    try {
      if (canonical_wire.size > std::numeric_limits<size_t>::max()) {
        fillContractViolation(out_error, "canonical object wire exceeds the host size range");
        return false;
      }
      auto object = deserializeBuiltinObject(
          static_cast<sdk::BuiltinObjectType>(object_type), canonical_wire.data,
          static_cast<size_t>(canonical_wire.size));
      if (!object) {
        fillContractViolation(out_error, std::move(object).error());
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

  static bool acceptObjectSpliced(
      void* ctx, bool has_timestamp, int64_t timestamp_ns, uint16_t object_type, PJ_bytes_view_t canonical_wire,
      uint32_t splice_field_number, uint64_t input_offset, uint64_t input_length, PJ_error_t* out_error) noexcept {
    auto* state = static_cast<ObjectSinkState*>(ctx);
    if (state == nullptr) {
      fillContractViolation(out_error, "spliced-object callback received invalid context");
      return false;
    }
    ++state->call_count;
    if (state->call_count != 1) {
      fillContractViolation(out_error, "functional parser emitted more than one canonical object");
      return false;
    }
    if (!validateExpectedObjectType(*state, object_type, out_error)) {
      return false;
    }
    uint32_t eligible_field = 0;
    if (!pj_builtin_object_splice_field_number_v1(object_type, &eligible_field) ||
        splice_field_number != eligible_field) {
      fillContractViolation(out_error, "functional parser emitted an ineligible object splice field");
      return false;
    }
    const uint64_t payload_size = state->input_payload.size();
    if (input_offset > payload_size || input_length > payload_size - input_offset) {
      fillContractViolation(out_error, "functional parser emitted an out-of-bounds object splice");
      return false;
    }
    if ((canonical_wire.data == nullptr && canonical_wire.size != 0) ||
        canonical_wire.size > std::numeric_limits<size_t>::max()) {
      fillContractViolation(out_error, "spliced-object callback received invalid canonical wire bytes");
      return false;
    }

    try {
      const auto type = static_cast<sdk::BuiltinObjectType>(object_type);
      auto object = deserializeBuiltinObject(type, canonical_wire.data, static_cast<size_t>(canonical_wire.size));
      if (!object) {
        fillContractViolation(out_error, "spliced canonical object wire is malformed: " + object.error());
        return false;
      }
      const size_t offset = static_cast<size_t>(input_offset);
      const size_t length = static_cast<size_t>(input_length);
      Span<const uint8_t> materialized;
      sdk::BufferAnchor anchor = state->input_anchor;
      if (anchor) {
        const uint8_t* data = state->input_payload.data();
        materialized = Span<const uint8_t>(data == nullptr ? nullptr : data + offset, length);
      } else {
        auto owned = std::make_shared<std::vector<uint8_t>>();
        if (length != 0) {
          owned->assign(
              state->input_payload.begin() + static_cast<ptrdiff_t>(offset),
              state->input_payload.begin() + static_cast<ptrdiff_t>(offset + length));
        }
        materialized = Span<const uint8_t>(owned->data(), owned->size());
        anchor = std::move(owned);
      }
      const auto attach = [&]<typename Object>() {
        auto* typed = std::any_cast<Object>(&*object);
        if (typed == nullptr) {
          return false;
        }
        typed->data = materialized;
        typed->anchor = anchor;
        return true;
      };
      bool attached = false;
      switch (type) {
        case sdk::BuiltinObjectType::kImage:
          attached = attach.template operator()<sdk::Image>();
          break;
        case sdk::BuiltinObjectType::kPointCloud:
          attached = attach.template operator()<sdk::PointCloud>();
          break;
        case sdk::BuiltinObjectType::kDepthImage:
          attached = attach.template operator()<sdk::DepthImage>();
          break;
        case sdk::BuiltinObjectType::kOccupancyGrid:
          attached = attach.template operator()<sdk::OccupancyGrid>();
          break;
        case sdk::BuiltinObjectType::kCompressedPointCloud:
          attached = attach.template operator()<sdk::CompressedPointCloud>();
          break;
        case sdk::BuiltinObjectType::kMesh3D:
          attached = attach.template operator()<sdk::Mesh3D>();
          break;
        case sdk::BuiltinObjectType::kVideoFrame:
          attached = attach.template operator()<sdk::VideoFrame>();
          break;
        case sdk::BuiltinObjectType::kOccupancyGridUpdate:
          attached = attach.template operator()<sdk::OccupancyGridUpdate>();
          break;
        case sdk::BuiltinObjectType::kVoxelGrid:
          attached = attach.template operator()<sdk::VoxelGrid>();
          break;
        case sdk::BuiltinObjectType::kGridMap:
          attached = attach.template operator()<sdk::GridMap>();
          break;
        default:
          break;
      }
      if (!attached) {
        fillContractViolation(out_error, "functional parser splice object type is not materializable");
        return false;
      }
      // GridMap decodes header-only for splices; the data-length check waits
      // until the bytes exist, which is now.
      if (type == sdk::BuiltinObjectType::kGridMap) {
        if (auto valid = validateGridMap(*std::any_cast<sdk::GridMap>(&*object)); !valid) {
          fillContractViolation(out_error, "spliced GridMap layout is invalid: " + valid.error());
          return false;
        }
      }
      state->record = sdk::ObjectRecord{
          .ts = has_timestamp ? std::optional<Timestamp>(timestamp_ns) : std::nullopt,
          .object = std::move(*object),
      };
      return true;
    } catch (const std::exception& e) {
      sdk::fillError(out_error, 1, "host", std::string("spliced canonical object sink threw: ") + e.what());
      return false;
    } catch (...) {
      sdk::fillError(out_error, 1, "host", "unknown exception in spliced canonical object sink");
      return false;
    }
  }

  static void fillContractViolation(PJ_error_t* out_error, std::string_view message) noexcept {
    sdk::fillError(out_error, 2, "host", message);
    sdk::setExtended(out_error, PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION, nullptr);
  }

  static bool validateExpectedObjectType(
      const ObjectSinkState& state, uint16_t object_type, PJ_error_t* out_error) noexcept {
    if (!state.expected_object_type.has_value() || *state.expected_object_type == sdk::BuiltinObjectType::kNone ||
        static_cast<uint16_t>(*state.expected_object_type) == object_type) {
      return true;
    }
    fillContractViolation(
        out_error, "functional parser emitted an object type that differs from the bound classification");
    return false;
  }

  const PJ_message_parser_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  std::shared_ptr<void> library_owner_;
  mutable std::optional<sdk::BuiltinObjectType> expected_object_type_;
};

}  // namespace PJ
