// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <pj_plugins/host/dialog_handle.hpp>
#include <pj_plugins/host/widget_data_view.hpp>
#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/sdk/file_picker_types.hpp>
#include <pj_plugins/sdk/tree_types.hpp>
#include <string>
#include <string_view>
#include <vector>

// Defined by dialog_controls.cpp, which is linked statically into this test.
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept;

namespace {

constexpr std::string_view kStack = "example_stack";
constexpr std::string_view kCompatibilityPage = "host_too_old_page";
constexpr std::string_view kBasicPage = "basic_page";
constexpr std::string_view kAdvancedPage = "advanced_page";
constexpr std::string_view kTree = "topic_tree";
constexpr std::string_view kPicker = "open_files_button";
constexpr std::string_view kImuId = "topic:/sensors/imu";
constexpr std::string_view kDiagnosticsId = "group:/diagnostics";

void deliverHostInfo(PJ::DialogHandle& handle, std::string_view sdk_version, std::uint64_t capabilities) {
  static constexpr char kPlotJugglerVersion[] = "4.2.0";
  const PJ_dialog_host_info_t info = {
      static_cast<std::uint32_t>(sizeof(PJ_dialog_host_info_t)),
      PJ_string_view_t{sdk_version.data(), sdk_version.size()},
      PJ_string_view_t{kPlotJugglerVersion, sizeof(kPlotJugglerVersion) - 1},
      capabilities,
  };
  ASSERT_EQ(handle.setHostInfo(info), PJ::SetHostInfoResult::Accepted);
}

const PJ::TreeItem* findTreeItem(const std::vector<PJ::TreeItem>& items, std::string_view id) {
  const auto item =
      std::find_if(items.begin(), items.end(), [id](const PJ::TreeItem& candidate) { return candidate.id == id; });
  return item == items.end() ? nullptr : &*item;
}

PJ::DialogHandle makeModernHandle(std::uint64_t capabilities = PJ_DIALOG_HOST_CAN_OPEN_FILES) {
  PJ::DialogHandle handle(PJ_get_dialog_vtable());
  deliverHostInfo(handle, "0.21.0", capabilities);
  return handle;
}

}  // namespace

TEST(DialogControlsExampleTest, AbsentAndPre021HostUseCompatibilityPage) {
  PJ::DialogHandle absent(PJ_get_dialog_vtable());
  PJ::WidgetDataView absent_state(absent.widget_data());
  EXPECT_EQ(absent_state.stackedPage(kStack), kCompatibilityPage);
  EXPECT_FALSE(absent_state.treeItems(kTree).has_value());
  EXPECT_FALSE(absent_state.isStructuredFilePicker(kPicker));
  EXPECT_EQ(absent_state.okEnabled("buttonBox"), false);

  PJ::DialogHandle old(PJ_get_dialog_vtable());
  deliverHostInfo(old, "0.20.9", PJ_DIALOG_HOST_CAN_OPEN_FILES);
  PJ::WidgetDataView old_state(old.widget_data());
  EXPECT_EQ(old_state.stackedPage(kStack), kCompatibilityPage);
  EXPECT_FALSE(old_state.treeItems(kTree).has_value());
  EXPECT_FALSE(old_state.isStructuredFilePicker(kPicker));

  // 0.21.0-pre is lower than the corresponding release and must also degrade.
  PJ::DialogHandle prerelease(PJ_get_dialog_vtable());
  deliverHostInfo(prerelease, "0.21.0-pre", PJ_DIALOG_HOST_CAN_OPEN_FILES);
  EXPECT_EQ(PJ::WidgetDataView(prerelease.widget_data()).stackedPage(kStack), kCompatibilityPage);
}

