#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_session_budget.hpp
 * @brief Aggregate resource accounting for parser-module admission.
 *
 * The tracker counts resources, not identities: every accepted module
 * reservation is an opaque id, so two loads of one artifact (or native and
 * wasm builds of one source) are two reservations. Duplicate-provider policy
 * belongs to the claim catalog, not here.
 *
 * The tracker is thread-safe. Loader wrappers release reservations from their
 * destructors, which run on whichever thread drops the wrapper, so callers
 * cannot be asked to serialize it. A declined reservation never mutates usage.
 */

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

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

enum class ParserModuleBudgetKind : uint8_t {
  kNone,
  kModuleCount,
  kArtifactFileSize,
  kTotalClaims,
  kActiveInstances,
  kTotalLinearMemory,
};

/// Outcome of one admission request. `reservation` is nonzero exactly when
/// the request was accepted and is the handle for every later call.
struct ParserModuleAdmissionDecision {
  uint64_t reservation = 0;
  ParserModuleBudgetKind exhausted_budget = ParserModuleBudgetKind::kNone;
  std::string diagnostic;

  [[nodiscard]] bool accepted() const noexcept {
    return reservation != 0;
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

  /// Reserve one module before compilation. `artifact_bytes` is a per-file
  /// gate; `claim_count` joins the aggregate claim total;
  /// `declared_linear_memory_maximum` is charged per admitted instance.
  [[nodiscard]] ParserModuleAdmissionDecision admitModule(
      uint64_t artifact_bytes, uint64_t claim_count, uint64_t declared_linear_memory_maximum);

  /// Reserve one instance of an admitted module. Every instance owns an
  /// independent store, so the module's declared memory is charged again.
  [[nodiscard]] ParserModuleAdmissionDecision admitInstance(uint64_t module_reservation);

  /// Releases are idempotent-safe: unknown or already-released ids are ignored.
  void releaseInstance(uint64_t module_reservation);
  /// A module reservation is released even when instances are still live;
  /// their later releases then find no module and are ignored.
  void releaseModule(uint64_t module_reservation);

  [[nodiscard]] const ParserModuleSessionBudgetLimits& limits() const noexcept;
  [[nodiscard]] ParserModuleSessionBudgetUsage usage() const;

 private:
  struct ModuleReservation {
    uint64_t claim_count = 0;
    uint64_t declared_linear_memory_maximum = 0;
    uint64_t active_instances = 0;
  };

  ParserModuleSessionBudgetLimits limits_;
  mutable std::mutex mutex_;
  ParserModuleSessionBudgetUsage usage_;
  uint64_t next_reservation_ = 1;
  std::map<uint64_t, ModuleReservation> modules_;
};

}  // namespace PJ
