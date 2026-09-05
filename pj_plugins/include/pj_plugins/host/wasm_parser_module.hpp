#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file wasm_parser_module.hpp
 * @brief Wasmer-backed loader for sandboxed functional parser modules.
 *
 * Admission is passive: the shared pj_base audit validates the manifest
 * section, reactor shape, operational signatures, exported memory, bounded
 * tables, and imports before Wasmer compiles the artifact. The v1 import
 * allow-list is deliberately empty. In particular, every
 * `wasi_snapshot_preview1` fd, path, socket, environment, clock, random,
 * process, and scheduler import is rejected.
 *
 * Wasmer 7.0.1's static C archive does not provide the share/obtain symbols
 * declared by wasm.h. Its engine-owned `wasmer_module_new` extension is the
 * equivalent used here: one compiled module is instantiated in an independent
 * store for every bound instance. The exit-criterion prototype also verified
 * that a store/instance tolerates sequential calls from a thread other than
 * its creator. Instances therefore have no creator-thread affinity, but calls
 * on one instance must never overlap. Per-store executor serialization remains
 * a host responsibility.
 *
 * Compilation uses Wasmer's Singlepass backend when the archive provides it:
 * its compile time is linear in artifact size, which is the only bound
 * available for compiling an untrusted artifact (there is no cancellable
 * compile API). Wasmer's metering middleware is enabled for every compiled
 * module and each guest call receives a fresh instruction-point allowance.
 * That is an instruction budget, not a wall-clock deadline: bulk operators
 * such as memory.copy cost one point regardless of size, and the pinned
 * archive exports no interrupt or epoch API. Linear memory must declare a
 * maximum within the loader cap; Wasmer enforces that maximum at runtime. The
 * SDK authoring preset puts a configurable 1 MiB guest shadow stack before
 * data segments so overflow traps instead of corrupting them. Native engine
 * stack depth relies on Wasmer 7's guarded default because its C API exposes
 * no stack-limit setter.
 *
 * Like the native loader, this loader carries no fault policy: instances
 * classify faults and the host feeds them to ParserModuleStrikeTracker.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/parser_module_wasm.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ {

namespace detail {
struct WasmParserModuleState;
}

class WasmParserModuleInstance;

/// Per-artifact caps and the per-call instruction budget.
struct WasmParserModuleLimits {
  static constexpr uint64_t kDefaultMaximumArtifactBytes = UINT64_C(64) * 1024U * 1024U;
  static constexpr uint64_t kDefaultMeteringPointsPerCall = UINT64_C(10000000);

  uint64_t maximum_artifact_bytes = kDefaultMaximumArtifactBytes;
  uint64_t maximum_linear_memory_bytes = parser_module::ParserModuleWasmLimits::kDefaultMaximumLinearMemoryBytes;
  uint64_t maximum_table_elements = parser_module::ParserModuleWasmLimits::kDefaultMaximumTableElements;
  uint64_t metering_points_per_call = kDefaultMeteringPointsPerCall;
};

struct WasmParserModuleLoadOptions {
  WasmParserModuleLimits limits;
  /// Optional application-owned aggregate session budget.
  std::shared_ptr<ParserModuleSessionBudgetTracker> budget;
  /// Every rejection emits exactly one error diagnostic when a sink is set.
  DiagnosticSink sink;
  std::string diagnostic_source = "WasmParserModule";
};

class WasmParserModule {
 public:
  WasmParserModule() = default;

  /// Read, audit, admit, and compile one wasm parser module.
  [[nodiscard]] static Expected<WasmParserModule> load(
      std::string_view path, const WasmParserModuleLoadOptions& options = {});

  [[nodiscard]] bool valid() const noexcept {
    return state_ != nullptr;
  }

  [[nodiscard]] std::string_view path() const noexcept;
  [[nodiscard]] std::string_view manifestJson() const noexcept;
  [[nodiscard]] uint64_t artifactSize() const noexcept;
  [[nodiscard]] uint64_t declaredLinearMemoryMaximum() const noexcept;
  [[nodiscard]] uint64_t declaredTableElements() const noexcept;

 private:
  explicit WasmParserModule(std::shared_ptr<const detail::WasmParserModuleState> state);

  std::shared_ptr<const detail::WasmParserModuleState> state_;

  friend class WasmParserModuleInstance;
};

}  // namespace PJ
