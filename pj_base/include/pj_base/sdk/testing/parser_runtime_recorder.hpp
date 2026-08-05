/**
 * @file parser_runtime_recorder.hpp
 * @brief Test helper that records parser runtime diagnostics.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/sdk/parser_runtime_host.hpp"

namespace PJ::sdk::testing {

/** One owning diagnostic captured by ParserRuntimeRecorder. @since 0.21.0 */
struct RecordedParserDiagnostic {
  ParserDiagnosticLevel level = ParserDiagnosticLevel::Info;
  std::string stable_code;
  std::string message;
  uint64_t occurrences = 0;
};

/// In-memory ParserDiagnosticSink for single-threaded parser unit tests.
/// Strings are copied, so records remain valid after the parser call returns.
///
/// @since 0.21.0
class ParserRuntimeRecorder final : public ParserDiagnosticSink {
 public:
  ParserRuntimeRecorder() : host_(*this) {}

  ParserRuntimeRecorder(const ParserRuntimeRecorder&) = delete;
  ParserRuntimeRecorder& operator=(const ParserRuntimeRecorder&) = delete;
  ParserRuntimeRecorder(ParserRuntimeRecorder&&) = delete;
  ParserRuntimeRecorder& operator=(ParserRuntimeRecorder&&) = delete;

  /// Build a PJ_parser_runtime_host_t whose context points at this recorder.
  /// The recorder must outlive the returned handle.
  ///
  /// @since 0.21.0
  [[nodiscard]] PJ_parser_runtime_host_t makeHost() noexcept {
    return host_.view();
  }

  [[nodiscard]] const std::vector<RecordedParserDiagnostic>& diagnostics() const noexcept {
    return diagnostics_;
  }

  [[nodiscard]] std::vector<RecordedParserDiagnostic>& diagnostics() noexcept {
    return diagnostics_;
  }

  void clear() noexcept {
    diagnostics_.clear();
  }

  void reportDiagnostic(
      ParserDiagnosticLevel level, std::string_view stable_code, std::string_view message,
      uint64_t occurrences) noexcept override {
    try {
      diagnostics_.push_back(
          RecordedParserDiagnostic{level, std::string(stable_code), std::string(message), occurrences});
    } catch (...) {
      // The diagnostics ABI is noexcept. A test recorder that runs out of
      // memory drops the record rather than terminating across the boundary.
    }
  }

 private:
  ParserRuntimeHost host_;
  std::vector<RecordedParserDiagnostic> diagnostics_;
};

}  // namespace PJ::sdk::testing
