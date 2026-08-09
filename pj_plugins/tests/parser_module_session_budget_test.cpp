// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_session_budget.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace PJ {
namespace {

void expectDecline(
    const ParserModuleAdmissionDecision& decision, ParserModuleBudgetKind budget, std::string_view diagnostic_name) {
  EXPECT_EQ(decision.outcome, ParserModuleAdmissionOutcome::kDecline);
  EXPECT_EQ(decision.exhausted_budget, budget);
  EXPECT_NE(decision.diagnostic.find(diagnostic_name), std::string::npos);
}

TEST(ParserModuleSessionBudget, RejectsEachModuleAdmissionBudgetWithoutMutation) {
  const ParserModuleSessionBudgetLimits limits{
      .maximum_modules = 1,
      .maximum_artifact_bytes = 100,
      .maximum_claims = 2,
      .maximum_active_instances = 2,
      .maximum_linear_memory_bytes = 400,
  };

  {
    ParserModuleSessionBudgetTracker tracker(limits);
    expectDecline(
        tracker.admitModule("oversize", 101, 1, 100), ParserModuleBudgetKind::kArtifactFileSize, "artifact_file_size");
    EXPECT_EQ(tracker.usage().modules, 0U);
  }
  {
    ParserModuleSessionBudgetTracker tracker(limits);
    expectDecline(tracker.admitModule("claims", 100, 3, 100), ParserModuleBudgetKind::kTotalClaims, "total_claims");
    EXPECT_EQ(tracker.usage().claims, 0U);
  }
  {
    ParserModuleSessionBudgetTracker tracker(limits);
    ASSERT_TRUE(tracker.admitModule("first", 100, 2, 100).accepted());
    expectDecline(tracker.admitModule("second", 1, 0, 1), ParserModuleBudgetKind::kModuleCount, "module_count");
    EXPECT_EQ(tracker.usage().modules, 1U);
    EXPECT_EQ(tracker.usage().claims, 2U);
  }
}

TEST(ParserModuleSessionBudget, AppliesInstanceAndDeclaredMemoryBudgetsIndependently) {
  const ParserModuleSessionBudgetLimits limits{
      .maximum_modules = 2,
      .maximum_artifact_bytes = 100,
      .maximum_claims = 4,
      .maximum_active_instances = 2,
      .maximum_linear_memory_bytes = 300,
  };
  ParserModuleSessionBudgetTracker tracker(limits);
  ASSERT_TRUE(tracker.admitModule("small", 100, 1, 100).accepted());
  ASSERT_TRUE(tracker.admitModule("large", 100, 1, 250).accepted());
  ASSERT_TRUE(tracker.admitInstance("large").accepted());

  expectDecline(tracker.admitInstance("small"), ParserModuleBudgetKind::kTotalLinearMemory, "total_linear_memory");
  EXPECT_EQ(tracker.usage().active_instances, 1U);
  EXPECT_TRUE(tracker.releaseInstance("large"));
  ASSERT_TRUE(tracker.admitInstance("small").accepted());
  ASSERT_TRUE(tracker.admitInstance("small").accepted());
  expectDecline(tracker.admitInstance("small"), ParserModuleBudgetKind::kActiveInstances, "active_instances");
}

TEST(ParserModuleSessionBudget, ReleaseRequiresNoLiveInstancesAndRestoresCapacity) {
  ParserModuleSessionBudgetTracker tracker(
      ParserModuleSessionBudgetLimits{
          .maximum_modules = 1,
          .maximum_artifact_bytes = 10,
          .maximum_claims = 1,
          .maximum_active_instances = 1,
          .maximum_linear_memory_bytes = 20,
      });
  ASSERT_TRUE(tracker.admitModule("module", 10, 1, 20).accepted());
  EXPECT_FALSE(tracker.admitModule("module", 10, 1, 20).accepted());
  ASSERT_TRUE(tracker.admitInstance("module").accepted());
  EXPECT_FALSE(tracker.releaseModule("module"));
  EXPECT_TRUE(tracker.releaseInstance("module"));
  EXPECT_TRUE(tracker.releaseModule("module"));
  EXPECT_EQ(tracker.usage().modules, 0U);
  EXPECT_EQ(tracker.usage().claims, 0U);
  EXPECT_EQ(tracker.usage().active_instances, 0U);
  EXPECT_EQ(tracker.usage().declared_linear_memory_bytes, 0U);
}

}  // namespace
}  // namespace PJ
