#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <wasmer.h>

#include <cstdint>
#include <memory>
#include <string>

#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ::detail {

struct WasmParserModuleState {
  ~WasmParserModuleState();

  std::string path;
  std::string manifest_json;
  uint64_t claim_count = 0;
  uint64_t artifact_size = 0;
  uint64_t declared_linear_memory_maximum = 0;
  uint64_t declared_table_elements = 0;
  uint64_t metering_points_per_call = 0;
  wasm_engine_t* engine = nullptr;
  wasm_module_t* module = nullptr;
  /// Null when the host loaded without a session budget.
  std::shared_ptr<ParserModuleSessionBudgetTracker> session_budget;
  uint64_t module_reservation = 0;
};

}  // namespace PJ::detail
