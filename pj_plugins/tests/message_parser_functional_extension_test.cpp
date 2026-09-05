// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin/image.hpp"
#include "pj_base/parser_functional_protocol.h"
#include "pj_plugins/host/message_parser_handle.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

constexpr std::string_view kSchema = "example/Image";

class FunctionalParser final : public PJ::MessageParserPluginBase {
 public:
  FunctionalParser() {
    PJ::sdk::SchemaHandler handler;
    handler.object_type = PJ::sdk::BuiltinObjectType::kImage;
    handler.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      return PJ::sdk::ScalarRecord{
          .ts = 1234,
          .fields =
              {
                  {.name = "temperature", .value = 42.5},
                  {.name = "label", .value = std::string_view("camera")},
                  {.name = "missing", .value = PJ::sdk::TypedNull{PJ::PrimitiveType::kUint32}},
              },
      };
    };
    handler.parse_object = [](PJ::Timestamp timestamp,
                              PJ::sdk::PayloadView payload) -> PJ::Expected<PJ::sdk::ObjectRecord> {
      PJ::sdk::Image image;
      image.width = static_cast<uint32_t>(payload.bytes.size());
      image.height = 1;
      image.encoding = "mono8";
      image.row_step = image.width;
      image.data = payload.bytes;
      image.anchor = std::move(payload.anchor);
      image.timestamp_ns = timestamp;
      image.frame_id = "camera";
      return PJ::sdk::ObjectRecord{.ts = timestamp + 7, .object = std::move(image)};
    };
    registerSchemaHandler(kSchema, std::move(handler));
  }
};

class ThrowingFunctionalParser final : public PJ::MessageParserPluginBase {
 public:
  ThrowingFunctionalParser() {
    PJ::sdk::SchemaHandler handler;
    handler.object_type = PJ::sdk::BuiltinObjectType::kImage;
    handler.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      throw std::runtime_error("scalar explosion");
    };
    handler.parse_object = [](PJ::Timestamp, PJ::sdk::PayloadView) -> PJ::Expected<PJ::sdk::ObjectRecord> {
      throw std::runtime_error("object explosion");
    };
    registerSchemaHandler(kSchema, std::move(handler));
  }
};

class CustomExtensionParser final : public PJ::MessageParserPluginBase {
 public:
  const void* pluginExtension(std::string_view id) override {
    return id == "example.custom.v1" ? &marker_ : nullptr;
  }

  int marker_ = 17;
};

/// Registers a handler for one schema and keeps legacy parse() for every
/// other schema — the mixed model the parse() doc-comment sanctions.
class MixedModelParser final : public PJ::MessageParserPluginBase {
 public:
  MixedModelParser() {
    PJ::sdk::SchemaHandler handler;
    handler.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      return PJ::sdk::ScalarRecord{};
    };
    registerSchemaHandler(kSchema, std::move(handler));
  }

  PJ::Status parse(PJ::Timestamp, PJ::Span<const uint8_t>) override {
    return PJ::okStatus();
  }
};

class BindRegisteredParser final : public PJ::MessageParserPluginBase {
 public:
  PJ::Status bindSchema(std::string_view type_name, PJ::Span<const uint8_t> schema) override {
    auto status = MessageParserPluginBase::bindSchema(type_name, schema);
    if (!status) {
      return status;
    }
    PJ::sdk::SchemaHandler handler;
    handler.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      return PJ::sdk::ScalarRecord{};
    };
    registerSchemaHandler(type_name, std::move(handler));
    return PJ::okStatus();
  }
};

template <typename Parser>
const PJ_message_parser_vtable_t* parserVtable() {
  static const auto* vtable = PJ::MessageParserPluginBase::vtableWithCreate(
      []() noexcept -> void* {
        try {
          return new Parser();
        } catch (...) {
          return nullptr;
        }
      },
      R"({"id":"functional-test","name":"Functional Test","version":"1.0.0","encoding":["test"]})");
  return vtable;
}

