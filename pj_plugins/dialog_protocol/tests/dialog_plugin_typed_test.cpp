// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
#include <utility>
#include <vector>

namespace {

/// A recording subclass that tracks which typed handler was called and with what value.
class RecordingPlugin : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return R"({"name":"test"})";
  }
  std::string ui_content() const override {
    return "<ui/>";
  }
  std::string widget_data() override {
    return "{}";
  }

  // --- Recording typed handlers ---

  bool onTextChanged(std::string_view widget_name, std::string_view text) override {
    last_handler = "text_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(text);
    return true;
  }

  bool onIndexChanged(std::string_view widget_name, int index) override {
    last_handler = "index_changed";
    last_widget = std::string(widget_name);
    last_int = index;
    return true;
  }

  bool onToggled(std::string_view widget_name, bool checked) override {
    last_handler = "toggled";
    last_widget = std::string(widget_name);
    last_bool = checked;
    return true;
  }

  bool onValueChanged(std::string_view widget_name, int value) override {
    last_handler = "value_int";
    last_widget = std::string(widget_name);
    last_int = value;
    return true;
  }

  bool onValueChanged(std::string_view widget_name, double value) override {
    last_handler = "value_double";
    last_widget = std::string(widget_name);
    last_double = value;
    return true;
  }

  bool onSelectionChanged(std::string_view widget_name, const std::vector<std::string>& selected) override {
    last_handler = "selection_changed";
    last_widget = std::string(widget_name);
    last_strings = selected;
    return true;
  }

  bool onClicked(std::string_view widget_name) override {
    last_handler = "clicked";
    last_widget = std::string(widget_name);
    return true;
  }

  bool onFileSelected(std::string_view widget_name, std::string_view path) override {
    ++file_selected_calls;
    last_handler = "file_selected";
    last_widget = std::string(widget_name);
    last_text = std::string(path);
    return true;
  }

  bool onFolderSelected(std::string_view widget_name, std::string_view path) override {
    ++folder_selected_calls;
    last_handler = "folder_selected";
    last_widget = std::string(widget_name);
    last_text = std::string(path);
    return true;
  }

  bool onTabChanged(std::string_view widget_name, int index) override {
    last_handler = "tab_changed";
    last_widget = std::string(widget_name);
    last_int = index;
    return true;
  }

  bool onStackedPageChanged(std::string_view widget_name, int index, std::string_view page_object_name) override {
    ++stacked_calls;
    last_handler = "stacked_page_changed";
    last_widget = std::string(widget_name);
    last_int = index;
    last_page = std::string(page_object_name);
    return true;
  }

  bool onTreeSelectionChanged(std::string_view widget_name, const std::vector<std::string>& ids) override {
    ++tree_calls;
    last_handler = "tree_selection_changed";
    last_widget = std::string(widget_name);
    last_strings = ids;
    return true;
  }

  bool onTreeItemActivated(std::string_view widget_name, std::string_view id, int column) override {
    ++tree_calls;
    last_handler = "tree_item_activated";
    last_widget = std::string(widget_name);
    last_text = std::string(id);
    last_int = column;
    return true;
  }

  bool onTreeExpansionChanged(std::string_view widget_name, std::string_view id, bool expanded) override {
    ++tree_calls;
    last_handler = "tree_expansion_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(id);
    last_bool = expanded;
    return true;
  }

  bool onTreeCheckStateChanged(std::string_view widget_name, std::string_view id, PJ::TreeCheckState state) override {
    ++tree_calls;
    last_handler = "tree_check_state_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(id);
    last_tree_check_state = state;
    return true;
  }

  bool onFilePickerResult(std::string_view widget_name, const PJ::FilePickerResult& result) override {
    ++file_picker_result_calls;
    last_handler = "file_picker_result";
    last_widget = std::string(widget_name);
    last_file_picker_result = result;
    return true;
  }

  bool onDateRangeChanged(std::string_view widget_name, std::string_view from_iso, std::string_view to_iso) override {
    last_handler = "date_range_changed";
    last_widget = std::string(widget_name);
    last_date_from = std::string(from_iso);
    last_date_to = std::string(to_iso);
    return true;
  }

  bool onDateTimeChanged(std::string_view widget_name, std::string_view iso8601) override {
    last_handler = "date_time_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(iso8601);
    return true;
  }

  bool onCodeChangedWithCursor(std::string_view widget_name, std::string_view code, int cursor) override {
    last_handler = "code_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(code);
    last_int = cursor;
    return true;
  }

  bool onItemDeleteRequested(std::string_view widget_name, int index) override {
    last_handler = "item_delete_requested";
    last_widget = std::string(widget_name);
    last_int = index;
    return true;
  }

  // Recorded state
  std::string last_handler;
  std::string last_widget;
  std::string last_text;
  std::string last_date_from;
  std::string last_date_to;
  std::string last_page;
  int last_int = -1;
  int stacked_calls = 0;
  int tree_calls = 0;
  int file_picker_result_calls = 0;
  int file_selected_calls = 0;
  int folder_selected_calls = 0;
  double last_double = -1.0;
  bool last_bool = false;
  PJ::TreeCheckState last_tree_check_state = PJ::TreeCheckState::None;
  PJ::FilePickerResult last_file_picker_result;
  std::vector<std::string> last_strings;

  void reset() {
    last_handler.clear();
    last_widget.clear();
    last_text.clear();
    last_date_from.clear();
    last_date_to.clear();
    last_page.clear();
    last_int = -1;
    stacked_calls = 0;
    tree_calls = 0;
    file_picker_result_calls = 0;
    file_selected_calls = 0;
    folder_selected_calls = 0;
    last_double = -1.0;
    last_bool = false;
    last_tree_check_state = PJ::TreeCheckState::None;
    last_file_picker_result = {};
    last_strings.clear();
  }
};

