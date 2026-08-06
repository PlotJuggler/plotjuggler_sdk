// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/parser_runtime_host.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "pj_base/sdk/testing/parser_runtime_recorder.hpp"

namespace {

struct RawCapture {
  PJ_parser_diagnostic_level_t level = PJ_PARSER_DIAGNOSTIC_INFO;
  std::string stable_code;
  std::string message;
  uint64_t occurrences = 0;
  uint64_t calls = 0;
};

void captureRaw(
    void* ctx, PJ_parser_diagnostic_level_t level, PJ_string_view_t stable_code, PJ_string_view_t message,
    uint64_t occurrences) noexcept {
  auto* capture = static_cast<RawCapture*>(ctx);
  capture->level = level;
  capture->stable_code.assign(stable_code.data == nullptr ? "" : stable_code.data, stable_code.size);
  capture->message.assign(message.data == nullptr ? "" : message.data, message.size);
  capture->occurrences = occurrences;
  ++capture->calls;
}

TEST(ParserRuntimeHostView, ForwardsRawCServiceFields) {
  RawCapture capture;
  const PJ_parser_runtime_host_vtable_t vtable = {
      /* protocol_version = */ 1,
      /* struct_size      = */ sizeof(PJ_parser_runtime_host_vtable_t),
      /* report_diagnostic = */ &captureRaw,
  };
  PJ::sdk::ParserRuntimeHostView view{PJ_parser_runtime_host_t{&capture, &vtable}};

  view.reportDiagnostic(
      PJ::sdk::ParserDiagnosticLevel::Warning, "integer_overflow", "7 integer values exceeded uint32", 7);

  EXPECT_EQ(capture.calls, 1u);
  EXPECT_EQ(capture.level, PJ_PARSER_DIAGNOSTIC_WARNING);
  EXPECT_EQ(capture.stable_code, "integer_overflow");
  EXPECT_EQ(capture.message, "7 integer values exceeded uint32");
  EXPECT_EQ(capture.occurrences, 7u);
}

TEST(ParserRuntimeHostView, UnboundAndMissingSlotAreSafeNoOps) {
  PJ::sdk::ParserRuntimeHostView unbound;
  EXPECT_FALSE(unbound.valid());
  EXPECT_NO_FATAL_FAILURE(unbound.reportDiagnostic(PJ::sdk::ParserDiagnosticLevel::Info, "ignored", "service absent"));

  RawCapture capture;
  const PJ_parser_runtime_host_vtable_t truncated_vtable = {
      /* protocol_version = */ 1,
      /* struct_size      = */ offsetof(PJ_parser_runtime_host_vtable_t, report_diagnostic),
      /* report_diagnostic = */ &captureRaw,
  };
  PJ::sdk::ParserRuntimeHostView truncated{PJ_parser_runtime_host_t{&capture, &truncated_vtable}};
  EXPECT_NO_FATAL_FAILURE(truncated.reportDiagnostic(PJ::sdk::ParserDiagnosticLevel::Error, "ignored", "slot absent"));
  EXPECT_EQ(capture.calls, 0u);
}

class CapturingSink final : public PJ::sdk::ParserDiagnosticSink {
 public:
  void reportDiagnostic(
      PJ::sdk::ParserDiagnosticLevel level, std::string_view stable_code, std::string_view message,
      uint64_t occurrences) noexcept override {
    level_ = level;
    stable_code_.assign(stable_code);
    message_.assign(message);
    occurrences_ = occurrences;
    ++calls_;
  }

  PJ::sdk::ParserDiagnosticLevel level_ = PJ::sdk::ParserDiagnosticLevel::Info;
  std::string stable_code_;
  std::string message_;
  uint64_t occurrences_ = 0;
  uint64_t calls_ = 0;
};

TEST(ParserRuntimeHost, RawCViewForwardsToSink) {
  CapturingSink sink;
  PJ::sdk::ParserRuntimeHost host{sink};
  const PJ_parser_runtime_host_t raw = host.view();

  ASSERT_NE(raw.ctx, nullptr);
  ASSERT_NE(raw.vtable, nullptr);
  ASSERT_NE(raw.vtable->report_diagnostic, nullptr);
  raw.vtable->report_diagnostic(
      raw.ctx, PJ_PARSER_DIAGNOSTIC_ERROR, PJ::sdk::toAbiString("schema_mismatch"),
      PJ::sdk::toAbiString("optional field has the wrong type"), 3);

  EXPECT_EQ(sink.calls_, 1u);
  EXPECT_EQ(sink.level_, PJ::sdk::ParserDiagnosticLevel::Error);
  EXPECT_EQ(sink.stable_code_, "schema_mismatch");
  EXPECT_EQ(sink.message_, "optional field has the wrong type");
  EXPECT_EQ(sink.occurrences_, 3u);
}

TEST(ParserRuntimeRecorder, CapturesOwningRecordsAndClears) {
  PJ::sdk::testing::ParserRuntimeRecorder recorder;
  PJ::sdk::ParserRuntimeHostView view{recorder.makeHost()};

  std::string transient_message = "first occurrence";
  view.reportDiagnostic(PJ::sdk::ParserDiagnosticLevel::Info, "optional_field", transient_message);
  transient_message = "changed after call";

  ASSERT_EQ(recorder.diagnostics().size(), 1u);
  const auto& diagnostic = recorder.diagnostics().front();
  EXPECT_EQ(diagnostic.level, PJ::sdk::ParserDiagnosticLevel::Info);
  EXPECT_EQ(diagnostic.stable_code, "optional_field");
  EXPECT_EQ(diagnostic.message, "first occurrence");
  EXPECT_EQ(diagnostic.occurrences, 1u);

  recorder.clear();
  EXPECT_TRUE(recorder.diagnostics().empty());
}

}  // namespace
