// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/dialog_library.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "pj_plugins/host/widget_event_builder.hpp"

#ifndef PJ_MOCK_DIALOG_PLUGIN_PATH
#error "PJ_MOCK_DIALOG_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MISSING_DIALOG_ABI_PLUGIN_PATH
#error "PJ_MISSING_DIALOG_ABI_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_OLD_DIALOG_VTABLE_PLUGIN_PATH
#error "PJ_OLD_DIALOG_VTABLE_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_OLD_FILE_PICKER_DISPATCHER_PLUGIN_PATH
#error "PJ_OLD_FILE_PICKER_DISPATCHER_PLUGIN_PATH must be defined"
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

TEST(DialogLibraryTest, AcceptsRequiredPrefixOnlyVtableAndGatesOptionalTailSlots) {
  auto lib = PJ::DialogLibrary::load(PJ_OLD_DIALOG_VTABLE_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  ASSERT_NE(lib->vtable(), nullptr);
  EXPECT_EQ(lib->vtable()->struct_size, PJ_DIALOG_MIN_VTABLE_SIZE);
  EXPECT_EQ(PJ_DIALOG_MIN_VTABLE_SIZE, offsetof(PJ_dialog_vtable_t, manifest_json));
  EXPECT_FALSE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, lib->vtable(), manifest_json));
  EXPECT_FALSE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, lib->vtable(), set_host_info));

  auto handle = lib->createHandle();
  EXPECT_EQ(handle.manifest(), R"({"id":"old-dialog-vtable","name":"Old Dialog Vtable","version":"1.0.0"})");

  const PJ_dialog_host_info_t info{
      static_cast<uint32_t>(sizeof(PJ_dialog_host_info_t)),
      PJ_string_view_t{"0.21.0", 6},
      PJ_string_view_t{"4.2.0", 5},
      PJ_DIALOG_HOST_CAN_OPEN_FILE,
  };
  PJ_error_t error{};
  error.code = 91;
  EXPECT_EQ(handle.setHostInfo(info, &error), PJ::SetHostInfoResult::Unsupported);
  EXPECT_EQ(error.code, 91);
}

TEST(DialogLibraryTest, OldBinaryFilePickerDispatcherSeesSelectedLegacyKeyAndOtherStatusesStaySilent) {
  auto lib = PJ::DialogLibrary::load(PJ_OLD_FILE_PICKER_DISPATCHER_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  auto handle = lib->createHandle();

  PJ::FilePickerResult result;
  result.status = PJ::FilePickerStatus::Selected;
  result.paths = {"/data/a.mcap", "/data/b.mcap"};
  result.display_names = {"a.mcap", "b.mcap"};
  EXPECT_TRUE(
      handle.sendEvent("bags", PJ::WidgetEventBuilder::filePickerResult(PJ::FilePickerMode::OpenFiles, result)));
  auto observed = nlohmann::json::parse(handle.widget_data());
  EXPECT_EQ(observed["legacy_calls"], 1);
  EXPECT_EQ(observed["callback"], "file_selected");
  EXPECT_EQ(observed["path"], "/data/a.mcap");

  result.paths = {"/data/bags"};
  result.display_names = {"bags"};
  EXPECT_TRUE(handle.sendEvent(
      "folder", PJ::WidgetEventBuilder::filePickerResult(PJ::FilePickerMode::SelectDirectory, result)));
  observed = nlohmann::json::parse(handle.widget_data());
  EXPECT_EQ(observed["legacy_calls"], 2);
  EXPECT_EQ(observed["callback"], "folder_selected");
  EXPECT_EQ(observed["path"], "/data/bags");

  for (const auto status :
       {PJ::FilePickerStatus::Cancelled, PJ::FilePickerStatus::Unsupported, PJ::FilePickerStatus::Error}) {
    result = {};
    result.status = status;
    result.error = status == PJ::FilePickerStatus::Error ? "failed" : "";
    EXPECT_FALSE(handle.sendEvent("picker", PJ::WidgetEventBuilder::filePickerResult(result)));
  }
  observed = nlohmann::json::parse(handle.widget_data());
  EXPECT_EQ(observed["legacy_calls"], 2);
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
