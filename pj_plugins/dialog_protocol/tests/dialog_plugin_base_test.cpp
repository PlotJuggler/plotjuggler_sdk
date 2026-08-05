// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <pj_plugins/sdk/dialog_plugin_base.hpp>
#include <string>
#include <string_view>

namespace {

static_assert(
    static_cast<uint64_t>(PJ::DialogHostCapability::kCanOpenFile) == PJ_DIALOG_HOST_CAN_OPEN_FILE,
    "C++ capability mirrors the C ABI");
static_assert(
    static_cast<uint64_t>(PJ::DialogHostCapability::kCanOpenFiles) == PJ_DIALOG_HOST_CAN_OPEN_FILES,
    "C++ capability mirrors the C ABI");
static_assert(
    static_cast<uint64_t>(PJ::DialogHostCapability::kCanSaveFilePath) == PJ_DIALOG_HOST_CAN_SAVE_FILE_PATH,
    "C++ capability mirrors the C ABI");
static_assert(
    static_cast<uint64_t>(PJ::DialogHostCapability::kCanSelectFolder) == PJ_DIALOG_HOST_CAN_SELECT_FOLDER,
    "C++ capability mirrors the C ABI");
static_assert(
    static_cast<uint64_t>(PJ::DialogHostCapability::kStagesBrowserFile) == PJ_DIALOG_HOST_STAGES_BROWSER_FILE,
    "C++ capability mirrors the C ABI");

class HostInfoDialog final : public PJ::DialogPluginBase {
 public:
  std::string manifest() const override {
    return R"({"id":"host-info-test","name":"Host Info Test","version":"1.0.0"})";
  }

  std::string ui_content() const override {
    return "<ui/>";
  }

  std::string widget_data() override {
    return "{}";
  }

  bool onWidgetEvent(std::string_view, std::string_view) override {
    return false;
  }

  [[nodiscard]] const std::optional<PJ::DialogHostInfo>& observedHostInfo() const noexcept {
    return hostInfo();
  }
};

class DialogPluginBaseHostInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    vtable_ = PJ::DialogPluginBase::vtableWithCreate([]() noexcept -> void* {
      try {
        return static_cast<PJ::DialogPluginBase*>(new HostInfoDialog());
      } catch (...) {
        return nullptr;
      }
    });
    ASSERT_NE(vtable_, nullptr);
    ASSERT_TRUE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, vtable_, set_host_info));
    context_ = vtable_->create();
    ASSERT_NE(context_, nullptr);
  }

  void TearDown() override {
    if (context_ != nullptr) {
      vtable_->destroy(context_);
    }
  }

  [[nodiscard]] HostInfoDialog& plugin() const {
    return *static_cast<HostInfoDialog*>(static_cast<PJ::DialogPluginBase*>(context_));
  }

  [[nodiscard]] bool deliver(
      std::string_view sdk_version, std::string_view plotjuggler_version, uint64_t capabilities) const {
    const PJ_dialog_host_info_t info{
        static_cast<uint32_t>(sizeof(PJ_dialog_host_info_t)),
        PJ_string_view_t{sdk_version.data(), sdk_version.size()},
        PJ_string_view_t{plotjuggler_version.data(), plotjuggler_version.size()},
        capabilities,
    };
    PJ_error_t error{};
    return vtable_->set_host_info(context_, &info, &error);
  }

  const PJ_dialog_vtable_t* vtable_ = nullptr;
  void* context_ = nullptr;
};

TEST_F(DialogPluginBaseHostInfoTest, HostInfoIsEmptyBeforeDelivery) {
  EXPECT_FALSE(plugin().observedHostInfo().has_value());
}

TEST_F(DialogPluginBaseHostInfoTest, CopiesStringsAndCapabilitiesDuringDelivery) {
  {
    std::string sdk_version = "0.21.0-backed-by-temporary-storage";
    std::string plotjuggler_version = "4.2.0-backed-by-temporary-storage";
    ASSERT_TRUE(
        deliver(sdk_version, plotjuggler_version, PJ_DIALOG_HOST_CAN_OPEN_FILE | PJ_DIALOG_HOST_STAGES_BROWSER_FILE));
  }

  const auto& info = plugin().observedHostInfo();
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->sdk_version, "0.21.0-backed-by-temporary-storage");
  EXPECT_EQ(info->plotjuggler_version, "4.2.0-backed-by-temporary-storage");
  EXPECT_TRUE(info->has(PJ::DialogHostCapability::kCanOpenFile));
  EXPECT_TRUE(info->has(PJ::DialogHostCapability::kStagesBrowserFile));
  EXPECT_FALSE(info->has(PJ::DialogHostCapability::kCanOpenFiles));
  EXPECT_FALSE(info->has(PJ::DialogHostCapability::kCanSaveFilePath));
  EXPECT_FALSE(info->has(PJ::DialogHostCapability::kCanSelectFolder));
}

TEST_F(DialogPluginBaseHostInfoTest, RepeatedDeliveryIsLastWriterWins) {
  ASSERT_TRUE(deliver("0.21.0", "4.1.0", PJ_DIALOG_HOST_CAN_OPEN_FILE));
  ASSERT_TRUE(deliver("0.22.0", "4.2.0", PJ_DIALOG_HOST_CAN_SELECT_FOLDER));

  const auto& info = plugin().observedHostInfo();
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->sdk_version, "0.22.0");
  EXPECT_EQ(info->plotjuggler_version, "4.2.0");
  EXPECT_FALSE(info->has(PJ::DialogHostCapability::kCanOpenFile));
  EXPECT_TRUE(info->has(PJ::DialogHostCapability::kCanSelectFolder));
}

}  // namespace
