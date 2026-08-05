/**
 * @file parser_runtime_host.hpp
 * @brief Host adapter for the optional "pj.parser_runtime.v1" service.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {

/// Sink implemented by an embedding host to consume parser diagnostics.
///
/// Calls arrive synchronously from parser callback threads. The sink must be
/// thread-safe, noexcept, and non-blocking; queueing, bounding, rate limiting,
/// and aggregation are host policy. Borrowed strings are valid only for the
/// call and must be copied if retained. The host aggregates by parser manifest
/// ID, bound schema/type, level, and stable code; that binding context is
/// supplied by the embedder around this per-parser service instance.
///
/// @since 0.21.0
class ParserDiagnosticSink {
 public:
  virtual ~ParserDiagnosticSink() = default;

  /// @since 0.21.0
  virtual void reportDiagnostic(
      ParserDiagnosticLevel level, std::string_view stable_code, std::string_view message,
      uint64_t occurrences) noexcept = 0;
};

/// Adapts a ParserDiagnosticSink to the C ABI PJ_parser_runtime_host_t.
/// The adapter and sink must outlive every plugin bound to view().
///
/// @since 0.21.0
class ParserRuntimeHost {
 public:
  explicit ParserRuntimeHost(ParserDiagnosticSink& sink) noexcept : sink_(sink) {}

  /// Return the service fat pointer registered under
  /// PJ_PARSER_RUNTIME_HOST_SERVICE_V1.
  ///
  /// @since 0.21.0
  [[nodiscard]] PJ_parser_runtime_host_t view() noexcept {
    static constexpr PJ_parser_runtime_host_vtable_t kVtable = {
        /* protocol_version = */ 1,
        /* struct_size      = */ sizeof(PJ_parser_runtime_host_vtable_t),
        /* report_diagnostic = */ &ParserRuntimeHost::dispatchReportDiagnostic,
    };
    return PJ_parser_runtime_host_t{this, &kVtable};
  }

 private:
  static void dispatchReportDiagnostic(
      void* ctx, PJ_parser_diagnostic_level_t level, PJ_string_view_t stable_code, PJ_string_view_t message,
      uint64_t occurrences) noexcept {
    if (ctx == nullptr) {
      return;
    }
    auto* self = static_cast<ParserRuntimeHost*>(ctx);
    self->sink_.reportDiagnostic(
        static_cast<ParserDiagnosticLevel>(level), toStringView(stable_code), toStringView(message), occurrences);
  }

  ParserDiagnosticSink& sink_;
};

}  // namespace PJ::sdk
