#include "pj_plugins/host/dialog_library.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

#ifndef PJ_MOCK_DIALOG_PLUGIN_PATH
#error "PJ_MOCK_DIALOG_PLUGIN_PATH must be defined"
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

TEST(DialogLibraryTest, MoveSemantics) {
  auto lib = PJ::DialogLibrary::load(PJ_MOCK_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  PJ::DialogLibrary moved = std::move(*lib);
  EXPECT_TRUE(moved.valid());
  EXPECT_FALSE(lib->valid());
}

}  // namespace
