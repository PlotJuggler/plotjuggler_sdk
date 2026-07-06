// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// describe_schema_columns tail slot: SDK-side ColumnSpec -> wire-JSON
// serialization (incl. path escaping), the base-class "unsupported" default,
// host-side MessageParserHandle gating, and the type-name round-trip helpers.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "pj_plugins/host/message_parser_handle.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

constexpr char kManifest[] =
    R"({"id":"mock-manifest-parser","name":"Mock Manifest Parser","version":"1.0.0","encoding":["mock"]})";

class ManifestParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Expected<std::vector<PJ::sdk::ColumnSpec>> describeSchemaColumns(
      std::string_view type_name, PJ::Span<const uint8_t> schema) const override {
    (void)schema;
    if (type_name != "mock/Type") {
      return PJ::unexpected(std::string("unknown type: ") + std::string(type_name));
    }
    return std::vector<PJ::sdk::ColumnSpec>{
        {"pose.position.x", PJ::PrimitiveType::kFloat64},
        {"status", PJ::PrimitiveType::kString},
        {"count", PJ::PrimitiveType::kUint32},
        {R"(weird"name)", PJ::PrimitiveType::kBool},
    };
  }
};

class DefaultParser : public PJ::MessageParserPluginBase {};

template <typename Plugin>
const PJ_message_parser_vtable_t* vtableFor() {
  return PJ::MessageParserPluginBase::vtableWithCreate(
      []() noexcept -> void* {
        try {
          return new Plugin();
        } catch (...) {
          return nullptr;
        }
      },
      kManifest);
}

TEST(DescribeSchemaColumnsTest, SerializesSpecsToWireJsonInOrder) {
  PJ::MessageParserHandle handle(vtableFor<ManifestParser>());
  ASSERT_TRUE(handle.valid());

  std::string json;
  auto status = handle.describeSchemaColumns("mock/Type", {}, json);
  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(
      json, R"([{"path":"pose.position.x","type":"float64"},{"path":"status","type":"string"},)"
            R"({"path":"count","type":"uint32"},{"path":"weird\"name","type":"bool"}])");
}

TEST(DescribeSchemaColumnsTest, PluginErrorPropagates) {
  PJ::MessageParserHandle handle(vtableFor<ManifestParser>());
  std::string json = "untouched";
  auto status = handle.describeSchemaColumns("other/Type", {}, json);
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("unknown type: other/Type"), std::string::npos);
  EXPECT_EQ(json, "untouched");
}

TEST(DescribeSchemaColumnsTest, BaseDefaultIsUnsupported) {
  PJ::MessageParserHandle handle(vtableFor<DefaultParser>());
  std::string json;
  auto status = handle.describeSchemaColumns("mock/Type", {}, json);
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("does not support column manifests"), std::string::npos);
}

TEST(DescribeSchemaColumnsTest, OldPluginVtableIsGated) {
  // A plugin compiled before the tail slot reports a smaller struct_size.
  static PJ_message_parser_vtable_t old_vt = *vtableFor<ManifestParser>();
  old_vt.struct_size = offsetof(PJ_message_parser_vtable_t, describe_schema_columns);
  PJ::MessageParserHandle handle(&old_vt);
  std::string json;
  auto status = handle.describeSchemaColumns("mock/Type", {}, json);
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("does not expose describe_schema_columns"), std::string::npos);
}

TEST(DescribeSchemaColumnsTest, PrimitiveTypeNamesRoundTrip) {
  for (const auto type :
       {PJ::PrimitiveType::kFloat32, PJ::PrimitiveType::kFloat64, PJ::PrimitiveType::kInt8, PJ::PrimitiveType::kInt16,
        PJ::PrimitiveType::kInt32, PJ::PrimitiveType::kInt64, PJ::PrimitiveType::kUint8, PJ::PrimitiveType::kUint16,
        PJ::PrimitiveType::kUint32, PJ::PrimitiveType::kUint64, PJ::PrimitiveType::kBool, PJ::PrimitiveType::kString}) {
    const auto name = PJ::sdk::primitiveTypeJsonName(type);
    const auto parsed = PJ::sdk::primitiveTypeFromJsonName(name);
    ASSERT_TRUE(parsed.has_value()) << name;
    EXPECT_EQ(*parsed, type) << name;
  }
  EXPECT_FALSE(PJ::sdk::primitiveTypeFromJsonName("double").has_value());
  EXPECT_EQ(PJ::sdk::primitiveTypeJsonName(PJ::PrimitiveType::kUnspecified), "float64");
}

}  // namespace
