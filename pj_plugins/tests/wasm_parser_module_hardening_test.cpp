// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <any>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_plugins/host/parser_module_runtime.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"
#include "pj_plugins/host/wasm_parser_module.hpp"
#include "pj_plugins/host/wasm_parser_module_runtime.hpp"

namespace PJ {
namespace {

Span<const uint8_t> bytes(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

parser_module::BindingInfoV1 binding() {
  return parser_module::BindingInfoV1{
      .route = parser_module::Route::kObject,
      .claim_index = 0,
      .expected_object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
      .encoding = bytes("ros2msg"),
      .type_name = bytes("test_msgs/msg/Adversarial"),
      .schema = bytes("uint8 behavior\n"),
      .claim_id = bytes("adversarial"),
      .config_json = bytes("{}"),
      .schema_digest = {},
  };
}

Expected<WasmParserModuleInstance> createBound(const WasmParserModule& module) {
  auto instance = WasmParserModuleInstance::create(module, 0);
  if (!instance) {
    return unexpected(instance.error().message);
  }
  auto result = instance->bind(binding());
  if (!result) {
    return unexpected(result.error());
  }
  if (result->outcome != ParserModuleBindOutcome::kAccept) {
    return unexpected("adversarial fixture bind was not accepted: " + result->message);
  }
  return std::move(*instance);
}

ParserModuleParseResult parseBehavior(WasmParserModuleInstance& instance, uint8_t behavior) {
  const std::array<uint8_t, 1> payload{behavior};
  auto result = instance.parse(parser_module::ParseInputV1{.payload = payload});
  EXPECT_TRUE(result.has_value()) << result.error();
  return result ? std::move(*result) : ParserModuleParseResult{};
}

TEST(WasmParserModuleHardening, EnforcesArtifactAndDeclaredMemoryAdmissionCaps) {
  const uint64_t file_size = std::filesystem::file_size(PJ_ADVERSARIAL_WASM_PATH);
  WasmParserModuleLimits limits;
  limits.maximum_artifact_bytes = file_size - 1;
  std::vector<Diagnostic> diagnostics;
  auto oversized = WasmParserModule::load(
      PJ_ADVERSARIAL_WASM_PATH, limits, [&](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
  ASSERT_FALSE(oversized.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(oversized.error().find("artifact_file_size budget exhausted"), std::string::npos);

  limits = WasmParserModuleLimits{};
  limits.maximum_linear_memory_bytes = UINT64_C(128) * 1024U * 1024U;
  diagnostics.clear();
  auto memory_bomb = WasmParserModule::load(
      PJ_ADVERSARIAL_WASM_PATH, limits, [&](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
  ASSERT_FALSE(memory_bomb.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(memory_bomb.error().find("exceeds configured cap"), std::string::npos);
}

TEST(WasmParserModuleHardening, EnforcesAggregateBudgetsAtActualAdmissionBoundaries) {
  const uint64_t file_size = std::filesystem::file_size(PJ_ADVERSARIAL_WASM_PATH);
  const auto load_with = [](ParserModuleSessionBudgetLimits limits) {
    auto budget = std::make_shared<ParserModuleSessionBudgetTracker>(limits);
    auto loaded = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, budget);
    return std::pair(std::move(budget), std::move(loaded));
  };

  ParserModuleSessionBudgetLimits limits;
  limits.maximum_modules = 0;
  auto [module_budget, module_decline] = load_with(limits);
  ASSERT_FALSE(module_decline.has_value());
  EXPECT_NE(module_decline.error().find("module_count"), std::string::npos);
  EXPECT_EQ(module_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_artifact_bytes = file_size - 1;
  auto [artifact_budget, artifact_decline] = load_with(limits);
  ASSERT_FALSE(artifact_decline.has_value());
  EXPECT_NE(artifact_decline.error().find("artifact_file_size"), std::string::npos);
  EXPECT_EQ(artifact_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_claims = 0;
  auto [claim_budget, claim_decline] = load_with(limits);
  ASSERT_FALSE(claim_decline.has_value());
  EXPECT_NE(claim_decline.error().find("total_claims"), std::string::npos);
  EXPECT_EQ(claim_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_active_instances = 0;
  auto [instance_budget, active_module] = load_with(limits);
  ASSERT_TRUE(active_module.has_value()) << active_module.error();
  auto active_decline = WasmParserModuleInstance::create(*active_module, 0);
  ASSERT_FALSE(active_decline.has_value());
  EXPECT_EQ(active_decline.error().outcome, WasmParserModuleCreateOutcome::kAdmissionDecline);
  EXPECT_NE(active_decline.error().message.find("active_instances"), std::string::npos);
  EXPECT_EQ(instance_budget->usage().active_instances, 0U);

  limits = {};
  limits.maximum_linear_memory_bytes = UINT64_C(128) * 1024U * 1024U;
  auto [memory_budget, memory_module] = load_with(limits);
  ASSERT_TRUE(memory_module.has_value()) << memory_module.error();
  auto memory_decline = WasmParserModuleInstance::create(*memory_module, 0);
  ASSERT_FALSE(memory_decline.has_value());
  EXPECT_EQ(memory_decline.error().outcome, WasmParserModuleCreateOutcome::kAdmissionDecline);
  EXPECT_NE(memory_decline.error().message.find("total_linear_memory"), std::string::npos);
  EXPECT_EQ(memory_budget->usage().declared_linear_memory_bytes, 0U);
}

TEST(WasmParserModuleHardening, MetersInfiniteLoopAsDistinctContractViolation) {
  WasmParserModuleLimits limits;
  limits.metering_points_per_call = UINT64_C(1000000);
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, limits);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = createBound(*module);
  ASSERT_TRUE(instance.has_value()) << instance.error();

  const ParserModuleParseResult result = parseBehavior(*instance, 1);
  EXPECT_EQ(result.fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(result.message.find("wasm metering exhausted during pj_module_parse"), std::string::npos);
  EXPECT_NE(result.message.find("instruction-point limit 1000000"), std::string::npos);
}

TEST(WasmParserModuleHardening, EngineRejectsRuntimeGrowthPastDeclaredMaximum) {
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  EXPECT_EQ(module->declaredLinearMemoryMaximum(), UINT64_C(256) * 1024U * 1024U);
  auto instance = createBound(*module);
  ASSERT_TRUE(instance.has_value()) << instance.error();

  const ParserModuleParseResult result = parseBehavior(*instance, 2);
  EXPECT_EQ(result.fault, ParserModuleFaultKind::kDataError);
  EXPECT_NE(result.message.find("memory growth rejected by declared maximum"), std::string::npos);
}

TEST(WasmParserModuleHardening, TrapQuarantineReplaysBindingThenDisablesOnRepeat) {
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = createBound(*module);
  ASSERT_TRUE(instance.has_value()) << instance.error();

  for (uint8_t strike = 1; strike <= 3; ++strike) {
    const ParserModuleParseResult result = parseBehavior(*instance, 0);
    ASSERT_EQ(result.fault, ParserModuleFaultKind::kContractViolation);
    const ParserModuleStrikeState state = module->strikeState(0);
    if (strike < 3) {
      EXPECT_EQ(state.health, ParserModuleClaimHealth::kActive);
      EXPECT_EQ(state.strikes, strike);
    } else {
      EXPECT_EQ(state.health, ParserModuleClaimHealth::kActive);
      EXPECT_EQ(state.strikes, 0U);
      EXPECT_EQ(state.quarantine_count, 1U);
      EXPECT_NE(result.message.find("quarantined and recreated"), std::string::npos);
    }
  }

  const ParserModuleParseResult recovered = parseBehavior(*instance, 3);
  ASSERT_EQ(recovered.fault, ParserModuleFaultKind::kNone) << recovered.message;
  const auto* object = std::get_if<ParserModuleObjectOutput>(&*recovered.output);
  ASSERT_NE(object, nullptr);
  const auto* cloud = std::any_cast<sdk::PointCloud>(&object->object);
  ASSERT_NE(cloud, nullptr);
  ASSERT_EQ(cloud->data.size(), 1U);
  EXPECT_EQ(cloud->data[0], 42U);

  for (uint8_t strike = 0; strike < 3; ++strike) {
    const ParserModuleParseResult result = parseBehavior(*instance, 0);
    ASSERT_EQ(result.fault, ParserModuleFaultKind::kContractViolation);
  }
  const ParserModuleStrikeState disabled = module->strikeState(0);
  EXPECT_EQ(disabled.health, ParserModuleClaimHealth::kDisabled);
  EXPECT_EQ(disabled.quarantine_count, 2U);
  EXPECT_FALSE(instance->valid());
}

}  // namespace
}  // namespace PJ
