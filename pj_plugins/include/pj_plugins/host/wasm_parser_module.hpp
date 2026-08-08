#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file wasm_parser_module.hpp
 * @brief Wasmer-backed loader for sandboxed functional parser modules.
 *
 * Admission is passive: the loader validates the manifest section, reactor
 * shape, operational signatures, exported memory, and imports before Wasmer
 * compiles the artifact. The v1 import allow-list is deliberately empty. In
 * particular, every `wasi_snapshot_preview1` fd, path, socket, environment,
 * clock, random, process, and scheduler import is rejected.
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
 * Wasmer's metering middleware is enabled for every compiled module and each
 * guest call receives a fresh instruction-point allowance. The pinned archive
 * exports no public interrupt or epoch API, so metering is the enforceable
 * deadline mechanism. Linear memory must declare a maximum within the loader
 * cap; Wasmer enforces that maximum at runtime. The SDK authoring preset puts
 * a configurable 1 MiB guest shadow stack before data segments so overflow
 * traps instead of corrupting them. Native engine stack depth relies on
 * Wasmer 7's guarded default because its C API exposes no stack-limit setter.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_plugins/host/parser_module_runtime.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ {

namespace detail {
struct WasmParserModuleState;
}

class WasmParserModuleInstance;

struct WasmParserModuleLimits {
  static constexpr uint64_t kDefaultMaximumArtifactBytes = UINT64_C(64) * 1024U * 1024U;
  static constexpr uint64_t kDefaultMaximumLinearMemoryBytes = UINT64_C(256) * 1024U * 1024U;
  static constexpr uint64_t kDefaultMeteringPointsPerCall = UINT64_C(10000000);

  uint64_t maximum_artifact_bytes = kDefaultMaximumArtifactBytes;
  uint64_t maximum_linear_memory_bytes = kDefaultMaximumLinearMemoryBytes;
  uint64_t metering_points_per_call = kDefaultMeteringPointsPerCall;
};

class WasmParserModule {
 public:
  WasmParserModule() = default;

  /// Read, validate, and compile one wasm parser module. Rejection emits
  /// exactly one error diagnostic when a sink is supplied.
  [[nodiscard]] static Expected<WasmParserModule> load(
      std::string_view path, DiagnosticSink sink = {}, std::string diagnostic_source = "WasmParserModule");

  /// Load with explicit artifact, linear-memory, and instruction budgets.
  [[nodiscard]] static Expected<WasmParserModule> load(
      std::string_view path, const WasmParserModuleLimits& limits, DiagnosticSink sink = {},
      std::string diagnostic_source = "WasmParserModule");

  /// Load using an application-owned aggregate session budget.
  [[nodiscard]] static Expected<WasmParserModule> load(
      std::string_view path, std::shared_ptr<ParserModuleSessionBudgetTracker> budget, DiagnosticSink sink = {},
      std::string diagnostic_source = "WasmParserModule");

  /// Load with both per-artifact limits and aggregate session budgets.
  [[nodiscard]] static Expected<WasmParserModule> load(
      std::string_view path, const WasmParserModuleLimits& limits,
      std::shared_ptr<ParserModuleSessionBudgetTracker> budget, DiagnosticSink sink = {},
      std::string diagnostic_source = "WasmParserModule");

  [[nodiscard]] bool valid() const noexcept {
    return state_ != nullptr;
  }

  [[nodiscard]] std::string_view path() const noexcept;
  [[nodiscard]] std::string_view manifestJson() const noexcept;
  [[nodiscard]] uint64_t artifactSize() const noexcept;
  [[nodiscard]] uint64_t declaredLinearMemoryMaximum() const noexcept;
  [[nodiscard]] ParserModuleStrikeState strikeState(uint32_t claim_index) const;

 private:
  explicit WasmParserModule(std::shared_ptr<const detail::WasmParserModuleState> state);

  std::shared_ptr<const detail::WasmParserModuleState> state_;

  friend class WasmParserModuleInstance;
};

}  // namespace PJ