/// Leaves stacked/tree callbacks at their defaults while recording every
/// legacy typed callback, so fail-closed dispatch cannot hide a misroute.
class LegacyHandlerRecordingPlugin : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return R"({"name":"legacy-handler-recording-test"})";
  }
  std::string ui_content() const override {
    return "<ui/>";
  }
  std::string widget_data() override {
    return "{}";
  }

  bool onTextChanged(std::string_view /*widget_name*/, std::string_view /*text*/) override {
    return recordLegacyCall();
  }
  bool onIndexChanged(std::string_view /*widget_name*/, int /*index*/) override {
    return recordLegacyCall();
  }
  bool onToggled(std::string_view /*widget_name*/, bool /*checked*/) override {
    return recordLegacyCall();
  }
  bool onValueChanged(std::string_view /*widget_name*/, int /*value*/) override {
    return recordLegacyCall();
  }
  bool onValueChanged(std::string_view /*widget_name*/, double /*value*/) override {
    return recordLegacyCall();
  }
  bool onSelectionChanged(std::string_view /*widget_name*/, const std::vector<std::string>& /*selected*/) override {
    return recordLegacyCall();
  }
  bool onClicked(std::string_view /*widget_name*/) override {
    return recordLegacyCall();
  }
  bool onFileSelected(std::string_view /*widget_name*/, std::string_view /*path*/) override {
    return recordLegacyCall();
  }
  bool onFolderSelected(std::string_view /*widget_name*/, std::string_view /*path*/) override {
    return recordLegacyCall();
  }
  bool onTabChanged(std::string_view /*widget_name*/, int /*index*/) override {
    return recordLegacyCall();
  }
  bool onItemDoubleClicked(std::string_view /*widget_name*/, int /*index*/) override {
    return recordLegacyCall();
  }
  bool onItemDeleteRequested(std::string_view /*widget_name*/, int /*index*/) override {
    return recordLegacyCall();
  }
  bool onHeaderClicked(std::string_view /*widget_name*/, int /*section*/) override {
    return recordLegacyCall();
  }
  bool onTableRadioSelected(std::string_view /*widget_name*/, int /*row*/) override {
    return recordLegacyCall();
  }
  bool onCodeChanged(std::string_view /*widget_name*/, std::string_view /*code*/) override {
    return recordLegacyCall();
  }
  bool onCodeChangedWithCursor(std::string_view /*widget_name*/, std::string_view /*code*/, int /*cursor*/) override {
    return recordLegacyCall();
  }
  bool onItemsDropped(std::string_view /*widget_name*/, const std::vector<std::string>& /*items*/) override {
    return recordLegacyCall();
  }
  bool onChartViewChanged(
      std::string_view /*widget_name*/, double /*x_min*/, double /*x_max*/, double /*y_min*/,
      double /*y_max*/) override {
    return recordLegacyCall();
  }
  bool onRangeChanged(std::string_view /*widget_name*/, int /*lower*/, int /*upper*/) override {
    return recordLegacyCall();
  }
  bool onMarkerTimelineChanged(
      std::string_view /*widget_name*/, const std::vector<PJ::TimelineMark>& /*marks*/) override {
    return recordLegacyCall();
  }
  bool onDateRangeChanged(
      std::string_view /*widget_name*/, std::string_view /*from_iso*/, std::string_view /*to_iso*/) override {
    return recordLegacyCall();
  }
  bool onDateTimeChanged(std::string_view /*widget_name*/, std::string_view /*iso8601*/) override {
    return recordLegacyCall();
  }

  int legacy_calls = 0;

 private:
  bool recordLegacyCall() {
    ++legacy_calls;
    return true;
  }
};

