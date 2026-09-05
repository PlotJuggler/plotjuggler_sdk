#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <memory>
#include <string>

#include "detail/native_parser_module_loader.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ::detail {

struct NativeParserModuleState {
  ~NativeParserModuleState();

  NativeModuleHandle handle = nullptr;
  std::string path;
  std::string manifest_json;
  /// Null when the host loaded without a session budget (0.22 behavior).
  std::shared_ptr<ParserModuleSessionBudgetTracker> session_budget;
  uint64_t module_reservation = 0;

  PJ_module_abi_fn_t abi = nullptr;
  PJ_module_create_fn_t create = nullptr;
  PJ_module_destroy_fn_t destroy = nullptr;
  PJ_module_bind_fn_t bind = nullptr;
  PJ_module_parse_fn_t parse = nullptr;
  PJ_module_last_error_fn_t last_error = nullptr;
  PJ_module_alloc_fn_t alloc = nullptr;
  PJ_module_free_fn_t free = nullptr;
  PJ_module_manifest_addr_fn_t manifest_addr = nullptr;
  PJ_module_manifest_len_fn_t manifest_len = nullptr;
};

}  // namespace PJ::detail