const PJ_message_parser_vtable_t* legacyStyleVtable() {
  static const PJ_message_parser_vtable_t vtable = [] {
    PJ_message_parser_vtable_t copy = *parserVtable<FunctionalParser>();
    copy.get_plugin_extension = [](void*, PJ_string_view_t) noexcept -> const void* { return nullptr; };
    return copy;
  }();
  return &vtable;
}

bool emitNoScalars(void*, int64_t, PJ_bytes_view_t, const PJ_parser_scalar_sink_v1_t*, PJ_error_t*) noexcept {
  return true;
}

bool emitNoObject(void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v1_t*, PJ_error_t*) noexcept {
  return true;
}

bool emitTwoScalarRecords(
    void*, int64_t, PJ_bytes_view_t, const PJ_parser_scalar_sink_v1_t* sink, PJ_error_t* out_error) noexcept {
  if (sink == nullptr || sink->accept_record == nullptr) {
    return false;
  }
  (void)sink->accept_record(sink->ctx, false, 0, nullptr, 0, out_error);
  (void)sink->accept_record(sink->ctx, false, 0, nullptr, 0, out_error);
  return true;
}

bool emitUnknownObject(
    void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v1_t* sink, PJ_error_t* out_error) noexcept {
  if (sink == nullptr || sink->accept_object == nullptr) {
    return false;
  }
  const uint8_t byte = 0;
  return sink->accept_object(sink->ctx, false, 0, 999, PJ_bytes_view_t{&byte, sizeof(byte)}, out_error);
}

bool emitImageV1(void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v1_t* sink, PJ_error_t* out_error) noexcept {
  static constexpr std::array<uint8_t, 2> kImageWire{0x10, 0x01};
  return sink != nullptr && sink->accept_object != nullptr &&
         sink->accept_object(
             sink->ctx, false, 0, PJ_BUILTIN_OBJECT_TYPE_IMAGE, PJ_bytes_view_t{kImageWire.data(), kImageWire.size()},
             out_error);
}

bool emitSplicedPointCloudV2(
    void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v2_t* sink, PJ_error_t* out_error) noexcept {
  static constexpr std::array<uint8_t, 2> kPointCloudWire{0x10, 0x01};
  return sink != nullptr && sink->accept_object_spliced != nullptr &&
         sink->accept_object_spliced(
             sink->ctx, true, 88, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
             PJ_bytes_view_t{kPointCloudWire.data(), kPointCloudWire.size()}, 9, 1, 2, out_error);
}

// Header-only PJ.GridMap wire: column_count=1, row_count=1, cell_stride=1,
// row_stride=1 (fields 5..8); `data` (field 10) arrives as the splice.
bool emitSplicedGridMapV2(
    void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v2_t* sink, PJ_error_t* out_error) noexcept {
  static constexpr std::array<uint8_t, 8> kGridMapWire{0x28, 0x01, 0x30, 0x01, 0x38, 0x01, 0x40, 0x01};
  return sink != nullptr && sink->accept_object_spliced != nullptr &&
         sink->accept_object_spliced(
             sink->ctx, true, 89, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP,
             PJ_bytes_view_t{kGridMapWire.data(), kGridMapWire.size()}, 10, 1, 2, out_error);
}

// Same header as emitSplicedGridMapV2 but two columns (row_stride 2) and a
// one-byte splice: the attached bytes cannot cover the declared cells.
bool emitSplicedGridMapTooShortV2(
    void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v2_t* sink, PJ_error_t* out_error) noexcept {
  static constexpr std::array<uint8_t, 8> kGridMapWire{0x28, 0x02, 0x30, 0x01, 0x38, 0x01, 0x40, 0x02};
  return sink != nullptr && sink->accept_object_spliced != nullptr &&
         sink->accept_object_spliced(
             sink->ctx, true, 90, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP,
             PJ_bytes_view_t{kGridMapWire.data(), kGridMapWire.size()}, 10, 1, 1, out_error);
}

bool emitMismatchedImageV2(
    void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v2_t* sink, PJ_error_t* out_error) noexcept {
  static constexpr std::array<uint8_t, 2> kImageWire{0x10, 0x01};
  return sink != nullptr && sink->accept_object != nullptr &&
         sink->accept_object(
             sink->ctx, false, 0, PJ_BUILTIN_OBJECT_TYPE_IMAGE, PJ_bytes_view_t{kImageWire.data(), kImageWire.size()},
             out_error);
}