/// Uses the SDK's default onFilePickerResult bridge and records only the two
/// legacy destinations, matching a typed plugin recompiled without adopting
/// the new callback.
class DefaultFilePickerBridgePlugin : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return R"({"name":"default-file-picker-bridge-test"})";
  }
  std::string ui_content() const override {
    return "<ui/>";
  }
  std::string widget_data() override {
    return "{}";
  }

  bool onFileSelected(std::string_view widget_name, std::string_view path) override {
    ++file_calls;
    last_widget = std::string(widget_name);
    last_path = std::string(path);
    return true;
  }

  bool onFolderSelected(std::string_view widget_name, std::string_view path) override {
    ++folder_calls;
    last_widget = std::string(widget_name);
    last_path = std::string(path);
    return true;
  }

  int file_calls = 0;
  int folder_calls = 0;
  std::string last_widget;
  std::string last_path;
};

/// Observes every structured callback, then explicitly delegates to the SDK
/// default so non-selected status tests can prove that no legacy callback runs.
class DelegatingFilePickerBridgePlugin : public DefaultFilePickerBridgePlugin {
 public:
  bool onFilePickerResult(std::string_view widget_name, const PJ::FilePickerResult& result) override {
    ++result_calls;
    last_status = result.status;
    return PJ::DialogPluginTyped::onFilePickerResult(widget_name, result);
  }

  int result_calls = 0;
  PJ::FilePickerStatus last_status = PJ::FilePickerStatus::Selected;
};

// Helper: call the base class on_widget_event through the public interface.
// DialogPluginTyped::on_widget_event is final, but we access it via DialogPluginBase ref.
bool dispatch(PJ::DialogPluginBase& plugin, std::string_view widget, std::string_view json) {
  return plugin.onWidgetEvent(widget, json);
}

}  // namespace

class TypedDispatchTest : public ::testing::Test {
 protected:
  RecordingPlugin plugin_;
};

// --- Individual dispatch tests ---

TEST_F(TypedDispatchTest, TextChanged) {
  EXPECT_TRUE(dispatch(plugin_, "my_input", R"({"text": "hello"})"));
  EXPECT_EQ(plugin_.last_handler, "text_changed");
  EXPECT_EQ(plugin_.last_widget, "my_input");
  EXPECT_EQ(plugin_.last_text, "hello");
}

TEST_F(TypedDispatchTest, IndexChanged) {
  EXPECT_TRUE(dispatch(plugin_, "combo", R"({"current_index": 3})"));
  EXPECT_EQ(plugin_.last_handler, "index_changed");
  EXPECT_EQ(plugin_.last_int, 3);
}

TEST_F(TypedDispatchTest, Toggled) {
  EXPECT_TRUE(dispatch(plugin_, "checkbox", R"({"checked": true})"));
  EXPECT_EQ(plugin_.last_handler, "toggled");
  EXPECT_TRUE(plugin_.last_bool);
}

TEST_F(TypedDispatchTest, ToggledFalse) {
  EXPECT_TRUE(dispatch(plugin_, "checkbox", R"({"checked": false})"));
  EXPECT_EQ(plugin_.last_handler, "toggled");
  EXPECT_FALSE(plugin_.last_bool);
}

TEST_F(TypedDispatchTest, ValueInt) {
  EXPECT_TRUE(dispatch(plugin_, "spinbox", R"({"value": 42})"));
  EXPECT_EQ(plugin_.last_handler, "value_int");
  EXPECT_EQ(plugin_.last_int, 42);
}

