// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/dialog_library.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#ifndef PJ_MOCK_DIALOG_PLUGIN_PATH
#error "PJ_MOCK_DIALOG_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MISSING_DIALOG_ABI_PLUGIN_PATH
#error "PJ_MISSING_DIALOG_ABI_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

namespace {

TEST(DialogLibraryTest, LoadAndCreateHandle) {
  auto lib = PJ::DialogLibrary::load(PJ_MOCK_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  EXPECT_TRUE(lib->valid());
  EXPECT_EQ(lib->vtable()->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);

  auto handle = lib->createHandle();
  EXPECT_NE(handle.vtable(), nullptr);
  EXPECT_NE(handle.context(), nullptr);

  std::string manifest = handle.manifest();
  auto j = nlohmann::json::parse(manifest, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_EQ(j["name"], "Mock Dialog");
}

TEST(DialogLibraryTest, HandleLifecycle) {
  auto lib = PJ::DialogLibrary::load(PJ_MOCK_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto handle = lib->createHandle();
  EXPECT_FALSE(handle.ui_content().empty());
  EXPECT_FALSE(handle.widget_data().empty());

  // Config round-trip
  (void)handle.sendEvent("name_input", R"({"text": "test_name"})");
  std::string cfg = handle.save_config();
  auto parsed = nlohmann::json::parse(cfg);
  EXPECT_EQ(parsed["name"], "test_name");

  auto handle2 = lib->createHandle();
  EXPECT_TRUE(handle2.load_config(cfg));
  auto wd = nlohmann::json::parse(handle2.widget_data());
  EXPECT_EQ(wd["name_input"]["text"], "test_name");
}

TEST(DialogLibraryTest, LoadInvalidPath) {
  auto lib = PJ::DialogLibrary::load("/nonexistent/path.so");
  EXPECT_FALSE(lib);
}

TEST(DialogLibraryTest, RejectsMissingAbiVersionSymbol) {
  auto lib = PJ::DialogLibrary::load(PJ_MISSING_DIALOG_ABI_PLUGIN_PATH);
  ASSERT_FALSE(lib);
  EXPECT_NE(lib.error().find("pj_plugin_abi_version"), std::string::npos);
}

TEST(DialogLibraryTest, RejectsMissingRequiredSlot) {
  auto lib = PJ::DialogLibrary::load(PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(lib);
  EXPECT_NE(lib.error().find("Dialog vtable missing required slot: get_ui_content"), std::string::npos);
}

TEST(DialogLibraryTest, HandleKeepsSharedLibraryLoadedAfterLibraryObjectDies) {
  std::unique_ptr<PJ::DialogHandle> handle;
  {
    auto lib = PJ::DialogLibrary::load(PJ_MOCK_DIALOG_PLUGIN_PATH);
    ASSERT_TRUE(lib) << lib.error();
    handle = std::make_unique<PJ::DialogHandle>(lib->createHandle());
    ASSERT_NE(handle->context(), nullptr);
  }

  auto j = nlohmann::json::parse(handle->manifest(), nullptr, false);
  ASSERT_FALSE(j.is_discarded());
  EXPECT_EQ(j["name"], "Mock Dialog");
  handle.reset();
}

TEST(DialogLibraryTest, MoveSemantics) {
  auto lib = PJ::DialogLibrary::load(PJ_MOCK_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  PJ::DialogLibrary moved = std::move(*lib);
  EXPECT_TRUE(moved.valid());
  EXPECT_FALSE(lib->valid());
}

}  // namespace