template <PJ_parser_parse_scalars_fn_t ParseScalars, PJ_parser_parse_object_fn_t ParseObject>
const PJ_message_parser_vtable_t* adversarialVtable() {
  static const PJ_parser_functional_v1_t extension{
      .struct_size = sizeof(PJ_parser_functional_v1_t),
      .parse_scalars = ParseScalars,
      .parse_object = ParseObject,
  };
  static const PJ_message_parser_vtable_t vtable = [] {
    PJ_message_parser_vtable_t copy = *parserVtable<FunctionalParser>();
    copy.get_plugin_extension = [](void*, PJ_string_view_t id) noexcept -> const void* {
      const std::string_view name{id.data == nullptr ? "" : id.data, id.size};
      return name == PJ_PARSER_FUNCTIONAL_EXTENSION_V1 ? &extension : nullptr;
    };
    return copy;
  }();
  return &vtable;
}

template <auto ParseObject, uint16_t ClassifiedType = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD>
const PJ_message_parser_vtable_t* adversarialV2Vtable() {
  static const PJ_parser_functional_v2_t extension{
      .struct_size = sizeof(PJ_parser_functional_v2_t),
      .parse_scalars = emitNoScalars,
      .parse_object = ParseObject,
  };
  static const PJ_message_parser_vtable_t vtable = [] {
    PJ_message_parser_vtable_t copy = *parserVtable<FunctionalParser>();
    copy.classify_schema = [](void*, PJ_string_view_t, PJ_bytes_view_t, PJ_schema_classification_t* out,
                              PJ_error_t*) noexcept {
      if (out == nullptr) {
        return false;
      }
      out->object_type = ClassifiedType;
      out->reserved = 0;
      return true;
    };
    copy.get_plugin_extension = [](void*, PJ_string_view_t id) noexcept -> const void* {
      const std::string_view name{id.data == nullptr ? "" : id.data, id.size};
      return name == PJ_PARSER_FUNCTIONAL_EXTENSION_V2 ? &extension : nullptr;
    };
    return copy;
  }();
  return &vtable;
}

TEST(MessageParserFunctionalExtension, NewlyBuiltParserExposesStableExtensionAutomatically) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());

  ASSERT_TRUE(handle.valid());
  EXPECT_TRUE(handle.supportsFunctionalParsing());
  const auto* extension =
      static_cast<const PJ_parser_functional_v1_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
  ASSERT_NE(extension, nullptr);
  EXPECT_GE(extension->struct_size, PJ_PARSER_FUNCTIONAL_V1_MIN_SIZE);
}

TEST(MessageParserFunctionalExtension, NewlyBuiltParserExposesV1AndV2AndV2EmitsFullCanonicalWire) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));

  const auto* v1 =
      static_cast<const PJ_parser_functional_v1_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
  const auto* v2 =
      static_cast<const PJ_parser_functional_v2_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2));
  ASSERT_NE(v1, nullptr);
  ASSERT_NE(v2, nullptr);
  EXPECT_GE(v1->struct_size, PJ_PARSER_FUNCTIONAL_V1_MIN_SIZE);
  EXPECT_GE(v2->struct_size, PJ_PARSER_FUNCTIONAL_V2_MIN_SIZE);
  EXPECT_NE(v2->parse_scalars, nullptr);

  struct SinkState {
    bool accepted_full = false;
    bool accepted_spliced = false;
    uint16_t object_type = PJ_BUILTIN_OBJECT_TYPE_NONE;
  } state;
  PJ_parser_object_sink_v2_t sink{
      .struct_size = sizeof(PJ_parser_object_sink_v2_t),
      .ctx = &state,
      .accept_object =
          [](void* ctx, bool, int64_t, uint16_t object_type, PJ_bytes_view_t wire, PJ_error_t*) noexcept {
            auto& captured = *static_cast<SinkState*>(ctx);
            captured.accepted_full = true;
            captured.object_type = object_type;
            return wire.size != 0;
          },
      .accept_object_spliced =
          [](void* ctx, bool, int64_t, uint16_t, PJ_bytes_view_t, uint32_t, uint64_t, uint64_t, PJ_error_t*) noexcept {
            static_cast<SinkState*>(ctx)->accepted_spliced = true;
            return true;
          },
  };
  const std::array<uint8_t, 3> payload{1, 2, 3};
  PJ_error_t error{};
  EXPECT_TRUE(v2->parse_object(
      handle.context(), 100, PJ_payload_t{.data = payload.data(), .size = payload.size(), .anchor = {}}, &sink, &error))
      << error.message;
  EXPECT_TRUE(state.accepted_full);
  EXPECT_FALSE(state.accepted_spliced);
  EXPECT_EQ(state.object_type, PJ_BUILTIN_OBJECT_TYPE_IMAGE);
}