TEST_F(TypedDispatchTest, ValueDouble) {
  EXPECT_TRUE(dispatch(plugin_, "dspinbox", R"({"value": 3.14})"));
  EXPECT_EQ(plugin_.last_handler, "value_double");
  EXPECT_DOUBLE_EQ(plugin_.last_double, 3.14);
}

TEST_F(TypedDispatchTest, Clicked) {
  EXPECT_TRUE(dispatch(plugin_, "btn", R"({"clicked": true})"));
  EXPECT_EQ(plugin_.last_handler, "clicked");
  EXPECT_EQ(plugin_.last_widget, "btn");
}

TEST_F(TypedDispatchTest, FileSelected) {
  EXPECT_TRUE(dispatch(plugin_, "file_btn", R"({"file_selected": "/tmp/data.csv"})"));
  EXPECT_EQ(plugin_.last_handler, "file_selected");
  EXPECT_EQ(plugin_.last_text, "/tmp/data.csv");
}

TEST_F(TypedDispatchTest, FolderSelectedLegacyOnlyStillDispatches) {
  EXPECT_TRUE(dispatch(plugin_, "folder_btn", R"({"folder_selected": "/tmp/data"})"));
  EXPECT_EQ(plugin_.last_handler, "folder_selected");
  EXPECT_EQ(plugin_.last_text, "/tmp/data");
  EXPECT_EQ(plugin_.folder_selected_calls, 1);
  EXPECT_EQ(plugin_.file_picker_result_calls, 0);
}

TEST_F(TypedDispatchTest, OverriddenFilePickerResultGetsExactlyOneNewCallbackAndNoLegacyCallback) {
  EXPECT_TRUE(dispatch(
      plugin_, "bags",
      R"({"file_picker_result":{"status":"selected","mode":"open_files","paths":["/a.mcap","/b.mcap"],"display_names":["a.mcap","b.mcap"],"selected_filter_id":"bags","error":""},"file_selected":"/a.mcap"})"));
  EXPECT_EQ(plugin_.file_picker_result_calls, 1);
  EXPECT_EQ(plugin_.file_selected_calls, 0);
  EXPECT_EQ(plugin_.folder_selected_calls, 0);
  EXPECT_EQ(plugin_.last_handler, "file_picker_result");
  EXPECT_EQ(plugin_.last_widget, "bags");
  EXPECT_EQ(plugin_.last_file_picker_result.status, PJ::FilePickerStatus::Selected);
  EXPECT_EQ(plugin_.last_file_picker_result.mode, PJ::FilePickerMode::OpenFiles);
  EXPECT_EQ(plugin_.last_file_picker_result.paths, (std::vector<std::string>{"/a.mcap", "/b.mcap"}));
}

TEST(TypedDispatchFilePickerBridgeTest, DefaultHandlerForwardsFirstFilePathExactlyOnce) {
  DefaultFilePickerBridgePlugin plugin;
  EXPECT_TRUE(dispatch(
      plugin, "bags",
      R"({"file_picker_result":{"status":"selected","mode":"open_files","paths":["/a.mcap","/b.mcap"],"display_names":["a.mcap","b.mcap"],"selected_filter_id":"bags","error":""}})"));
  EXPECT_EQ(plugin.file_calls, 1);
  EXPECT_EQ(plugin.folder_calls, 0);
  EXPECT_EQ(plugin.last_widget, "bags");
  EXPECT_EQ(plugin.last_path, "/a.mcap");
}

TEST(TypedDispatchFilePickerBridgeTest, DefaultHandlerForwardsDirectoryPathExactlyOnce) {
  DefaultFilePickerBridgePlugin plugin;
  EXPECT_TRUE(dispatch(
      plugin, "folder",
      R"({"file_picker_result":{"status":"selected","mode":"select_directory","paths":["/data"],"display_names":["data"],"selected_filter_id":"","error":""}})"));
  EXPECT_EQ(plugin.file_calls, 0);
  EXPECT_EQ(plugin.folder_calls, 1);
  EXPECT_EQ(plugin.last_widget, "folder");
  EXPECT_EQ(plugin.last_path, "/data");
}

