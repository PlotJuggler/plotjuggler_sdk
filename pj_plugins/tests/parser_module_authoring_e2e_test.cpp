// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/native_parser_module.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"
#include "pj_plugins/host/parser_module_runtime.hpp"

namespace PJ {
namespace {

constexpr std::string_view kSchema = "uint32 width\nstring frame_id\nuint8[] data\n";

Span<const uint8_t> bytes(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

void appendU32(std::vector<uint8_t>& output, uint32_t value) {
  const size_t relative = output.size() - 4;
  output.insert(output.end(), (4 - (relative % 4)) % 4, 0);
  for (size_t index = 0; index < 4; ++index) {
    output.push_back(static_cast<uint8_t>(value >> (index * 8U)));
  }
}

std::vector<uint8_t> toyPayload() {
  std::vector<uint8_t> output{0, 1, 0, 0};
  appendU32(output, 2);
  appendU32(output, 4);
  output.insert(output.end(), {'m', 'a', 'p', 0});
  appendU32(output, 8);
  output.insert(output.end(), {1, 2, 3, 4, 5, 6, 7, 8});
  return output;
}

parser_module::BindingInfoV1 binding(uint32_t claim_index, std::string_view schema) {
  return parser_module::BindingInfoV1{
      .route = parser_module::Route::kObject,
      .claim_index = claim_index,
      .expected_object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
      .encoding = bytes("ros2msg"),
      .type_name = bytes(claim_index == 0 ? "toy_msgs/msg/Cloud" : "toy_msgs/msg/CloudSplice"),
      .schema = bytes(schema),
      .claim_id = bytes(claim_index == 0 ? "full-wire" : "spliced"),
      .config_json = bytes("{}"),
      .schema_digest = {},
  };
}

TEST(ParserModuleAuthoringE2E, LoadsAdmitsBindsAndParsesFullAndSplicedPointCloud) {
  auto module = NativeParserModule::load(PJ_TOY_CDR_POINTCLOUD_MODULE_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  ParserClaimCatalog catalog;
  auto manifest = catalog.ingestModuleManifest(module->manifestJson(), ParserClaimProvenance::kFolderDrop, 12);
  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  ASSERT_EQ(manifest->claims.size(), 2U);

  const auto payload = toyPayload();
  const parser_module::ParseInputV1 input{
      .has_timestamp = true,
      .timestamp_ns = 4242,
      .payload = payload,
  };

  auto full = NativeParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(full.has_value()) << full.error();
  auto full_bind = full->bind(binding(0, kSchema));
  ASSERT_TRUE(full_bind.has_value()) << full_bind.error();
  ASSERT_EQ(full_bind->outcome, ParserModuleBindOutcome::kAccept);
  auto full_result = full->parse(input);
  ASSERT_TRUE(full_result.has_value()) << full_result.error();
  ASSERT_EQ(full_result->fault, ParserModuleFaultKind::kNone) << full_result->message;
  const auto* full_object = std::get_if<ParserModuleObjectOutput>(&*full_result->output);
  ASSERT_NE(full_object, nullptr);
  EXPECT_FALSE(full_object->splice.has_value());
  const auto* full_cloud = std::any_cast<sdk::PointCloud>(&full_object->object);
  ASSERT_NE(full_cloud, nullptr);
  EXPECT_EQ(full_cloud->width, 2U);
  EXPECT_EQ(full_cloud->frame_id, "map");
  EXPECT_EQ(full_cloud->data.size(), 8U);
  EXPECT_EQ(full_cloud->data[7], 8U);
  EXPECT_EQ(full_cloud->timestamp_ns, 4242);

  auto spliced = NativeParserModuleInstance::create(*module, 1);
  ASSERT_TRUE(spliced.has_value()) << spliced.error();
  auto splice_bind = spliced->bind(binding(1, kSchema));
  ASSERT_TRUE(splice_bind.has_value()) << splice_bind.error();
  ASSERT_EQ(splice_bind->outcome, ParserModuleBindOutcome::kAccept);
  auto splice_result = spliced->parse(input);
  ASSERT_TRUE(splice_result.has_value()) << splice_result.error();
  ASSERT_EQ(splice_result->fault, ParserModuleFaultKind::kNone) << splice_result->message;
  const auto* splice_object = std::get_if<ParserModuleObjectOutput>(&*splice_result->output);
  ASSERT_NE(splice_object, nullptr);
  ASSERT_TRUE(splice_object->splice.has_value());
  EXPECT_EQ(splice_object->splice->field_number, 9U);
  EXPECT_EQ(splice_object->splice->input_offset, 20U);
  EXPECT_EQ(splice_object->splice->payload_bytes, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
  const auto* splice_cloud = std::any_cast<sdk::PointCloud>(&splice_object->object);
  ASSERT_NE(splice_cloud, nullptr);
  EXPECT_EQ(splice_cloud->timestamp_ns, 4242);
  ASSERT_EQ(splice_cloud->data.size(), 8U);
  EXPECT_EQ(splice_cloud->data[0], 1U);
  EXPECT_EQ(splice_cloud->data[7], 8U);
}

TEST(ParserModuleAuthoringE2E, DeclinesUnsupportedSchemaRevisionAtBind) {
  auto module = NativeParserModule::load(PJ_TOY_CDR_POINTCLOUD_MODULE_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = NativeParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error();
  auto result = instance->bind(binding(0, "uint32 width\nstring frame_id\n"));
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->outcome, ParserModuleBindOutcome::kDecline);
  EXPECT_NE(result->message.find("unsupported toy schema revision"), std::string::npos);
}

}  // namespace
}  // namespace PJ