TEST(MessageParserFunctionalExtension, V2FailureKindsAreFrozenAndCarryNoExtendedPayload) {
  PJ::MessageParserHandle throwing_handle(parserVtable<ThrowingFunctionalParser>());
  ASSERT_TRUE(throwing_handle.bindSchema(kSchema, {}));
  const auto* throwing_v2 = static_cast<const PJ_parser_functional_v2_t*>(
      throwing_handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2));
  ASSERT_NE(throwing_v2, nullptr);

  PJ_parser_scalar_sink_v1_t scalar_sink{
      .struct_size = sizeof(PJ_parser_scalar_sink_v1_t),
      .ctx = nullptr,
      .accept_record = [](void*, bool, int64_t, const PJ_named_field_value_t*, uint64_t,
                          PJ_error_t*) noexcept { return true; },
  };
  PJ_error_t error{};
  EXPECT_FALSE(throwing_v2->parse_scalars(throwing_handle.context(), 0, PJ_bytes_view_t{}, &scalar_sink, &error));
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION);
  EXPECT_EQ(error.extended, nullptr);

  PJ_parser_object_sink_v2_t object_sink{
      .struct_size = sizeof(PJ_parser_object_sink_v2_t),
      .ctx = nullptr,
      .accept_object = [](void*, bool, int64_t, uint16_t, PJ_bytes_view_t, PJ_error_t*) noexcept { return true; },
      .accept_object_spliced = [](void*, bool, int64_t, uint16_t, PJ_bytes_view_t, uint32_t, uint64_t, uint64_t,
                                  PJ_error_t*) noexcept { return true; },
  };
  error = {};
  EXPECT_FALSE(throwing_v2->parse_object(throwing_handle.context(), 0, PJ_payload_t{}, &object_sink, &error));
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION);
  EXPECT_EQ(error.extended, nullptr);

  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));
  const auto* v2 =
      static_cast<const PJ_parser_functional_v2_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2));
  ASSERT_NE(v2, nullptr);
  scalar_sink.accept_record = [](void*, bool, int64_t, const PJ_named_field_value_t*, uint64_t,
                                 PJ_error_t* out_error) noexcept {
    PJ::sdk::fillError(out_error, 7, "test", "sink declined record");
    return false;
  };
  error = {};
  EXPECT_FALSE(v2->parse_scalars(handle.context(), 0, PJ_bytes_view_t{}, &scalar_sink, &error));
  EXPECT_EQ(std::string_view(error.message), "sink declined record");
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_SINK_REJECTED);
  EXPECT_EQ(error.extended, nullptr);

  scalar_sink.accept_record = [](void*, bool, int64_t, const PJ_named_field_value_t*, uint64_t,
                                 PJ_error_t* out_error) noexcept {
    PJ::sdk::fillError(out_error, 7, "test", "sink detected a contract violation");
    PJ::sdk::setExtended(out_error, PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION, nullptr);
    return false;
  };
  error = {};
  EXPECT_FALSE(v2->parse_scalars(handle.context(), 0, PJ_bytes_view_t{}, &scalar_sink, &error));
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION);

  object_sink.accept_object = [](void*, bool, int64_t, uint16_t, PJ_bytes_view_t, PJ_error_t* out_error) noexcept {
    PJ::sdk::fillError(out_error, 7, "test", "object type contract violation");
    PJ::sdk::setExtended(out_error, PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION, nullptr);
    return false;
  };
  error = {};
  EXPECT_FALSE(v2->parse_object(handle.context(), 0, PJ_payload_t{}, &object_sink, &error));
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION);

  PJ::MessageParserHandle unbound_handle(parserVtable<FunctionalParser>());
  const auto* unbound_v2 = static_cast<const PJ_parser_functional_v2_t*>(
      unbound_handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2));
  ASSERT_NE(unbound_v2, nullptr);
  scalar_sink.accept_record = [](void*, bool, int64_t, const PJ_named_field_value_t*, uint64_t, PJ_error_t*) noexcept {
    return true;
  };
  error = {};
  EXPECT_FALSE(unbound_v2->parse_scalars(unbound_handle.context(), 0, PJ_bytes_view_t{}, &scalar_sink, &error));
  EXPECT_EQ(std::string_view(error.extended_kind), PJ_PARSER_ERROR_KIND_DATA_ERROR);
  EXPECT_EQ(error.extended, nullptr);
}