TEST(TypedDispatchFilePickerBridgeTest, NonSelectedStatusesReachNewHandlerButDoNotInvokeLegacyHandlers) {
  DelegatingFilePickerBridgePlugin plugin;
  const std::vector<std::pair<std::string, PJ::FilePickerStatus>> cases = {
      {R"({"file_picker_result":{"status":"cancelled","mode":"open_file","paths":[],"display_names":[],"selected_filter_id":"","error":""}})",
       PJ::FilePickerStatus::Cancelled},
      {R"({"file_picker_result":{"status":"unsupported","mode":"select_directory","paths":[],"display_names":[],"selected_filter_id":"","error":"not available"}})",
       PJ::FilePickerStatus::Unsupported},
      {R"({"file_picker_result":{"status":"error","mode":"save_file","paths":[],"display_names":[],"selected_filter_id":"","error":"failed"}})",
       PJ::FilePickerStatus::Error},
  };
  for (const auto& [json, status] : cases) {
    SCOPED_TRACE(json);
    EXPECT_FALSE(dispatch(plugin, "picker", json));
    EXPECT_EQ(plugin.last_status, status);
  }
  EXPECT_EQ(plugin.result_calls, 3);
  EXPECT_EQ(plugin.file_calls, 0);
  EXPECT_EQ(plugin.folder_calls, 0);
}

TEST_F(TypedDispatchTest, FilePickerResultToleratesUnknownAdditionalKeys) {
  EXPECT_TRUE(dispatch(
      plugin_, "picker",
      R"({"file_picker_result":{"status":"selected","mode":"open_file","paths":["/new"]},"file_selected":"/new","future_metadata":{"revision":2}})"));
  EXPECT_EQ(plugin_.file_picker_result_calls, 1);
  EXPECT_EQ(plugin_.file_selected_calls, 0);
  EXPECT_EQ(plugin_.last_file_picker_result.paths, (std::vector<std::string>{"/new"}));
}

TEST_F(TypedDispatchTest, FilePickerResultMalformedOrMixedPayloadsFailClosedWithoutFallthrough) {
  const std::vector<std::string> malformed = {
      R"({"file_picker_result":17,"file_selected":"/legacy"})",
      R"({"file_picker_result":{"status":"selected","paths":["/new"]},"file_selected":"/legacy"})",
      R"({"file_picker_result":{"status":"selected","mode":"open_file","paths":[]},"file_selected":"/legacy"})",
      R"({"file_picker_result":{"status":"selected","mode":"open_file","paths":["/new"]},"file_selected":"/legacy"})",
      R"({"file_picker_result":{"status":"selected","mode":"open_file","paths":["/new"]},"folder_selected":17})",
      R"({"file_picker_result":{"status":"broken","mode":"open_file","paths":[]},"stacked_index":2,"stacked_page":"legacy"})",
      R"({"file_picker_result":{"status":"cancelled","mode":"open_file","paths":[]},"file_selected":"/legacy"})",
  };
  for (const auto& json : malformed) {
    SCOPED_TRACE(json);
    plugin_.reset();
    EXPECT_FALSE(dispatch(plugin_, "picker", json));
    EXPECT_EQ(plugin_.file_picker_result_calls, 0);
    EXPECT_EQ(plugin_.file_selected_calls, 0);
    EXPECT_EQ(plugin_.folder_selected_calls, 0);
    EXPECT_EQ(plugin_.tree_calls, 0);
    EXPECT_EQ(plugin_.stacked_calls, 0);
    EXPECT_TRUE(plugin_.last_handler.empty());
  }
}

TEST_F(TypedDispatchTest, SelectionChanged) {
  EXPECT_TRUE(dispatch(plugin_, "list", R"({"selected_items": ["a", "b"]})"));
  EXPECT_EQ(plugin_.last_handler, "selection_changed");
  ASSERT_EQ(plugin_.last_strings.size(), 2u);
  EXPECT_EQ(plugin_.last_strings[0], "a");
  EXPECT_EQ(plugin_.last_strings[1], "b");
}

TEST_F(TypedDispatchTest, TabChanged) {
  EXPECT_TRUE(dispatch(plugin_, "tabs", R"({"tab_index": 2})"));
  EXPECT_EQ(plugin_.last_handler, "tab_changed");
  EXPECT_EQ(plugin_.last_int, 2);
}

