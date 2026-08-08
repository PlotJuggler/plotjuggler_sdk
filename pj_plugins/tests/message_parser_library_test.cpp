// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/message_parser_library.hpp"

#include <gtest/gtest.h>

#include <any>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "functional_image_parser_test_protocol.h"
#include "pj_base/builtin/image.hpp"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/testing/parser_write_recorder.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"

#ifndef PJ_MOCK_JSON_PARSER_PLUGIN_PATH
#error "PJ_MOCK_JSON_PARSER_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_FUNCTIONAL_IMAGE_PARSER_PLUGIN_PATH
#error "PJ_FUNCTIONAL_IMAGE_PARSER_PLUGIN_PATH must be defined"
#endif

namespace {

std::atomic_bool functional_plugin_unloaded = false;

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

// GCC 14's -O2 inliner falsely reports the moved-into optional's std::any
// payload as maybe-uninitialized when ~ObjectRecord() is fully inlined
// (observed with GCC 14.4). Clang has no -Wmaybe-uninitialized group.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
TEST(MessageParserLibraryTest, FunctionalObjectRemainsHostOwnedAfterPluginAndLibraryDie) {
  functional_plugin_unloaded = false;
  std::optional<PJ::sdk::ObjectRecord> record;
  {
    auto library = PJ::MessageParserLibrary::load(PJ_FUNCTIONAL_IMAGE_PARSER_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    auto handle = library->createHandle();
    ASSERT_TRUE(handle.supportsFunctionalParsing());
    ASSERT_TRUE(handle.bindSchema("test/Image", {}));
    const auto* unload_observer = static_cast<const PJ_test_unload_observer_v1_t*>(
        handle.getPluginExtension(PJ_FUNCTIONAL_IMAGE_PARSER_UNLOAD_OBSERVER_V1));
    ASSERT_NE(unload_observer, nullptr);
    ASSERT_GE(unload_observer->struct_size, sizeof(PJ_test_unload_observer_v1_t));
    ASSERT_NE(unload_observer->observe, nullptr);
    unload_observer->observe(
        &functional_plugin_unloaded, [](void* ctx) noexcept { static_cast<std::atomic_bool*>(ctx)->store(true); });

    auto payload = PJ::sdk::makePayloadView(std::vector<uint8_t>{4, 3, 2, 1});
    auto parsed = handle.parseObjectFunctional(777, std::move(payload));
    ASSERT_TRUE(parsed) << parsed.error();
    record = std::move(*parsed);
  }  // destroys the plugin instance, handle, library owner, and its dlopen lease

  ASSERT_TRUE(functional_plugin_unloaded.load()) << "test plugin DSO was retained instead of being unloaded";
  ASSERT_TRUE(record.has_value());
  EXPECT_FALSE(record->ts.has_value());
  const auto* image = std::any_cast<PJ::sdk::Image>(&record->object);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->timestamp_ns, 777);
  EXPECT_EQ(image->frame_id, "plugin-owned-frame");
  ASSERT_EQ(image->data.size(), 4U);
  EXPECT_EQ(image->data[0], 4U);
  EXPECT_EQ(image->data[3], 1U);

  record.reset();  // host-side destruction must not jump into the unloaded DSO
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

TEST(MessageParserLibraryTest, LoadNonexistentFails) {
  auto result = PJ::MessageParserLibrary::load("/nonexistent_path/fake_plugin.so");
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

}  // namespace