TEST(MessageParserFunctionalExtension, ScalarRouteDeliversOnlyCAbiValuesDuringTheSinkCall) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));

  bool called = false;
  const uint8_t payload[] = {1, 2, 3};
  const auto status = handle.parseScalarsFunctional(
      1000, payload,
      [&](std::optional<PJ::Timestamp> timestamp, PJ::Span<const PJ_named_field_value_t> fields) -> PJ::Status {
        called = true;
        EXPECT_EQ(timestamp, 1234);
        EXPECT_EQ(fields.size(), 3U);
        if (fields.size() != 3U) {
          return PJ::unexpected(std::string("unexpected scalar field count"));
        }

        EXPECT_EQ(std::string_view(fields[0].name.data, fields[0].name.size), "temperature");
        EXPECT_FALSE(fields[0].is_null);
        EXPECT_EQ(fields[0].value.type, PJ_PRIMITIVE_TYPE_FLOAT64);
        EXPECT_DOUBLE_EQ(fields[0].value.data.as_float64, 42.5);

        EXPECT_EQ(std::string_view(fields[1].name.data, fields[1].name.size), "label");
        EXPECT_EQ(fields[1].value.type, PJ_PRIMITIVE_TYPE_STRING);
        EXPECT_EQ(std::string_view(fields[1].value.data.as_string.data, fields[1].value.data.as_string.size), "camera");

        EXPECT_EQ(std::string_view(fields[2].name.data, fields[2].name.size), "missing");
        EXPECT_TRUE(fields[2].is_null);
        EXPECT_EQ(fields[2].value.type, PJ_PRIMITIVE_TYPE_UINT32);
        return PJ::okStatus();
      });

  EXPECT_TRUE(status) << status.error();
  EXPECT_TRUE(called);
}

TEST(MessageParserFunctionalExtension, ObjectRouteReturnsAHostOwnedTypedValue) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));

  const uint8_t payload[] = {9, 8, 7, 6};
  auto record = handle.parseObjectFunctional(2000, payload);

  ASSERT_TRUE(record) << record.error();
  EXPECT_EQ(record->ts, 2007);
  EXPECT_EQ(PJ::sdk::typeOf(record->object), PJ::sdk::BuiltinObjectType::kImage);
  const auto* image = record->object.get<PJ::sdk::Image>();
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 4U);
  EXPECT_EQ(image->height, 1U);
  EXPECT_EQ(image->encoding, "mono8");
  EXPECT_EQ(image->frame_id, "camera");
  EXPECT_EQ(image->data.size(), 4U);
  EXPECT_EQ(image->data[0], 9U);
  EXPECT_EQ(image->data[3], 6U);
}

TEST(MessageParserFunctionalExtension, HostFallsBackToV1WhenV2IsAbsent) {
  PJ::MessageParserHandle handle(adversarialVtable<emitNoScalars, emitImageV1>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));
  auto record = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>{});
  ASSERT_TRUE(record.has_value()) << record.error();
  EXPECT_EQ(PJ::sdk::typeOf(record->object), PJ::sdk::BuiltinObjectType::kImage);
}

