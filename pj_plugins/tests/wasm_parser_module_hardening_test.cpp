// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <any>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_plugins/host/parser_module_runtime.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"
#include "pj_plugins/host/wasm_parser_module.hpp"
#include "pj_plugins/host/wasm_parser_module_runtime.hpp"

namespace PJ {
namespace {

const ParserModuleClaimKey kClaim{"org.plotjuggler.test.adversarial-wasm", "adversarial"};

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

WasmParserModuleLoadOptions budgeted(std::shared_ptr<ParserModuleSessionBudgetTracker> budget) {
  WasmParserModuleLoadOptions options;
  options.budget = std::move(budget);
  return options;
}

ParserModuleParseResult parseBehavior(WasmParserModuleInstance& instance, uint8_t behavior) {
  const std::array<uint8_t, 1> payload{behavior};
  auto result = instance.parse(parser_module::ParseInputV1{.payload = payload});
  EXPECT_TRUE(result.has_value()) << result.error();
  return result ? std::move(*result) : ParserModuleParseResult{};
}

void expectPointCloud42(const ParserModuleParseResult& result) {
  ASSERT_EQ(result.fault, ParserModuleFaultKind::kNone) << result.message;
  const auto* object = std::get_if<ParserModuleObjectOutput>(&*result.output);
  ASSERT_NE(object, nullptr);
  const auto* cloud = std::any_cast<sdk::PointCloud>(&object->object);
  ASSERT_NE(cloud, nullptr);
  ASSERT_EQ(cloud->data.size(), 1U);
  EXPECT_EQ(cloud->data[0], 42U);
}

TEST(WasmParserModuleHardening, EnforcesArtifactAndDeclaredMemoryAdmissionCaps) {
  const uint64_t file_size = std::filesystem::file_size(PJ_ADVERSARIAL_WASM_PATH);
  std::vector<Diagnostic> diagnostics;
  WasmParserModuleLoadOptions options;
  options.sink = [&](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); };