TEST_F(TypedDispatchTest, StackedPageChangedFiresExactlyOnceWithBothValues) {
  EXPECT_TRUE(dispatch(plugin_, "configuration_stack", R"({"stacked_index": 2, "stacked_page": "advanced_page"})"));
  EXPECT_EQ(plugin_.stacked_calls, 1);
  EXPECT_EQ(plugin_.last_handler, "stacked_page_changed");
  EXPECT_EQ(plugin_.last_widget, "configuration_stack");
  EXPECT_EQ(plugin_.last_int, 2);
  EXPECT_EQ(plugin_.last_page, "advanced_page");
}

TEST_F(TypedDispatchTest, TreeSelectionChangedDispatchesExactlyOnceWithCompleteLogicalSet) {
  EXPECT_TRUE(dispatch(plugin_, "tree", R"({"tree_selection_changed":["visible","filtered-out"]})"));
  EXPECT_EQ(plugin_.tree_calls, 1);
  EXPECT_EQ(plugin_.last_handler, "tree_selection_changed");
  EXPECT_EQ(plugin_.last_widget, "tree");
  EXPECT_EQ(plugin_.last_strings, (std::vector<std::string>{"visible", "filtered-out"}));
}

TEST_F(TypedDispatchTest, TreeItemActivatedDispatchesExactlyOnce) {
  EXPECT_TRUE(dispatch(plugin_, "tree", R"({"tree_item_activated":{"id":"imu","column":1}})"));
  EXPECT_EQ(plugin_.tree_calls, 1);
  EXPECT_EQ(plugin_.last_handler, "tree_item_activated");
  EXPECT_EQ(plugin_.last_widget, "tree");
  EXPECT_EQ(plugin_.last_text, "imu");
  EXPECT_EQ(plugin_.last_int, 1);
}

TEST_F(TypedDispatchTest, TreeExpansionChangedDispatchesExactlyOnce) {
  EXPECT_TRUE(dispatch(plugin_, "tree", R"({"tree_expansion_changed":{"id":"sensors","expanded":true}})"));
  EXPECT_EQ(plugin_.tree_calls, 1);
  EXPECT_EQ(plugin_.last_handler, "tree_expansion_changed");
  EXPECT_EQ(plugin_.last_text, "sensors");
  EXPECT_TRUE(plugin_.last_bool);
}

TEST_F(TypedDispatchTest, TreeCheckStateChangedDispatchesExactlyOnce) {
  EXPECT_TRUE(dispatch(plugin_, "tree", R"({"tree_check_state_changed":{"id":"imu","state":"checked"}})"));
  EXPECT_EQ(plugin_.tree_calls, 1);
  EXPECT_EQ(plugin_.last_handler, "tree_check_state_changed");
  EXPECT_EQ(plugin_.last_text, "imu");
  EXPECT_EQ(plugin_.last_tree_check_state, PJ::TreeCheckState::Checked);
}

TEST_F(TypedDispatchTest, NonTreeEventsNeverTouchTreeCallbacks) {
  EXPECT_TRUE(dispatch(plugin_, "list", R"({"selected_items":["a"]})"));
  EXPECT_EQ(plugin_.tree_calls, 0);
  EXPECT_EQ(plugin_.last_handler, "selection_changed");

  plugin_.reset();
  EXPECT_TRUE(dispatch(plugin_, "tabs", R"({"tab_index":2})"));
  EXPECT_EQ(plugin_.tree_calls, 0);
  EXPECT_EQ(plugin_.last_handler, "tab_changed");

  plugin_.reset();
  EXPECT_TRUE(dispatch(plugin_, "combo", R"({"current_index":3})"));
  EXPECT_EQ(plugin_.tree_calls, 0);
  EXPECT_EQ(plugin_.last_handler, "index_changed");
}

TEST(TypedDispatchTreeFailClosedTest, MalformedAndMixedTreePayloadsInvokeNoOtherHandler) {
  LegacyHandlerRecordingPlugin plugin;
  EXPECT_FALSE(dispatch(plugin, "tree", R"({"tree_selection_changed":["ok",2],"text":"legacy"})"));
  EXPECT_FALSE(dispatch(plugin, "tree", R"({"tree_item_activated":{"id":"imu"},"clicked":true})"));
  EXPECT_FALSE(
      dispatch(plugin, "tree", R"({"tree_expansion_changed":{"id":"sensors","expanded":"yes"},"current_index":2})"));
  EXPECT_FALSE(
      dispatch(plugin, "tree", R"({"tree_check_state_changed":{"id":"imu","state":"partial"},"checked":true})"));
  EXPECT_FALSE(
      dispatch(plugin, "tree", R"({"tree_selection_changed":["imu"],"tree_item_activated":{"id":"imu","column":0}})"));
  EXPECT_FALSE(dispatch(
      plugin, "tree",
      R"({"tree_item_activated":{"id":"imu","column":0},"stacked_index":2,"stacked_page":"advanced"})"));
  EXPECT_EQ(plugin.legacy_calls, 0);
}