TEST(MessageParserFunctionalExtension, HostV2PathReconstructsEligibleSplices) {
  PJ::MessageParserHandle handle(adversarialV2Vtable<emitSplicedPointCloudV2>());
  ASSERT_TRUE(handle.bindSchema("example/PointCloud", {}));
  const std::array<uint8_t, 4> payload{10, 20, 30, 40};
  auto record = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>(payload));
  ASSERT_TRUE(record.has_value()) << record.error();
  EXPECT_EQ(record->ts, 88);
  const auto* cloud = record->object.get<PJ::sdk::PointCloud>();
  ASSERT_NE(cloud, nullptr);
  ASSERT_EQ(cloud->data.size(), 2U);
  EXPECT_EQ(cloud->data[0], 20U);
  EXPECT_EQ(cloud->data[1], 30U);
}

TEST(MessageParserFunctionalExtension, HostV2PathReconstructsGridMapSplice) {
  PJ::MessageParserHandle handle(adversarialV2Vtable<emitSplicedGridMapV2, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP>());
  ASSERT_TRUE(handle.bindSchema("example/GridMap", {}));
  const std::array<uint8_t, 4> payload{10, 20, 30, 40};
  auto record = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>(payload));
  ASSERT_TRUE(record.has_value()) << record.error();
  EXPECT_EQ(record->ts, 89);
  const auto* grid = record->object.get<PJ::sdk::GridMap>();
  ASSERT_NE(grid, nullptr);
  ASSERT_EQ(grid->data.size(), 2U);
  EXPECT_EQ(grid->data[0], 20U);
  EXPECT_EQ(grid->data[1], 30U);
  EXPECT_TRUE(PJ::validateGridMap(*grid).has_value());
}

TEST(MessageParserFunctionalExtension, HostV2PathRejectsSplicedGridMapWhoseBytesDoNotCoverTheCells) {
  PJ::MessageParserHandle handle(adversarialV2Vtable<emitSplicedGridMapTooShortV2, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP>());
  ASSERT_TRUE(handle.bindSchema("example/GridMap", {}));
  const std::array<uint8_t, 4> payload{10, 20, 30, 40};
  auto record = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>(payload));
  ASSERT_FALSE(record.has_value());
  EXPECT_NE(record.error().find("GridMap"), std::string::npos) << record.error();
}

TEST(MessageParserFunctionalExtension, HostRejectsObjectTypeThatDiffersFromBindingClassification) {
  PJ::MessageParserHandle handle(adversarialV2Vtable<emitMismatchedImageV2>());
  ASSERT_TRUE(handle.bindSchema("example/PointCloud", {}));
  auto record = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>{});
  ASSERT_FALSE(record.has_value());
  EXPECT_NE(record.error().find("differs from the bound classification"), std::string::npos);
}

TEST(MessageParserFunctionalExtension, PluginExceptionsNeverCrossEitherFunctionalRoute) {
  PJ::MessageParserHandle handle(parserVtable<ThrowingFunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));

  bool scalar_sink_called = false;
  const auto scalar = handle.parseScalarsFunctional(
      0, {}, [&](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) -> PJ::Status {
        scalar_sink_called = true;
        return PJ::okStatus();
      });
  EXPECT_FALSE(scalar);
  EXPECT_NE(scalar.error().find("scalar explosion"), std::string::npos);
  EXPECT_FALSE(scalar_sink_called);

  const auto object = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>{});
  EXPECT_FALSE(object);
  EXPECT_NE(object.error().find("object explosion"), std::string::npos);
}

TEST(MessageParserFunctionalExtension, ExistingPluginDefinedExtensionsRemainVisible) {
  PJ::MessageParserHandle handle(parserVtable<CustomExtensionParser>());

  EXPECT_FALSE(handle.supportsFunctionalParsing());
  EXPECT_EQ(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2), nullptr);
  const auto* marker = static_cast<const int*>(handle.getPluginExtension("example.custom.v1"));
  ASSERT_NE(marker, nullptr);
  EXPECT_EQ(*marker, 17);
}

