#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file wasm_parser_module_runtime.hpp
 * @brief Store-per-instance wasm parser-module lifecycle wrapper.
 *
 * The wrapper is move-only and not concurrently callable. A host may migrate
 * it between threads when calls do not overlap. Every host access to guest
 * memory re-acquires the current base and size after the preceding guest call.
 *
 * Like NativeParserModuleInstance, the wrapper only classifies faults: traps,
 * metering exhaustion, malformed descriptors, and bad splices are returned as
 * contract violations, module-reported failures as data errors. Recording
 * strikes, quarantining a claim, and replaying create/bind are host policy
 * driven through ParserModuleStrikeTracker, exactly as for native modules.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/parser_module_runtime.hpp"
#include "pj_plugins/host/wasm_parser_module.hpp"

namespace PJ {

namespace detail {
struct WasmParserModuleInstanceState;
}

enum class WasmParserModuleCreateOutcome : uint8_t {
  kError,
  kAdmissionDecline,
};

struct WasmParserModuleCreateError {
  WasmParserModuleCreateOutcome outcome = WasmParserModuleCreateOutcome::kError;
  ParserModuleFaultKind fault = ParserModuleFaultKind::kNone;
  std::string message;
};

class WasmParserModuleInstance {
 public:
  WasmParserModuleInstance();
  ~WasmParserModuleInstance();

  WasmParserModuleInstance(WasmParserModuleInstance&& other) noexcept;
  WasmParserModuleInstance& operator=(WasmParserModuleInstance&& other) noexcept;

  WasmParserModuleInstance(const WasmParserModuleInstance&) = delete;
  WasmParserModuleInstance& operator=(const WasmParserModuleInstance&) = delete;

  /// Instantiate the shared compiled module in a new store, run `_initialize`
  /// exactly once, then create the manifest claim at `claim_index`. A trap on
  /// that path is returned with `fault == kContractViolation`; a session-budget
  /// rejection with `outcome == kAdmissionDecline`.
  [[nodiscard]] static Expected<WasmParserModuleInstance, WasmParserModuleCreateError> create(
      const WasmParserModule& module, uint32_t claim_index);

  [[nodiscard]] Expected<ParserModuleBindResult> bind(const parser_module::BindingInfoV1& info);

  /// Parse one message and consume the returned descriptor transactionally.
  [[nodiscard]] Expected<ParserModuleParseResult> parse(const parser_module::ParseInputV1& input);

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] uint32_t claimIndex() const noexcept;
  /// The most recent contract-violation text, including guest `free` faults
  /// raised while cleaning up after another failure.
  [[nodiscard]] std::string_view lifecycleDiagnostic() const noexcept;

 private:
  explicit WasmParserModuleInstance(std::unique_ptr<detail::WasmParserModuleInstanceState> state);

  std::unique_ptr<detail::WasmParserModuleInstanceState> state_;
};

}  // namespace PJ