TEST_F(TypedDispatchTest, MalformedTreePayloadsInvokeNoTreeCallback) {
  EXPECT_FALSE(dispatch(plugin_, "tree", R"({"tree_selection_changed":"imu"})"));
  EXPECT_FALSE(dispatch(plugin_, "tree", R"({"tree_item_activated":{"id":"imu","column":-1}})"));
  EXPECT_FALSE(dispatch(plugin_, "tree", R"({"tree_item_activated":{"id":"imu","column":4294967296}})"));
  EXPECT_FALSE(dispatch(plugin_, "tree", R"({"tree_expansion_changed":{"id":"","expanded":true}})"));
  EXPECT_FALSE(dispatch(plugin_, "tree", R"({"tree_check_state_changed":{"id":"imu","state":"partial"}})"));
  EXPECT_EQ(plugin_.tree_calls, 0);
  EXPECT_TRUE(plugin_.last_handler.empty());
}

TEST_F(TypedDispatchTest, CombinedTreeAndStackedPayloadInvokesNeitherCallback) {
  EXPECT_FALSE(dispatch(
      plugin_, "tree",
      R"({"tree_item_activated":{"id":"imu","column":0},"stacked_index":2,"stacked_page":"advanced"})"));
  EXPECT_EQ(plugin_.tree_calls, 0);
  EXPECT_EQ(plugin_.stacked_calls, 0);
  EXPECT_TRUE(plugin_.last_handler.empty());
}

TEST_F(TypedDispatchTest, TabAndComboEventsDoNotTriggerStackedPageHandler) {
  EXPECT_TRUE(dispatch(plugin_, "tabs", R"({"tab_index": 1})"));
  EXPECT_EQ(plugin_.last_handler, "tab_changed");
  EXPECT_EQ(plugin_.stacked_calls, 0);

  plugin_.reset();
  EXPECT_TRUE(dispatch(plugin_, "combo", R"({"current_index": 3})"));
  EXPECT_EQ(plugin_.last_handler, "index_changed");
  EXPECT_EQ(plugin_.stacked_calls, 0);
}

TEST_F(TypedDispatchTest, PartialStackedPayloadsFailClosedBeforeLegacyChannels) {
  EXPECT_FALSE(dispatch(plugin_, "configuration_stack", R"({"stacked_index": 2, "current_index": 7})"));
  EXPECT_TRUE(plugin_.last_handler.empty());
  EXPECT_EQ(plugin_.stacked_calls, 0);

  plugin_.reset();
  EXPECT_FALSE(dispatch(plugin_, "configuration_stack", R"({"stacked_page": "advanced_page", "text": "x"})"));
  EXPECT_TRUE(plugin_.last_handler.empty());
  EXPECT_EQ(plugin_.stacked_calls, 0);
}

TEST(TypedDispatchDefaultHandlerTest, DefaultStackedHandlerInvokesNoLegacyHandler) {
  LegacyHandlerRecordingPlugin plugin;
  EXPECT_FALSE(dispatch(plugin, "configuration_stack", R"({"stacked_index": 2, "stacked_page": "p", "text": "x"})"));
  EXPECT_EQ(plugin.legacy_calls, 0);
}

TEST_F(TypedDispatchTest, DateRangeChanged) {
  EXPECT_TRUE(dispatch(plugin_, "picker", R"({"date_from_iso": "2016-04-29T00:00:00", "date_to_iso": ""})"));
  EXPECT_EQ(plugin_.last_handler, "date_range_changed");
  EXPECT_EQ(plugin_.last_widget, "picker");
  EXPECT_EQ(plugin_.last_date_from, "2016-04-29T00:00:00");
  EXPECT_EQ(plugin_.last_date_to, "");
}