TEST(DialogControlsExampleTest, InitialStateComposesValidatedTreeStackAndStructuredPicker) {
  auto handle = makeModernHandle();

  const auto* vtable = handle.vtable();
  ASSERT_TRUE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, vtable, manifest_json));
  ASSERT_NE(vtable->manifest_json, nullptr);
  EXPECT_EQ(nlohmann::json::parse(vtable->manifest_json), nlohmann::json::parse(handle.manifest()));

  const std::string ui = handle.ui_content();
  EXPECT_NE(ui.find("QTreeWidget"), std::string::npos);
  EXPECT_NE(ui.find("QStackedWidget"), std::string::npos);
  EXPECT_NE(ui.find("name=\"buttonBox\""), std::string::npos);
  EXPECT_NE(ui.find("QDialogButtonBox::Cancel|QDialogButtonBox::Ok"), std::string::npos);

  PJ::WidgetDataView state(handle.widget_data());
  EXPECT_EQ(state.stackedPage(kStack), kBasicPage);
  EXPECT_EQ(state.treeHeaders(kTree), (std::vector<std::string>{"Topic", "Type"}));
  EXPECT_EQ(state.treeMultiSelection(kTree), true);
  EXPECT_EQ(state.treeSelectedIds(kTree), (std::vector<std::string>{std::string(kImuId)}));

  std::string tree_error = "stale";
  const auto items = state.treeItems(kTree, &tree_error);
  ASSERT_TRUE(items.has_value()) << tree_error;
  EXPECT_TRUE(tree_error.empty());
  ASSERT_NE(findTreeItem(*items, kImuId), nullptr);
  const auto* diagnostics = findTreeItem(*items, kDiagnosticsId);
  ASSERT_NE(diagnostics, nullptr);
  EXPECT_TRUE(diagnostics->may_have_children);
  EXPECT_EQ(findTreeItem(*items, "topic:/diagnostics/cpu"), nullptr);

  std::string picker_error = "stale";
  const auto picker = state.filePickerOptions(kPicker, &picker_error);
  ASSERT_TRUE(picker.has_value()) << picker_error;
  EXPECT_TRUE(picker_error.empty());
  EXPECT_EQ(picker->mode, PJ::FilePickerMode::OpenFiles);
  ASSERT_EQ(picker->filters.size(), 2U);
  EXPECT_EQ(picker->filters[0].id, "plot_data");
  EXPECT_EQ(picker->filters[1].id, "all_files");
  EXPECT_EQ(picker->initially_selected_filter_id, "plot_data");
  EXPECT_EQ(state.enabled(kPicker), true);

  auto incapable_handle = makeModernHandle(PJ_DIALOG_HOST_CAN_OPEN_FILE);
  PJ::WidgetDataView incapable_state(incapable_handle.widget_data());
  EXPECT_EQ(incapable_state.enabled(kPicker), false);
  ASSERT_TRUE(incapable_state.label("picker_status_label").has_value());
  EXPECT_NE(incapable_state.label("picker_status_label")->find("does not advertise"), std::string::npos);
}

TEST(DialogControlsExampleTest, FilterEventsOnlyUpdateTheIdVisibilityChannel) {
  auto handle = makeModernHandle();
  (void)handle.widget_data();  // Consume the initial complete snapshot.

  ASSERT_TRUE(handle.sendEvent("topic_filter", PJ::WidgetEventBuilder::textChanged("IMU")));
  PJ::WidgetDataView filtered(handle.widget_data());
  std::string tree_error;
  EXPECT_FALSE(filtered.treeItems(kTree, &tree_error).has_value());
  EXPECT_TRUE(tree_error.empty());
  const auto visibility = filtered.treeVisibilityUpdate(kTree);
  ASSERT_TRUE(visibility.has_value());
  EXPECT_EQ(visibility->mode, PJ::TreeVisibilityUpdate::Mode::Filter);
  EXPECT_EQ(visibility->ids, (std::vector<std::string>{std::string(kImuId)}));

  ASSERT_TRUE(handle.sendEvent("topic_filter", PJ::WidgetEventBuilder::textChanged("")));
  PJ::WidgetDataView reset(handle.widget_data());
  EXPECT_FALSE(reset.treeItems(kTree).has_value());
  const auto reset_visibility = reset.treeVisibilityUpdate(kTree);
  ASSERT_TRUE(reset_visibility.has_value());
  EXPECT_EQ(reset_visibility->mode, PJ::TreeVisibilityUpdate::Mode::Reset);
  EXPECT_TRUE(reset_visibility->ids.empty());
}

