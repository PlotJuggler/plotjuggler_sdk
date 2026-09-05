// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_session_budget.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

namespace PJ {
namespace {

void expectDecline(
    const ParserModuleAdmissionDecision& decision, ParserModuleBudgetKind budget, std::string_view diagnostic_name) {
  EXPECT_FALSE(decision.accepted());
  EXPECT_EQ(decision.reservation, 0U);
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
    expectDecline(tracker.admitModule(101, 1, 100), ParserModuleBudgetKind::kArtifactFileSize, "artifact_file_size");
    EXPECT_EQ(tracker.usage().modules, 0U);
  }
  {
    ParserModuleSessionBudgetTracker tracker(limits);
    expectDecline(tracker.admitModule(100, 3, 100), ParserModuleBudgetKind::kTotalClaims, "total_claims");
    EXPECT_EQ(tracker.usage().claims, 0U);
  }
  {
    ParserModuleSessionBudgetTracker tracker(limits);
    ASSERT_TRUE(tracker.admitModule(100, 2, 100).accepted());
    expectDecline(tracker.admitModule(1, 0, 1), ParserModuleBudgetKind::kModuleCount, "module_count");
    EXPECT_EQ(tracker.usage().modules, 1U);
    EXPECT_EQ(tracker.usage().claims, 2U);
  }
}

TEST(ParserModuleSessionBudget, CountsResourcesNotIdentities) {
  // Two loads of one artifact (or native and wasm builds of one source) are
  // two reservations; duplicate-provider policy belongs to the catalog.
  ParserModuleSessionBudgetLimits limits;
  limits.maximum_modules = 2;
  ParserModuleSessionBudgetTracker tracker(limits);
  const auto first = tracker.admitModule(10, 1, 0);
  const auto second = tracker.admitModule(10, 1, 0);
  ASSERT_TRUE(first.accepted());
  ASSERT_TRUE(second.accepted());
  EXPECT_NE(first.reservation, second.reservation);
  EXPECT_EQ(tracker.usage().modules, 2U);
  expectDecline(tracker.admitModule(10, 1, 0), ParserModuleBudgetKind::kModuleCount, "module_count");
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
  const auto small = tracker.admitModule(100, 1, 100);
  const auto large = tracker.admitModule(100, 1, 250);
  ASSERT_TRUE(small.accepted());
  ASSERT_TRUE(large.accepted());
  ASSERT_TRUE(tracker.admitInstance(large.reservation).accepted());

  expectDecline(
      tracker.admitInstance(small.reservation), ParserModuleBudgetKind::kTotalLinearMemory, "total_linear_memory");
  EXPECT_EQ(tracker.usage().active_instances, 1U);
  tracker.releaseInstance(large.reservation);
  ASSERT_TRUE(tracker.admitInstance(small.reservation).accepted());
  ASSERT_TRUE(tracker.admitInstance(small.reservation).accepted());
  expectDecline(tracker.admitInstance(small.reservation), ParserModuleBudgetKind::kActiveInstances, "active_instances");

  expectDecline(tracker.admitInstance(0), ParserModuleBudgetKind::kNone, "not admitted");
}

TEST(ParserModuleSessionBudget, ReleasesAreIdempotentAndRestoreCapacity) {
  ParserModuleSessionBudgetTracker tracker(
      ParserModuleSessionBudgetLimits{
          .maximum_modules = 1,
          .maximum_artifact_bytes = 10,
          .maximum_claims = 1,
          .maximum_active_instances = 1,
          .maximum_linear_memory_bytes = 20,
      });
  const auto module = tracker.admitModule(10, 1, 20);
  ASSERT_TRUE(module.accepted());
  ASSERT_TRUE(tracker.admitInstance(module.reservation).accepted());
  tracker.releaseInstance(module.reservation);
  tracker.releaseInstance(module.reservation);  // ignored
  EXPECT_EQ(tracker.usage().active_instances, 0U);
  EXPECT_EQ(tracker.usage().declared_linear_memory_bytes, 0U);

  ASSERT_TRUE(tracker.admitInstance(module.reservation).accepted());
  tracker.releaseModule(module.reservation);    // gives back the live instance too
  tracker.releaseModule(module.reservation);    // ignored
  tracker.releaseInstance(module.reservation);  // ignored: module is gone
  EXPECT_EQ(tracker.usage().modules, 0U);
  EXPECT_EQ(tracker.usage().claims, 0U);
  EXPECT_EQ(tracker.usage().active_instances, 0U);
  EXPECT_EQ(tracker.usage().declared_linear_memory_bytes, 0U);
  ASSERT_TRUE(tracker.admitModule(10, 1, 20).accepted());
}

TEST(ParserModuleSessionBudget, TracksConcurrentAdmissionAndReleaseFromManyThreads) {
  // Wrapper destructors release reservations on whatever thread drops them,
  // so the tracker must synchronize itself.
  ParserModuleSessionBudgetLimits limits;
  limits.maximum_modules = 1000;
  limits.maximum_claims = 100000;
  ParserModuleSessionBudgetTracker tracker(limits);
  std::vector<std::thread> threads;
  for (int worker = 0; worker < 8; ++worker) {
    threads.emplace_back([&tracker] {
      for (int round = 0; round < 200; ++round) {
        const auto module = tracker.admitModule(1, 1, 1);
        ASSERT_TRUE(module.accepted());
        const auto instance = tracker.admitInstance(module.reservation);
        ASSERT_TRUE(instance.accepted());
        tracker.releaseInstance(module.reservation);
        tracker.releaseModule(module.reservation);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(tracker.usage().modules, 0U);
  EXPECT_EQ(tracker.usage().claims, 0U);
  EXPECT_EQ(tracker.usage().active_instances, 0U);
  EXPECT_EQ(tracker.usage().declared_linear_memory_bytes, 0U);
}

}  // namespace
}  // namespace PJ