TEST_F(TypedDispatchTest, DateTimeChanged) {
  EXPECT_TRUE(dispatch(plugin_, "startTime", R"({"datetime_iso": "2026-01-02T03:04:05"})"));
  EXPECT_EQ(plugin_.last_handler, "date_time_changed");
  EXPECT_EQ(plugin_.last_widget, "startTime");
  EXPECT_EQ(plugin_.last_text, "2026-01-02T03:04:05");
}

// --- Edge cases ---

TEST_F(TypedDispatchTest, UnrecognizedFieldReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin_, "widget", R"({"unknown_field": 123})"));
  EXPECT_TRUE(plugin_.last_handler.empty());
}

TEST_F(TypedDispatchTest, EmptyJsonReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin_, "widget", "{}"));
  EXPECT_TRUE(plugin_.last_handler.empty());
}

TEST_F(TypedDispatchTest, InvalidJsonReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin_, "widget", "not json"));
  EXPECT_TRUE(plugin_.last_handler.empty());
}

// --- Priority tests (multi-field events) ---

TEST_F(TypedDispatchTest, TextTakesPriorityOverClicked) {
  // text is checked before clicked in the dispatch chain
  EXPECT_TRUE(dispatch(plugin_, "w", R"({"text": "x", "clicked": true})"));
  EXPECT_EQ(plugin_.last_handler, "text_changed");
}

TEST_F(TypedDispatchTest, FileSelectedTakesPriorityOverClicked) {
  // file_selected is checked before clicked
  EXPECT_TRUE(dispatch(plugin_, "w", R"({"file_selected": "/a", "clicked": true})"));
  EXPECT_EQ(plugin_.last_handler, "file_selected");
}

TEST_F(TypedDispatchTest, CurrentIndexTakesPriorityOverValue) {
  // current_index is checked before value
  EXPECT_TRUE(dispatch(plugin_, "w", R"({"current_index": 1, "value": 5})"));
  EXPECT_EQ(plugin_.last_handler, "index_changed");
}

TEST_F(TypedDispatchTest, StackedPagePairClaimsEventBeforeText) {
  EXPECT_TRUE(dispatch(plugin_, "configuration_stack", R"({"stacked_index": 2, "stacked_page": "p", "text": "x"})"));
  EXPECT_EQ(plugin_.last_handler, "stacked_page_changed");
  EXPECT_EQ(plugin_.stacked_calls, 1);
  EXPECT_EQ(plugin_.last_int, 2);
  EXPECT_EQ(plugin_.last_page, "p");
}

TEST_F(TypedDispatchTest, StackedPagePairTakesPriorityOverScalarIndexChannels) {
  EXPECT_TRUE(dispatch(
      plugin_, "configuration_stack",
      R"({"stacked_index": 2, "stacked_page": "advanced_page", "current_index": 7, "tab_index": 8})"));
  EXPECT_EQ(plugin_.last_handler, "stacked_page_changed");
  EXPECT_EQ(plugin_.stacked_calls, 1);
  EXPECT_EQ(plugin_.last_int, 2);
  EXPECT_EQ(plugin_.last_page, "advanced_page");
}

TEST_F(TypedDispatchTest, CodeChangedCarriesCursorToTypedHandler) {
  EXPECT_TRUE(dispatch(plugin_, "editor", R"({"code_changed": "robot ==", "code_cursor": 8})"));
  EXPECT_EQ(plugin_.last_handler, "code_changed");
  EXPECT_EQ(plugin_.last_text, "robot ==");
  EXPECT_EQ(plugin_.last_int, 8);
}

TEST_F(TypedDispatchTest, CodeChangedWithoutCursorPassesNegativeOne) {
  EXPECT_TRUE(dispatch(plugin_, "editor", R"({"code_changed": "x"})"));
  EXPECT_EQ(plugin_.last_handler, "code_changed");
  EXPECT_EQ(plugin_.last_int, -1);
}

TEST_F(TypedDispatchTest, ItemDeleteRequestedReachesTypedHandler) {
  EXPECT_TRUE(dispatch(plugin_, "lst", R"({"item_delete_index": 2})"));
  EXPECT_EQ(plugin_.last_handler, "item_delete_requested");
  EXPECT_EQ(plugin_.last_widget, "lst");
  EXPECT_EQ(plugin_.last_int, 2);
}