TEST(MessageParserFunctionalExtension, NewlyBuiltLegacyStyleParserDoesNotFalselyAdvertiseFunctionalRoute) {
  PJ::MessageParserHandle handle(parserVtable<CustomExtensionParser>());

  EXPECT_FALSE(handle.supportsFunctionalParsing());
  const auto status = handle.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) { return PJ::okStatus(); });
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find(PJ_PARSER_FUNCTIONAL_EXTENSION_V1), std::string::npos);
}

TEST(MessageParserFunctionalExtension, HandlerRegisteredDuringBindEnablesExtensionWithoutCapabilityCaching) {
  PJ::MessageParserHandle handle(parserVtable<BindRegisteredParser>());

  EXPECT_FALSE(handle.supportsFunctionalParsing());
  ASSERT_TRUE(handle.bindSchema("dynamic/Type", {}));
  EXPECT_TRUE(handle.supportsFunctionalParsing());
}

TEST(MessageParserFunctionalExtension, MixedModelParserAdvertisesOnlyForHandledSchemas) {
  PJ::MessageParserHandle handled(parserVtable<MixedModelParser>());
  ASSERT_TRUE(handled.bindSchema(kSchema, {}));
  EXPECT_TRUE(handled.supportsFunctionalParsing());

  // The same plugin bound to a schema it only implements through legacy
  // parse() must not claim the functional route, or the host would take a
  // route that parseScalars/parseObject can only reject.
  PJ::MessageParserHandle unhandled(parserVtable<MixedModelParser>());
  ASSERT_TRUE(unhandled.bindSchema("example/Unhandled", {}));
  EXPECT_FALSE(unhandled.supportsFunctionalParsing());
  const auto status = unhandled.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) { return PJ::okStatus(); });
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find(PJ_PARSER_FUNCTIONAL_EXTENSION_V1), std::string::npos);
}

TEST(MessageParserFunctionalExtension, RebindingToAnUnhandledSchemaWithdrawsTheFunctionalRoute) {
  PJ::MessageParserHandle handle(parserVtable<MixedModelParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));
  ASSERT_TRUE(handle.supportsFunctionalParsing());

  ASSERT_TRUE(handle.bindSchema("example/Unhandled", {}));
  EXPECT_FALSE(handle.supportsFunctionalParsing());
}

TEST(MessageParserFunctionalExtension, SchemaGatingWithdrawsV2AlongsideV1) {
  // v2 shares v1's advertisement gate. A host prefers v2, so a v2 that stayed
  // advertised for a schema only legacy parse() implements would send every
  // message down a route parse_object/parse_scalars can only reject.
  PJ::MessageParserHandle handled(parserVtable<MixedModelParser>());
  ASSERT_TRUE(handled.bindSchema(kSchema, {}));
  EXPECT_NE(handled.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1), nullptr);
  EXPECT_NE(handled.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2), nullptr);

  PJ::MessageParserHandle unhandled(parserVtable<MixedModelParser>());
  ASSERT_TRUE(unhandled.bindSchema("example/Unhandled", {}));
  EXPECT_EQ(unhandled.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1), nullptr);
  EXPECT_EQ(unhandled.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V2), nullptr);
}

TEST(MessageParserFunctionalExtension, LegacyParserWithoutExtensionRemainsDetectable) {
  PJ::MessageParserHandle handle(legacyStyleVtable());

  EXPECT_FALSE(handle.supportsFunctionalParsing());
  const auto status = handle.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) { return PJ::okStatus(); });
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find(PJ_PARSER_FUNCTIONAL_EXTENSION_V1), std::string::npos);
}

TEST(MessageParserFunctionalExtension, HostRejectsSuccessWithoutExactlyOneSinkCall) {
  PJ::MessageParserHandle no_record(adversarialVtable<emitNoScalars, emitNoObject>());
  const auto absent = no_record.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) { return PJ::okStatus(); });
  EXPECT_FALSE(absent);
  EXPECT_NE(absent.error().find("exactly one"), std::string::npos);

  PJ::MessageParserHandle two_records(adversarialVtable<emitTwoScalarRecords, emitNoObject>());
  const auto duplicate = two_records.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) { return PJ::okStatus(); });
  EXPECT_FALSE(duplicate);
  EXPECT_NE(duplicate.error().find("exactly one"), std::string::npos);
}

