// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/testing/parser_runtime_recorder.hpp"
#include "pj_base/sdk/testing/parser_write_recorder.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

class InspectableParser final : public PJ::MessageParserPluginBase {
 public:
  [[nodiscard]] bool diagnosticsBound() const noexcept {
    return parserRuntimeHostBound();
  }

  void emitDiagnostic(
      PJ::sdk::ParserDiagnosticLevel level, std::string_view stable_code, std::string_view message,
      uint64_t occurrences = 1) const noexcept {
    parserRuntimeHost().reportDiagnostic(level, stable_code, message, occurrences);
  }
};

TEST(MessageParserRuntimeService, OptionalServiceCanBeAbsent) {
  PJ::sdk::testing::ParserWriteRecorder writes;
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::ParserWriteHostService>(writes.makeHost());

  InspectableParser parser;
  ASSERT_TRUE(parser.bind(PJ::sdk::ServiceRegistry{registry.view()}));
  EXPECT_FALSE(parser.diagnosticsBound());
  EXPECT_NO_FATAL_FAILURE(
      parser.emitDiagnostic(PJ::sdk::ParserDiagnosticLevel::Warning, "optional_field", "optional field was ignored"));
}

TEST(MessageParserRuntimeService, DefaultBindAcquiresAndForwardsOptionalService) {
  PJ::sdk::testing::ParserWriteRecorder writes;
  PJ::sdk::testing::ParserRuntimeRecorder diagnostics;
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::ParserWriteHostService>(writes.makeHost());
  registry.registerService<PJ::sdk::ParserRuntimeHostService>(diagnostics.makeHost());

  InspectableParser parser;
  ASSERT_TRUE(parser.bind(PJ::sdk::ServiceRegistry{registry.view()}));
  ASSERT_TRUE(parser.diagnosticsBound());

  parser.emitDiagnostic(
      PJ::sdk::ParserDiagnosticLevel::Error, "integer_overflow", "5 values exceeded the target width", 5);

  ASSERT_EQ(diagnostics.diagnostics().size(), 1u);
  const auto& diagnostic = diagnostics.diagnostics().front();
  EXPECT_EQ(diagnostic.level, PJ::sdk::ParserDiagnosticLevel::Error);
  EXPECT_EQ(diagnostic.stable_code, "integer_overflow");
  EXPECT_EQ(diagnostic.message, "5 values exceeded the target width");
  EXPECT_EQ(diagnostic.occurrences, 5u);
}

}  // namespace
