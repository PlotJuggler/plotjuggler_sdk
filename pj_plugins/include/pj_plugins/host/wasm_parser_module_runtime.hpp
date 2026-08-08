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
  WasmParserModuleInstance() = default;
  ~WasmParserModuleInstance();

  WasmParserModuleInstance(WasmParserModuleInstance&& other) noexcept;
  WasmParserModuleInstance& operator=(WasmParserModuleInstance&& other) noexcept;

  WasmParserModuleInstance(const WasmParserModuleInstance&) = delete;
  WasmParserModuleInstance& operator=(const WasmParserModuleInstance&) = delete;

  /// Instantiate the shared compiled module in a new store, run `_initialize`
  /// exactly once, then create the manifest claim at `claim_index`.
  [[nodiscard]] static Expected<WasmParserModuleInstance, WasmParserModuleCreateError> create(
      const WasmParserModule& module, uint32_t claim_index);

  [[nodiscard]] Expected<ParserModuleBindResult> bind(const parser_module::BindingInfoV1& info);

  /// Parse one message. Contract violations accrue per module claim. The
  /// third violation destroys and recreates the instance through the accepted
  /// create/bind inputs; a second quarantine disables the claim for the
  /// session and invalidates this wrapper.
  [[nodiscard]] Expected<ParserModuleParseResult> parse(const parser_module::ParseInputV1& input);

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] uint32_t claimIndex() const noexcept;
  [[nodiscard]] ParserModuleStrikeState strikeState() const;
  [[nodiscard]] std::string_view lifecycleDiagnostic() const noexcept;

 private:
  explicit WasmParserModuleInstance(std::unique_ptr<detail::WasmParserModuleInstanceState> state);

  [[nodiscard]] Expected<void> recreateBoundInstance();

  std::unique_ptr<detail::WasmParserModuleInstanceState> state_;
};

}  // namespace PJ
