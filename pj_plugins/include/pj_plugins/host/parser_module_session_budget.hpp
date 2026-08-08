#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_session_budget.hpp
 * @brief Pure admission accounting for parser-module session limits.
 *
 * This state is deliberately non-thread-safe. The application host owns
 * serialization and calls it before compilation or lazy instantiation. A
 * declined reservation never mutates usage.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace PJ {

struct ParserModuleSessionBudgetLimits {
  static constexpr uint64_t kDefaultMaximumModules = 64;
  static constexpr uint64_t kDefaultMaximumArtifactBytes = UINT64_C(64) * 1024U * 1024U;
  static constexpr uint64_t kDefaultMaximumClaims = 4096;
  static constexpr uint64_t kDefaultMaximumActiveInstances = 128;
  static constexpr uint64_t kDefaultMaximumLinearMemoryBytes = UINT64_C(4) * 1024U * 1024U * 1024U;

  uint64_t maximum_modules = kDefaultMaximumModules;
  uint64_t maximum_artifact_bytes = kDefaultMaximumArtifactBytes;
  uint64_t maximum_claims = kDefaultMaximumClaims;
  uint64_t maximum_active_instances = kDefaultMaximumActiveInstances;
  uint64_t maximum_linear_memory_bytes = kDefaultMaximumLinearMemoryBytes;
};

enum class ParserModuleAdmissionOutcome : uint8_t {
  kAccept,
  kDecline,
};

enum class ParserModuleBudgetKind : uint8_t {
  kNone,
  kModuleCount,
  kArtifactFileSize,
  kTotalClaims,
  kActiveInstances,
  kTotalLinearMemory,
};

struct ParserModuleAdmissionDecision {
  ParserModuleAdmissionOutcome outcome = ParserModuleAdmissionOutcome::kDecline;
  ParserModuleBudgetKind exhausted_budget = ParserModuleBudgetKind::kNone;
  std::string diagnostic;

  [[nodiscard]] bool accepted() const noexcept {
    return outcome == ParserModuleAdmissionOutcome::kAccept;
  }
};

struct ParserModuleSessionBudgetUsage {
  uint64_t modules = 0;
  uint64_t claims = 0;
  uint64_t active_instances = 0;
  uint64_t declared_linear_memory_bytes = 0;
};

class ParserModuleSessionBudgetTracker {
 public:
  explicit ParserModuleSessionBudgetTracker(ParserModuleSessionBudgetLimits limits = {});

  /// Reserve one compiled module before compilation. `artifact_bytes` is a
  /// per-file gate; claims contribute to the aggregate session total.
  [[nodiscard]] ParserModuleAdmissionDecision admitModule(
      std::string module_id, uint64_t artifact_bytes, uint64_t claim_count, uint64_t declared_linear_memory_maximum);

  /// Reserve one lazy instance. Its module's declared memory maximum is added
  /// to aggregate memory because every instance owns an independent store.
  [[nodiscard]] ParserModuleAdmissionDecision admitInstance(std::string_view module_id);

  [[nodiscard]] bool releaseInstance(std::string_view module_id);
  [[nodiscard]] bool releaseModule(std::string_view module_id);

  [[nodiscard]] const ParserModuleSessionBudgetLimits& limits() const noexcept;
  [[nodiscard]] ParserModuleSessionBudgetUsage usage() const noexcept;

 private:
  struct ModuleReservation {
    uint64_t artifact_bytes = 0;
    uint64_t claim_count = 0;
    uint64_t declared_linear_memory_maximum = 0;
    uint64_t active_instances = 0;
  };

  ParserModuleSessionBudgetLimits limits_;
  ParserModuleSessionBudgetUsage usage_;
  std::map<std::string, ModuleReservation, std::less<>> modules_;
};

/// Process-session defaults used by loader overloads that are not supplied an
/// application-owned tracker. The returned tracker is shared by native and
/// wasm admission.
[[nodiscard]] std::shared_ptr<ParserModuleSessionBudgetTracker> defaultParserModuleSessionBudget();

}  // namespace PJ
