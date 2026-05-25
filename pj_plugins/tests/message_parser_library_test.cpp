// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/message_parser_library.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/testing/parser_write_recorder.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"

#ifndef PJ_MOCK_JSON_PARSER_PLUGIN_PATH
#error "PJ_MOCK_JSON_PARSER_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

namespace {

TEST(MessageParserLibraryTest, LoadMockPlugin) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, PJ_MESSAGE_PARSER_PROTOCOL_VERSION);
}

TEST(MessageParserLibraryTest, RejectsMissingRequiredVtableSlot) {
  auto library = PJ::MessageParserLibrary::load(PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(library);
  EXPECT_NE(library.error().find("MessageParser vtable missing required slot: parse"), std::string::npos);
}

TEST(MessageParserLibraryTest, ManifestRoundTrip) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock JSON Parser"), std::string::npos);
  EXPECT_NE(handle.manifest().find("\"encoding\":[\"json\"]"), std::string::npos);
}

TEST(MessageParserLibraryTest, BindAndParse) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  PJ::sdk::testing::ParserWriteRecorder recorder;

  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::ParserWriteHostService>(recorder.makeHost());

  ASSERT_TRUE(handle.bind(reg.view()));

  const uint8_t payload[] = {'3', '.', '1', '4'};
  ASSERT_TRUE(handle.parse(999, payload));

  ASSERT_EQ(recorder.rows().size(), 1u);
  EXPECT_EQ(recorder.rows()[0].timestamp, 999);
  ASSERT_FALSE(recorder.rows()[0].fields.empty());
  EXPECT_EQ(recorder.rows()[0].fields[0].type, PJ::PrimitiveType::kFloat64);
  EXPECT_DOUBLE_EQ(recorder.rows()[0].fields[0].numeric, 3.14);
}

TEST(MessageParserLibraryTest, SaveLoadConfig) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  std::string cfg;
  ASSERT_TRUE(handle.saveConfig(cfg));
  EXPECT_EQ(cfg, "{}");
  ASSERT_TRUE(handle.loadConfig(R"({"format":"compact"})"));
}

TEST(MessageParserLibraryTest, HandleKeepsSharedLibraryLoadedAfterLibraryObjectDies) {
  std::unique_ptr<PJ::MessageParserHandle> handle;
  {
    auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    handle = std::make_unique<PJ::MessageParserHandle>(library->createHandle());
    ASSERT_TRUE(handle->valid());
  }

  EXPECT_NE(handle->manifest().find("Mock JSON Parser"), std::string::npos);
  std::string cfg;
  EXPECT_TRUE(handle->saveConfig(cfg));
  handle.reset();
}

TEST(MessageParserLibraryTest, LoadNonexistentFails) {
  auto result = PJ::MessageParserLibrary::load("/nonexistent_path/fake_plugin.so");
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

}  // namespace
