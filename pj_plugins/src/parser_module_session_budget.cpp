// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_session_budget.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace PJ {
namespace {

ParserModuleAdmissionDecision accept() {
  return ParserModuleAdmissionDecision{
      .outcome = ParserModuleAdmissionOutcome::kAccept,
      .exhausted_budget = ParserModuleBudgetKind::kNone,
      .diagnostic = {},
  };
}

ParserModuleAdmissionDecision decline(ParserModuleBudgetKind kind, std::string reason) {
  return ParserModuleAdmissionDecision{
      .outcome = ParserModuleAdmissionOutcome::kDecline,
      .exhausted_budget = kind,
      .diagnostic = "parser-module admission DECLINE: " + std::move(reason),
  };
}

ParserModuleAdmissionDecision declineBudget(ParserModuleBudgetKind kind, std::string_view name) {
  return decline(kind, std::string(name) + " budget exhausted");
}

bool exceedsAggregate(uint64_t current, uint64_t additional, uint64_t maximum) {
  return current > maximum || additional > maximum - current;
}

}  // namespace

ParserModuleSessionBudgetTracker::ParserModuleSessionBudgetTracker(ParserModuleSessionBudgetLimits limits)
    : limits_(limits) {}

ParserModuleAdmissionDecision ParserModuleSessionBudgetTracker::admitModule(
    std::string module_id, uint64_t artifact_bytes, uint64_t claim_count, uint64_t declared_linear_memory_maximum) {
  if (modules_.find(module_id) != modules_.end()) {
    return decline(ParserModuleBudgetKind::kNone, "module is already admitted");
  }
  if (usage_.modules >= limits_.maximum_modules) {
    return declineBudget(ParserModuleBudgetKind::kModuleCount, "module_count");
  }
  if (artifact_bytes > limits_.maximum_artifact_bytes) {
    return declineBudget(ParserModuleBudgetKind::kArtifactFileSize, "artifact_file_size");
  }
  if (exceedsAggregate(usage_.claims, claim_count, limits_.maximum_claims)) {
    return declineBudget(ParserModuleBudgetKind::kTotalClaims, "total_claims");
  }

  modules_.emplace(
      std::move(module_id), ModuleReservation{
                                .artifact_bytes = artifact_bytes,
                                .claim_count = claim_count,
                                .declared_linear_memory_maximum = declared_linear_memory_maximum,
                                .active_instances = 0,
                            });
  ++usage_.modules;
  usage_.claims += claim_count;
  return accept();
}

ParserModuleAdmissionDecision ParserModuleSessionBudgetTracker::admitInstance(std::string_view module_id) {
  auto module = modules_.find(module_id);
  if (module == modules_.end()) {
    return decline(ParserModuleBudgetKind::kNone, "module is not admitted");
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
  return accept();
}

bool ParserModuleSessionBudgetTracker::releaseInstance(std::string_view module_id) {
  auto module = modules_.find(module_id);
  if (module == modules_.end() || module->second.active_instances == 0) {
    return false;
  }
  --module->second.active_instances;
  --usage_.active_instances;
  usage_.declared_linear_memory_bytes -= module->second.declared_linear_memory_maximum;
  return true;
}

bool ParserModuleSessionBudgetTracker::releaseModule(std::string_view module_id) {
  auto module = modules_.find(module_id);
  if (module == modules_.end() || module->second.active_instances != 0) {
    return false;
  }
  --usage_.modules;
  usage_.claims -= module->second.claim_count;
  modules_.erase(module);
  return true;
}

const ParserModuleSessionBudgetLimits& ParserModuleSessionBudgetTracker::limits() const noexcept {
  return limits_;
}

ParserModuleSessionBudgetUsage ParserModuleSessionBudgetTracker::usage() const noexcept {
  return usage_;
}

std::shared_ptr<ParserModuleSessionBudgetTracker> defaultParserModuleSessionBudget() {
  static auto tracker = std::make_shared<ParserModuleSessionBudgetTracker>();
  return tracker;
}

}  // namespace PJ
