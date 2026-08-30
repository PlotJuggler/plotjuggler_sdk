#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file native_parser_module.hpp
 * @brief Session-lifetime loader for native functional parser modules.
 *
 * Loading resolves the complete frozen module ABI, copies the embedded
 * manifest, and validates its identity and claims for budget accounting.
 * Successfully opened artifacts remain loaded for the process session; no
 * module code or manifest pointer is used after unload. Catalog insertion and
 * provenance assignment remain an explicit ParserClaimCatalog caller step.
 */

#include <memory>
#include <string>
#include <string_view>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ {

namespace detail {
struct NativeParserModuleState;
}

class NativeParserModuleInstance;

class NativeParserModule {
 public:
  NativeParserModule() = default;

  /// Open and validate one native parser-module artifact. Each rejected load
  /// emits exactly one error diagnostic when a sink is supplied.
  [[nodiscard]] static Expected<NativeParserModule> load(
      std::string_view path, DiagnosticSink sink = {}, std::string diagnostic_source = "NativeParserModule");

  /// Load under an application-owned aggregate session budget. A null budget
  /// behaves exactly like the overload above: no admission accounting.
  [[nodiscard]] static Expected<NativeParserModule> load(
      std::string_view path, std::shared_ptr<ParserModuleSessionBudgetTracker> budget, DiagnosticSink sink = {},
      std::string diagnostic_source = "NativeParserModule");

  [[nodiscard]] bool valid() const noexcept {
    return state_ != nullptr;
  }

  [[nodiscard]] std::string_view path() const noexcept;
  [[nodiscard]] std::string_view manifestJson() const noexcept;

 private:
  explicit NativeParserModule(std::shared_ptr<const detail::NativeParserModuleState> state);

  std::shared_ptr<const detail::NativeParserModuleState> state_;

  friend class NativeParserModuleInstance;
};

}  // namespace PJ