  options.limits.maximum_artifact_bytes = file_size - 1;
  auto oversized = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, options);
  ASSERT_FALSE(oversized.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(oversized.error().find("artifact_file_size budget exhausted"), std::string::npos);

  options.limits = WasmParserModuleLimits{};
  options.limits.maximum_linear_memory_bytes = UINT64_C(128) * 1024U * 1024U;
  diagnostics.clear();
  auto memory_bomb = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, options);
  ASSERT_FALSE(memory_bomb.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(memory_bomb.error().find("exceeds configured cap"), std::string::npos);

  options.limits = WasmParserModuleLimits{};
  options.limits.maximum_table_elements = 0;
  diagnostics.clear();
  auto table_bomb = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, options);
  ASSERT_FALSE(table_bomb.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(table_bomb.error().find("table maximum"), std::string::npos);
}

TEST(WasmParserModuleHardening, EnforcesAggregateBudgetsAtActualAdmissionBoundaries) {
  const uint64_t file_size = std::filesystem::file_size(PJ_ADVERSARIAL_WASM_PATH);
  const auto load_with = [](ParserModuleSessionBudgetLimits limits) {
    auto budget = std::make_shared<ParserModuleSessionBudgetTracker>(limits);
    auto loaded = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, budgeted(budget));
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

  // Reservations are released by the wrappers themselves, module last.
  limits = {};
  limits.maximum_modules = 1;
  limits.maximum_active_instances = 1;
  auto [release_budget, released_load] = load_with(limits);
  ASSERT_TRUE(released_load.has_value()) << released_load.error();
  std::optional<WasmParserModule> released_module(std::move(*released_load));
  {
    auto instance = createBound(*released_module);
    ASSERT_TRUE(instance.has_value()) << instance.error();
    EXPECT_EQ(release_budget->usage().active_instances, 1U);
    released_module.reset();
    EXPECT_EQ(release_budget->usage().modules, 1U) << "module reservation outlives its instances";
  }
  EXPECT_EQ(release_budget->usage().modules, 0U);
  EXPECT_EQ(release_budget->usage().active_instances, 0U);
  EXPECT_EQ(release_budget->usage().declared_linear_memory_bytes, 0U);
}

TEST(WasmParserModuleHardening, MetersInfiniteLoopAsDistinctContractViolation) {
  WasmParserModuleLoadOptions options;
  options.limits.metering_points_per_call = UINT64_C(1000000);
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, options);
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

TEST(WasmParserModuleHardening, HostDrivenQuarantineReplaysBindingThenDisablesOnRepeat) {
  // The wasm wrapper only classifies; this is the host loop, identical to the
  // native one: record faults, replay create/bind on quarantine, mark the
  // recreation, and stop creating instances once the claim is disabled.
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  ParserModuleStrikeTracker tracker;
  auto instance = createBound(*module);
  ASSERT_TRUE(instance.has_value()) << instance.error();

  for (uint8_t strike = 1; strike <= 3; ++strike) {
    const ParserModuleParseResult result = parseBehavior(*instance, 0);
    ASSERT_EQ(result.fault, ParserModuleFaultKind::kContractViolation);
    EXPECT_NE(result.message.find("wasm trap"), std::string::npos);
    EXPECT_EQ(instance->lifecycleDiagnostic(), result.message);
    const ParserModuleStrikeState state = tracker.recordFault(kClaim, result.fault);
    EXPECT_EQ(state.health, strike < 3 ? ParserModuleClaimHealth::kActive : ParserModuleClaimHealth::kQuarantined);
  }
  EXPECT_EQ(tracker.state(kClaim).quarantine_count, 1U);

  instance = createBound(*module);
  ASSERT_TRUE(instance.has_value()) << instance.error();
  ASSERT_TRUE(tracker.markRecreated(kClaim));
  expectPointCloud42(parseBehavior(*instance, 3));

  for (uint8_t strike = 0; strike < 3; ++strike) {
    const ParserModuleParseResult result = parseBehavior(*instance, 0);
    ASSERT_EQ(result.fault, ParserModuleFaultKind::kContractViolation);
    (void)tracker.recordFault(kClaim, result.fault);
  }
  const ParserModuleStrikeState disabled = tracker.state(kClaim);
  EXPECT_EQ(disabled.health, ParserModuleClaimHealth::kDisabled);
  EXPECT_EQ(disabled.quarantine_count, 2U);
  EXPECT_FALSE(tracker.markRecreated(kClaim));
}

TEST(WasmParserModuleHardening, IndependentInstancesRunConcurrentlyUnderOneTracker) {
  // Scalar and object routes of one claim run on different threads in the
  // host; the stores are independent and the shared tracker is synchronized.
  auto budget = std::make_shared<ParserModuleSessionBudgetTracker>();
  auto module = WasmParserModule::load(PJ_ADVERSARIAL_WASM_PATH, budgeted(budget));
  ASSERT_TRUE(module.has_value()) << module.error();
  ParserModuleStrikeTracker tracker;

  std::vector<std::thread> threads;
  for (int worker = 0; worker < 4; ++worker) {
    threads.emplace_back([&] {
      auto instance = createBound(*module);
      ASSERT_TRUE(instance.has_value()) << instance.error();
      for (int round = 0; round < 25; ++round) {
        expectPointCloud42(parseBehavior(*instance, 3));
        const ParserModuleParseResult trap = parseBehavior(*instance, 2);  // data error, never a strike
        EXPECT_EQ(trap.fault, ParserModuleFaultKind::kDataError);
        (void)tracker.recordFault(kClaim, trap.fault);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(tracker.state(kClaim).strikes, 0U);
  EXPECT_EQ(tracker.state(kClaim).health, ParserModuleClaimHealth::kActive);
  EXPECT_EQ(budget->usage().active_instances, 0U);
  EXPECT_EQ(budget->usage().modules, 1U);
}

}  // namespace
}  // namespace PJ
