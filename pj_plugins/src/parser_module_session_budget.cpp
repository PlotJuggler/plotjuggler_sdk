// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_session_budget.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace PJ {
namespace {

ParserModuleAdmissionDecision declineBudget(ParserModuleBudgetKind kind, std::string_view name) {
  return ParserModuleAdmissionDecision{
      .reservation = 0,
      .exhausted_budget = kind,
      .diagnostic = "parser-module admission DECLINE: " + std::string(name) + " budget exhausted",
  };
}

bool exceedsAggregate(uint64_t current, uint64_t additional, uint64_t maximum) {
  return current > maximum || additional > maximum - current;
}

}  // namespace

ParserModuleSessionBudgetTracker::ParserModuleSessionBudgetTracker(ParserModuleSessionBudgetLimits limits)
    : limits_(limits) {}

ParserModuleAdmissionDecision ParserModuleSessionBudgetTracker::admitModule(
    uint64_t artifact_bytes, uint64_t claim_count, uint64_t declared_linear_memory_maximum) {
  const std::scoped_lock lock(mutex_);
  if (usage_.modules >= limits_.maximum_modules) {
    return declineBudget(ParserModuleBudgetKind::kModuleCount, "module_count");
  }
  if (artifact_bytes > limits_.maximum_artifact_bytes) {
    return declineBudget(ParserModuleBudgetKind::kArtifactFileSize, "artifact_file_size");
  }
  if (exceedsAggregate(usage_.claims, claim_count, limits_.maximum_claims)) {
    return declineBudget(ParserModuleBudgetKind::kTotalClaims, "total_claims");
  }

  const uint64_t reservation = next_reservation_++;
  modules_.emplace(
      reservation, ModuleReservation{
                       .claim_count = claim_count,
                       .declared_linear_memory_maximum = declared_linear_memory_maximum,
                       .active_instances = 0,
                   });
  ++usage_.modules;
  usage_.claims += claim_count;
  return ParserModuleAdmissionDecision{
      .reservation = reservation, .exhausted_budget = ParserModuleBudgetKind::kNone, .diagnostic = {}};
}

ParserModuleAdmissionDecision ParserModuleSessionBudgetTracker::admitInstance(uint64_t module_reservation) {
  const std::scoped_lock lock(mutex_);
  auto module = modules_.find(module_reservation);
  if (module == modules_.end()) {
    return ParserModuleAdmissionDecision{
        .reservation = 0,
        .exhausted_budget = ParserModuleBudgetKind::kNone,
        .diagnostic = "parser-module admission DECLINE: module is not admitted",
    };
  }
  if (usage_.active_instances >= limits_.maximum_active_instances) {
    return declineBudget(ParserModuleBudgetKind::kActiveInstances, "active_instances");
  }
  if (exceedsAggregate(
          usage_.declared_linear_memory_bytes, module->second.declared_linear_memory_maximum,
          limits_.maximum_linear_memory_bytes)) {
    return declineBudget(ParserModuleBudgetKind::kTotalLinearMemory, "total_linear_memory");
  }

  ++module->second.active_instances;
  ++usage_.active_instances;
  usage_.declared_linear_memory_bytes += module->second.declared_linear_memory_maximum;
  return ParserModuleAdmissionDecision{
      .reservation = module_reservation, .exhausted_budget = ParserModuleBudgetKind::kNone, .diagnostic = {}};
}

void ParserModuleSessionBudgetTracker::releaseInstance(uint64_t module_reservation) {
  const std::scoped_lock lock(mutex_);
  auto module = modules_.find(module_reservation);
  if (module == modules_.end() || module->second.active_instances == 0) {
    return;
  }
  --module->second.active_instances;
  --usage_.active_instances;
  usage_.declared_linear_memory_bytes -= module->second.declared_linear_memory_maximum;
}

void ParserModuleSessionBudgetTracker::releaseModule(uint64_t module_reservation) {
  const std::scoped_lock lock(mutex_);
  auto module = modules_.find(module_reservation);
  if (module == modules_.end()) {
    return;
  }
  // Live instances of a dropped module still hold their store; their memory
  // and instance counts are given back when each of them is released.
  usage_.active_instances -= module->second.active_instances;
  usage_.declared_linear_memory_bytes -=
      module->second.active_instances * module->second.declared_linear_memory_maximum;
  --usage_.modules;
  usage_.claims -= module->second.claim_count;
  modules_.erase(module);
}

const ParserModuleSessionBudgetLimits& ParserModuleSessionBudgetTracker::limits() const noexcept {
  return limits_;
}

ParserModuleSessionBudgetUsage ParserModuleSessionBudgetTracker::usage() const {
  const std::scoped_lock lock(mutex_);
  return usage_;
}

}  // namespace PJ