TEST(DialogControlsExampleTest, TreeEventsRoundTripCheckSelectionAndLazyChildren) {
  auto handle = makeModernHandle();
  (void)handle.widget_data();

  ASSERT_TRUE(
      handle.sendEvent(kTree, PJ::WidgetEventBuilder::treeCheckStateChanged(kImuId, PJ::TreeCheckState::Checked)));
  PJ::WidgetDataView checked_state(handle.widget_data());
  std::string validation_error;
  const auto checked_items = checked_state.treeItems(kTree, &validation_error);
  ASSERT_TRUE(checked_items.has_value()) << validation_error;
  const auto* imu = findTreeItem(*checked_items, kImuId);
  ASSERT_NE(imu, nullptr);
  EXPECT_EQ(imu->check_state, PJ::TreeCheckState::Checked);

  const std::vector<std::string> selected = {std::string(kImuId), "topic:/vehicle/speed"};
  ASSERT_TRUE(handle.sendEvent(kTree, PJ::WidgetEventBuilder::treeSelectionChanged(selected)));
  PJ::WidgetDataView selected_state(handle.widget_data());
  EXPECT_EQ(selected_state.treeSelectedIds(kTree), selected);
  ASSERT_TRUE(selected_state.label("selection_label").has_value());
  EXPECT_NE(selected_state.label("selection_label")->find("/vehicle/speed"), std::string::npos);

  ASSERT_TRUE(handle.sendEvent(kTree, PJ::WidgetEventBuilder::treeExpansionChanged(kDiagnosticsId, true)));
  PJ::WidgetDataView expanded_state(handle.widget_data());
  const auto expanded_items = expanded_state.treeItems(kTree, &validation_error);
  ASSERT_TRUE(expanded_items.has_value()) << validation_error;
  const auto* diagnostics = findTreeItem(*expanded_items, kDiagnosticsId);
  ASSERT_NE(diagnostics, nullptr);
  EXPECT_FALSE(diagnostics->may_have_children);
  EXPECT_NE(findTreeItem(*expanded_items, "topic:/diagnostics/cpu"), nullptr);
  EXPECT_NE(findTreeItem(*expanded_items, "topic:/diagnostics/memory"), nullptr);
  const auto expanded_ids = expanded_state.treeExpandedIds(kTree);
  ASSERT_TRUE(expanded_ids.has_value());
  EXPECT_NE(std::find(expanded_ids->begin(), expanded_ids->end(), kDiagnosticsId), expanded_ids->end());
}

TEST(DialogControlsExampleTest, StackedPageEventsRoundTripStableObjectNames) {
  auto handle = makeModernHandle();
  (void)handle.widget_data();

  ASSERT_TRUE(handle.sendEvent(kStack, PJ::WidgetEventBuilder::stackedPageChanged(2, kAdvancedPage)));
  PJ::WidgetDataView state(handle.widget_data());
  EXPECT_EQ(state.stackedPage(kStack), kAdvancedPage);
  EXPECT_EQ(state.currentIndex("page_selector"), 1);
  ASSERT_TRUE(state.label("stack_event_label").has_value());
  EXPECT_NE(state.label("stack_event_label")->find("advanced_page"), std::string::npos);
}

TEST(DialogControlsExampleTest, PickerResultsDistinguishSelectedCancelledAndUnsupported) {
  auto handle = makeModernHandle();
  (void)handle.widget_data();

  PJ::FilePickerResult selected;
  selected.status = PJ::FilePickerStatus::Selected;
  selected.mode = PJ::FilePickerMode::OpenFiles;
  selected.paths = {"/staged/a.mcap", "/staged/b.mcap"};
  selected.display_names = {"a.mcap", "b.mcap"};
  selected.selected_filter_id = "plot_data";
  ASSERT_TRUE(handle.sendEvent(kPicker, PJ::WidgetEventBuilder::filePickerResult(selected)));
  PJ::WidgetDataView selected_state(handle.widget_data());
  ASSERT_TRUE(selected_state.label("picker_status_label").has_value());
  EXPECT_NE(selected_state.label("picker_status_label")->find("Selected 2 file(s)"), std::string::npos);
  EXPECT_NE(selected_state.label("picker_status_label")->find("filter: plot_data"), std::string::npos);
  EXPECT_EQ(selected_state.enabled(kPicker), true);

  PJ::FilePickerResult cancelled;
  cancelled.status = PJ::FilePickerStatus::Cancelled;
  cancelled.mode = PJ::FilePickerMode::OpenFiles;
  ASSERT_TRUE(handle.sendEvent(kPicker, PJ::WidgetEventBuilder::filePickerResult(cancelled)));
  PJ::WidgetDataView cancelled_state(handle.widget_data());
  ASSERT_TRUE(cancelled_state.label("picker_status_label").has_value());
  EXPECT_NE(cancelled_state.label("picker_status_label")->find("cancelled"), std::string::npos);
  EXPECT_EQ(cancelled_state.enabled(kPicker), true);

  PJ::FilePickerResult unsupported;
  unsupported.status = PJ::FilePickerStatus::Unsupported;
  unsupported.mode = PJ::FilePickerMode::OpenFiles;
  ASSERT_TRUE(handle.sendEvent(kPicker, PJ::WidgetEventBuilder::filePickerResult(unsupported)));
  PJ::WidgetDataView unsupported_state(handle.widget_data());
  ASSERT_TRUE(unsupported_state.label("picker_status_label").has_value());
  EXPECT_NE(unsupported_state.label("picker_status_label")->find("unsupported"), std::string::npos);
  EXPECT_EQ(unsupported_state.enabled(kPicker), false);
}
