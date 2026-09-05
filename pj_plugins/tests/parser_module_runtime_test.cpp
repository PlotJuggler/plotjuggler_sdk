// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "native_parser_module_fixture.hpp"
#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/span.hpp"

namespace PJ {
namespace {

using enum pj_fixture::ClaimIndex;

Span<const uint8_t> bytes(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

parser_module::BindingInfoV1 binding(
    uint32_t claim_index, parser_module::Route route, uint16_t object_type = PJ_BUILTIN_OBJECT_TYPE_NONE) {
  return parser_module::BindingInfoV1{
      .route = route,
      .claim_index = claim_index,
      .expected_object_type = object_type,
      .encoding = bytes("protobuf"),
      .type_name = bytes("fixture.Message"),
      .schema = {},
      .claim_id = bytes("fixture-claim"),
      .config_json = bytes("{}"),
      .schema_digest = bytes("sha256:test"),
  };
}

parser_module::ParseInputV1 input() {
  static constexpr std::array<uint8_t, 4> kPayload{10, 20, 30, 40};
  return parser_module::ParseInputV1{
      .has_timestamp = true,
      .timestamp_ns = 100,
      .payload = kPayload,
  };
}

NativeParserModule loadFixture() {
  auto module = NativeParserModule::load(PJ_NATIVE_MODULE_FIXTURE_PATH);
  EXPECT_TRUE(module.has_value()) << module.error();
  return module ? std::move(*module) : NativeParserModule{};
}

NativeParserModuleInstance createBound(
    const NativeParserModule& module, uint32_t claim_index, parser_module::Route route,
    uint16_t object_type = PJ_BUILTIN_OBJECT_TYPE_NONE) {
  auto instance = NativeParserModuleInstance::create(module, claim_index);
  EXPECT_TRUE(instance.has_value()) << instance.error();
  if (!instance) {
    return {};
  }
  auto result = instance->bind(binding(claim_index, route, object_type));
  EXPECT_TRUE(result.has_value()) << result.error();
  if (result) {
    EXPECT_EQ(result->outcome, ParserModuleBindOutcome::kAccept);
    EXPECT_EQ(result->fault, ParserModuleFaultKind::kNone);
  }
  return std::move(*instance);
}

TEST(ParserModuleRuntime, RunsObjectAndScalarLifecyclesAndOwnsOutputs) {
  auto module = loadFixture();
  ASSERT_TRUE(module.valid());

  auto object_instance = createBound(module, kObject, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  auto object_result = object_instance.parse(input());
  ASSERT_TRUE(object_result.has_value()) << object_result.error();
  ASSERT_EQ(object_result->fault, ParserModuleFaultKind::kNone);
  ASSERT_TRUE(object_result->output.has_value());
  const auto* object = std::get_if<ParserModuleObjectOutput>(&*object_result->output);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(sdk::typeOf(object->object), sdk::BuiltinObjectType::kPointCloud);
  EXPECT_FALSE(object->wire.empty());
  EXPECT_FALSE(object->splice.has_value());
  const auto* cloud = object->object.get<sdk::PointCloud>();
  ASSERT_NE(cloud, nullptr);
  EXPECT_EQ(cloud->data.size(), 4U);
  EXPECT_EQ(cloud->data[2], 3U);

  auto scalar_instance = createBound(module, kScalar, parser_module::Route::kScalar);
  auto scalar_result = scalar_instance.parse(input());
  ASSERT_TRUE(scalar_result.has_value()) << scalar_result.error();
  ASSERT_EQ(scalar_result->fault, ParserModuleFaultKind::kNone);
  ASSERT_TRUE(scalar_result->output.has_value());
  const auto* scalar = std::get_if<ParserModuleScalarOutput>(&*scalar_result->output);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(scalar->has_timestamp);
  EXPECT_EQ(scalar->timestamp_ns, 42);
  ASSERT_EQ(scalar->fields.size(), 2U);
  EXPECT_EQ(scalar->fields[0].name, "temperature");
  EXPECT_DOUBLE_EQ(std::get<double>(scalar->fields[0].value), 21.5);
  EXPECT_EQ(std::get<std::string>(scalar->fields[1].value), "ready");
}

TEST(ParserModuleRuntime, SurfacesBindDeclineAndTokenZeroCreationError) {
  auto module = loadFixture();

  auto declined = NativeParserModuleInstance::create(module, kDecline);
  ASSERT_TRUE(declined.has_value()) << declined.error();
  auto bind_result = declined->bind(binding(kDecline, parser_module::Route::kScalar));
  ASSERT_TRUE(bind_result.has_value()) << bind_result.error();
  EXPECT_EQ(bind_result->outcome, ParserModuleBindOutcome::kDecline);
  EXPECT_EQ(bind_result->fault, ParserModuleFaultKind::kNone);
  EXPECT_EQ(bind_result->message, "fixture bind declined");
  EXPECT_FALSE(declined->parse(input()).has_value());

  auto failed = NativeParserModuleInstance::create(module, kCreateFailure);
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), "fixture creation failure");
}

TEST(ParserModuleRuntime, ClassifiesDataErrorsSeparatelyFromContractViolations) {
  auto module = loadFixture();

  auto data_instance = createBound(module, kDataError, parser_module::Route::kScalar);
  auto data_result = data_instance.parse(input());
  ASSERT_TRUE(data_result.has_value()) << data_result.error();
  EXPECT_EQ(data_result->fault, ParserModuleFaultKind::kDataError);
  EXPECT_EQ(data_result->result_code, PJ_MODULE_ERR_MALFORMED_INPUT);
  EXPECT_EQ(data_result->message, "fixture payload decode error");

  for (const uint32_t claim_index : {kMalformed, kBadToken, kRouteMismatch}) {
    auto instance = createBound(module, claim_index, parser_module::Route::kScalar);
    auto result = instance.parse(input());
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result->fault, ParserModuleFaultKind::kContractViolation) << claim_index;
  }