TEST(MessageParserFunctionalExtension, HostRejectsUnknownCanonicalObjectType) {
  PJ::MessageParserHandle handle(adversarialVtable<emitNoScalars, emitUnknownObject>());
  const auto object = handle.parseObjectFunctional(0, PJ::Span<const uint8_t>{});

  EXPECT_FALSE(object);
  EXPECT_NE(object.error().find("unknown"), std::string::npos);
}

TEST(MessageParserFunctionalExtension, ScalarConsumerFailurePropagatesAcrossTheExtension) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));

  const auto status = handle.parseScalarsFunctional(
      0, {}, [](std::optional<PJ::Timestamp>, PJ::Span<const PJ_named_field_value_t>) -> PJ::Status {
        return PJ::unexpected(std::string("consumer rejected record"));
      });
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("consumer rejected record"), std::string::npos);
}

TEST(MessageParserFunctionalExtension, ProviderRejectsMalformedCallerOwnedSinkBeforeParsing) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));
  const auto* extension =
      static_cast<const PJ_parser_functional_v1_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
  ASSERT_NE(extension, nullptr);

  PJ_parser_scalar_sink_v1_t undersized_sink{};
  undersized_sink.struct_size = offsetof(PJ_parser_scalar_sink_v1_t, accept_record);
  PJ_error_t error{};
  EXPECT_FALSE(extension->parse_scalars(handle.context(), 0, PJ_bytes_view_t{}, &undersized_sink, &error));
  EXPECT_NE(std::string_view(error.message).find("scalar sink"), std::string_view::npos);

  PJ_parser_object_sink_v1_t undersized_object_sink{};
  undersized_object_sink.struct_size = offsetof(PJ_parser_object_sink_v1_t, accept_object);
  error = {};
  EXPECT_FALSE(extension->parse_object(handle.context(), 0, PJ_payload_t{}, &undersized_object_sink, &error));
  EXPECT_NE(std::string_view(error.message).find("object sink"), std::string_view::npos);
}

TEST(MessageParserFunctionalExtension, ObjectPayloadAnchorIsReleasedExactlyOnceAfterSynchronousSerialization) {
  PJ::MessageParserHandle handle(parserVtable<FunctionalParser>());
  ASSERT_TRUE(handle.bindSchema(kSchema, {}));
  const auto* extension =
      static_cast<const PJ_parser_functional_v1_t*>(handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
  ASSERT_NE(extension, nullptr);

  int releases = 0;
  const uint8_t byte = 5;
  PJ_payload_t payload{
      .data = &byte,
      .size = 1,
      .anchor =
          PJ_payload_anchor_t{
              .ctx = &releases,
              .release = [](void* ctx) { ++*static_cast<int*>(ctx); },
          },
  };
  PJ_parser_object_sink_v1_t sink{
      .struct_size = sizeof(PJ_parser_object_sink_v1_t),
      .ctx = nullptr,
      .accept_object = [](void*, bool, int64_t, uint16_t, PJ_bytes_view_t, PJ_error_t*) noexcept { return true; },
  };
  PJ_error_t error{};

  EXPECT_TRUE(extension->parse_object(handle.context(), 0, payload, &sink, &error));
  EXPECT_EQ(releases, 1);

  PJ::MessageParserHandle throwing_handle(parserVtable<ThrowingFunctionalParser>());
  ASSERT_TRUE(throwing_handle.bindSchema(kSchema, {}));
  const auto* throwing_extension = static_cast<const PJ_parser_functional_v1_t*>(
      throwing_handle.getPluginExtension(PJ_PARSER_FUNCTIONAL_EXTENSION_V1));
  ASSERT_NE(throwing_extension, nullptr);
  releases = 0;
  error = {};
  EXPECT_FALSE(throwing_extension->parse_object(throwing_handle.context(), 0, payload, &sink, &error));
  EXPECT_EQ(releases, 1);
}

}  // namespace