  auto mismatch = createBound(module, kTypeMismatch, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  auto mismatch_result = mismatch.parse(input());
  ASSERT_TRUE(mismatch_result.has_value()) << mismatch_result.error();
  EXPECT_EQ(mismatch_result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(mismatch_result->message.find("does not match"), std::string::npos);
}

TEST(ParserModuleRuntime, AcceptsEligibleSpliceAndRejectsInvalidReferences) {
  auto module = loadFixture();

  auto valid = createBound(module, kSplice, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  auto valid_result = valid.parse(input());
  ASSERT_TRUE(valid_result.has_value()) << valid_result.error();
  ASSERT_EQ(valid_result->fault, ParserModuleFaultKind::kNone);
  const auto* valid_object = std::get_if<ParserModuleObjectOutput>(&*valid_result->output);
  ASSERT_NE(valid_object, nullptr);
  ASSERT_TRUE(valid_object->splice.has_value());
  EXPECT_EQ(valid_object->splice->field_number, 9U);
  EXPECT_EQ(valid_object->splice->input_offset, 1U);
  EXPECT_EQ(valid_object->splice->payload_bytes, (std::vector<uint8_t>{20, 30}));
  const auto* valid_cloud = valid_object->object.get<sdk::PointCloud>();
  ASSERT_NE(valid_cloud, nullptr);
  ASSERT_EQ(valid_cloud->data.size(), 2U);
  EXPECT_EQ(valid_cloud->data[0], 20U);
  EXPECT_EQ(valid_cloud->data[1], 30U);

  auto out_of_bounds =
      createBound(module, kSpliceOutOfBounds, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  auto out_of_bounds_result = out_of_bounds.parse(input());
  ASSERT_TRUE(out_of_bounds_result.has_value()) << out_of_bounds_result.error();
  EXPECT_EQ(out_of_bounds_result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(out_of_bounds_result->message.find("outside"), std::string::npos);

  auto ineligible =
      createBound(module, kSpliceIneligible, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  auto ineligible_result = ineligible.parse(input());
  ASSERT_TRUE(ineligible_result.has_value()) << ineligible_result.error();
  EXPECT_EQ(ineligible_result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(ineligible_result->message.find("not eligible"), std::string::npos);
}

TEST(ParserModuleRuntime, AttachesGridMapSpliceToTheDecodedHeader) {
  auto module = loadFixture();
  auto bound = createBound(module, kSpliceGridMap, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP);
  auto result = bound.parse(input());
  ASSERT_TRUE(result.has_value()) << result.error();
  ASSERT_EQ(result->fault, ParserModuleFaultKind::kNone) << result->message;
  const auto* object = std::get_if<ParserModuleObjectOutput>(&*result->output);
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(object->splice.has_value());
  EXPECT_EQ(object->splice->field_number, 10U);
  const auto* grid = object->object.get<sdk::GridMap>();
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->column_count, 2U);
  ASSERT_EQ(grid->data.size(), 2U);
  EXPECT_EQ(grid->data[0], 20U);
  EXPECT_EQ(grid->data[1], 30U);
  EXPECT_TRUE(validateGridMap(*grid).has_value());
}

TEST(ParserModuleRuntime, RejectsGridMapSpliceWhoseBytesDoNotCoverTheCells) {
  auto module = loadFixture();
  auto bound = createBound(module, kSpliceGridMapShort, parser_module::Route::kObject, PJ_BUILTIN_OBJECT_TYPE_GRID_MAP);
  auto result = bound.parse(input());
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(result->message.find("GridMap"), std::string::npos) << result->message;
}

TEST(ParserModuleRuntime, StrikeTrackerQuarantinesReplaysAndThenDisables) {
  auto module = loadFixture();
  const ParserModuleClaimKey key{"org.plotjuggler.test.native-module", "malformed"};
  ParserModuleStrikeTracker tracker;

  auto first = createBound(module, kMalformed, parser_module::Route::kScalar);
  for (int strike = 1; strike <= 3; ++strike) {
    auto result = first.parse(input());
    ASSERT_TRUE(result.has_value()) << result.error();
    const auto state = tracker.recordFault(key, result->fault);
    EXPECT_EQ(state.health, strike == 3 ? ParserModuleClaimHealth::kQuarantined : ParserModuleClaimHealth::kActive);
  }
  EXPECT_EQ(tracker.state(key).quarantine_count, 1U);

  first = {};
  auto replay = createBound(module, kMalformed, parser_module::Route::kScalar);
  ASSERT_TRUE(replay.valid());
  ASSERT_TRUE(tracker.markRecreated(key));
  EXPECT_EQ(tracker.state(key).health, ParserModuleClaimHealth::kActive);

  EXPECT_EQ(tracker.recordFault(key, ParserModuleFaultKind::kDataError).health, ParserModuleClaimHealth::kActive);
  EXPECT_EQ(tracker.state(key).strikes, 0U);
  for (int strike = 1; strike <= 3; ++strike) {
    auto result = replay.parse(input());
    ASSERT_TRUE(result.has_value()) << result.error();
    const auto state = tracker.recordFault(key, result->fault);
    EXPECT_EQ(state.health, strike == 3 ? ParserModuleClaimHealth::kDisabled : ParserModuleClaimHealth::kActive);
  }
  EXPECT_FALSE(tracker.markRecreated(key));
  EXPECT_EQ(tracker.state(key).health, ParserModuleClaimHealth::kDisabled);
  EXPECT_EQ(tracker.state(key).quarantine_count, 2U);
}

}  // namespace
}  // namespace PJ
