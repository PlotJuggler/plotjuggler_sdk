// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <limits>
#include <pj_plugins/sdk/widget_event.hpp>

using PJ::WidgetEvent;

// --- Text ---

TEST(WidgetEventTest, Text) {
  WidgetEvent ev(R"({"text": "hello"})");
  ASSERT_TRUE(ev.text().has_value());
  EXPECT_EQ(ev.text().value(), "hello");
}

TEST(WidgetEventTest, TextMissing) {
  WidgetEvent ev(R"({"clicked": true})");
  EXPECT_FALSE(ev.text().has_value());
}

// --- CurrentIndex ---

TEST(WidgetEventTest, CurrentIndex) {
  WidgetEvent ev(R"({"current_index": 2})");
  ASSERT_TRUE(ev.currentIndex().has_value());
  EXPECT_EQ(ev.currentIndex().value(), 2);
}

TEST(WidgetEventTest, CurrentText) {
  WidgetEvent ev(R"({"current_text": "TCP"})");
  ASSERT_TRUE(ev.currentText().has_value());
  EXPECT_EQ(ev.currentText().value(), "TCP");
}

// --- Checked ---

TEST(WidgetEventTest, Checked) {
  WidgetEvent ev(R"({"checked": true})");
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_TRUE(ev.checked().value());
}

TEST(WidgetEventTest, CheckedFalse) {
  WidgetEvent ev(R"({"checked": false})");
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_FALSE(ev.checked().value());
}

// --- ValueInt ---

TEST(WidgetEventTest, ValueInt) {
  WidgetEvent ev(R"({"value": 42})");
  ASSERT_TRUE(ev.valueInt().has_value());
  EXPECT_EQ(ev.valueInt().value(), 42);
}

// --- ValueDouble ---

TEST(WidgetEventTest, ValueDouble) {
  WidgetEvent ev(R"({"value": 3.14})");
  ASSERT_TRUE(ev.valueDouble().has_value());
  EXPECT_DOUBLE_EQ(ev.valueDouble().value(), 3.14);
}

TEST(WidgetEventTest, ValueIntAlsoAccessibleAsDouble) {
  WidgetEvent ev(R"({"value": 42})");
  // Integer values are also numbers, so value_double should work
  ASSERT_TRUE(ev.valueDouble().has_value());
  EXPECT_DOUBLE_EQ(ev.valueDouble().value(), 42.0);
}

// --- SelectedItems ---

TEST(WidgetEventTest, SelectedItems) {
  WidgetEvent ev(R"({"selected_items": ["topic_a", "topic_b"]})");
  auto items = ev.selectedItems();
  ASSERT_TRUE(items.has_value());
  ASSERT_EQ(items->size(), 2u);
  EXPECT_EQ((*items)[0], "topic_a");
  EXPECT_EQ((*items)[1], "topic_b");
}

TEST(WidgetEventTest, SelectedItemsEmpty) {
  WidgetEvent ev(R"({"selected_items": []})");
  auto items = ev.selectedItems();
  ASSERT_TRUE(items.has_value());
  EXPECT_TRUE(items->empty());
}

// --- Clicked ---

TEST(WidgetEventTest, Clicked) {
  WidgetEvent ev(R"({"clicked": true})");
  EXPECT_TRUE(ev.clicked());
}

TEST(WidgetEventTest, ClickedMissing) {
  WidgetEvent ev(R"({"text": "hello"})");
  EXPECT_FALSE(ev.clicked());
}

// --- FileSelected ---

TEST(WidgetEventTest, FileSelected) {
  WidgetEvent ev(R"({"file_selected": "/path/to/cert.pem"})");
  ASSERT_TRUE(ev.fileSelected().has_value());
  EXPECT_EQ(ev.fileSelected().value(), "/path/to/cert.pem");
}

TEST(WidgetEventTest, FilePickerResultRejectsMalformedPayloadsAtomically) {
  const std::vector<std::string> malformed = {
      R"({"file_picker_result":17})",
      R"({"file_picker_result":{"status":"selected","paths":[],"display_names":[],"selected_filter_id":"","error":""}})",
      R"({"file_picker_result":{"status":"chosen","paths":["/a"],"display_names":["a"],"selected_filter_id":"","error":""}})",
      R"({"file_picker_result":{"status":"selected","paths":[""],"display_names":["a"],"selected_filter_id":"","error":""}})",
      R"({"file_picker_result":{"status":"selected","paths":["/a",2],"display_names":["a"],"selected_filter_id":"","error":""}})",
      R"({"file_picker_result":{"status":"selected","paths":["/a"],"display_names":[2],"selected_filter_id":"","error":""}})",
      R"({"file_picker_result":{"status":"selected","paths":["/a"],"display_names":["a"],"selected_filter_id":2,"error":""}})",
      R"({"file_picker_result":{"status":"selected","paths":["/a"],"display_names":["a"],"selected_filter_id":""}})",
  };
  for (const auto& json : malformed) {
    SCOPED_TRACE(json);
    EXPECT_FALSE(WidgetEvent(json).filePickerResult().has_value());
  }
}

// --- TabIndex ---

TEST(WidgetEventTest, TabIndex) {
  WidgetEvent ev(R"({"tab_index": 1})");
  ASSERT_TRUE(ev.tabIndex().has_value());
  EXPECT_EQ(ev.tabIndex().value(), 1);
}

// --- Stacked page ---

TEST(WidgetEventTest, StackedPageFieldsParseIndependently) {
  WidgetEvent page_only(R"({"stacked_page": "advanced_page"})");
  EXPECT_EQ(page_only.stackedPage(), "advanced_page");
  EXPECT_FALSE(page_only.stackedIndex().has_value());

  WidgetEvent index_only(R"({"stacked_index": 2})");
  EXPECT_EQ(index_only.stackedIndex(), 2);
  EXPECT_FALSE(index_only.stackedPage().has_value());
}

// --- Tree events ---

TEST(WidgetEventTest, TreeSelectionParsesCompleteIdSetIncludingEmpty) {
  WidgetEvent selected(R"({"tree_selection_changed":["visible","filtered-out"]})");
  EXPECT_EQ(selected.treeSelectionChanged(), (std::vector<std::string>{"visible", "filtered-out"}));

  WidgetEvent cleared(R"({"tree_selection_changed":[]})");
  ASSERT_TRUE(cleared.treeSelectionChanged().has_value());
  EXPECT_TRUE(cleared.treeSelectionChanged()->empty());
}

TEST(WidgetEventTest, TreeItemActivationParsesIdAndColumn) {
  WidgetEvent event(R"({"tree_item_activated":{"id":"imu","column":1}})");
  auto activation = event.treeItemActivated();
  ASSERT_TRUE(activation.has_value());
  EXPECT_EQ(activation->id, "imu");
  EXPECT_EQ(activation->column, 1);

  WidgetEvent maximum(
      std::string(R"({"tree_item_activated":{"id":"imu","column":)") + std::to_string(std::numeric_limits<int>::max()) +
      "}}");
  ASSERT_TRUE(maximum.treeItemActivated().has_value());
  EXPECT_EQ(maximum.treeItemActivated()->column, std::numeric_limits<int>::max());
}

TEST(WidgetEventTest, TreeExpansionParsesIdAndState) {
  WidgetEvent event(R"({"tree_expansion_changed":{"id":"sensors","expanded":false}})");
  auto expansion = event.treeExpansionChanged();
  ASSERT_TRUE(expansion.has_value());
  EXPECT_EQ(expansion->id, "sensors");
  EXPECT_FALSE(expansion->expanded);
}

TEST(WidgetEventTest, TreeCheckStateParsesCanonicalState) {
  WidgetEvent event(R"({"tree_check_state_changed":{"id":"imu","state":"partially_checked"}})");
  auto change = event.treeCheckStateChanged();
  ASSERT_TRUE(change.has_value());
  EXPECT_EQ(change->id, "imu");
  EXPECT_EQ(change->state, PJ::TreeCheckState::PartiallyChecked);
}

TEST(WidgetEventTest, MalformedTreeEventsReturnNullopt) {
  EXPECT_FALSE(WidgetEvent(R"({"tree_selection_changed":["ok",2]})").treeSelectionChanged());
  EXPECT_FALSE(WidgetEvent(R"({"tree_item_activated":{"id":"imu"}})").treeItemActivated());
  EXPECT_FALSE(WidgetEvent(R"({"tree_item_activated":{"id":"imu","column":-1}})").treeItemActivated());
  EXPECT_FALSE(WidgetEvent(R"({"tree_item_activated":{"id":"imu","column":2147483648}})").treeItemActivated());
  EXPECT_FALSE(WidgetEvent(R"({"tree_item_activated":{"id":"imu","column":4294967296}})").treeItemActivated());
  EXPECT_FALSE(
      WidgetEvent(R"({"tree_item_activated":{"id":"imu","column":18446744073709551615}})").treeItemActivated());
  EXPECT_FALSE(WidgetEvent(R"({"tree_expansion_changed":{"id":"","expanded":true}})").treeExpansionChanged());
  EXPECT_FALSE(WidgetEvent(R"({"tree_check_state_changed":{"id":"imu","state":"partial"}})").treeCheckStateChanged());
}

// --- Has ---

TEST(WidgetEventTest, Has) {
  WidgetEvent ev(R"({"text": "hello", "extra": 42})");
  EXPECT_TRUE(ev.has("text"));
  EXPECT_TRUE(ev.has("extra"));
  EXPECT_FALSE(ev.has("missing"));
}

// --- Raw ---

TEST(WidgetEventTest, Raw) {
  WidgetEvent ev(R"({"custom_field": "custom_value"})");
  EXPECT_EQ(ev.raw()["custom_field"], "custom_value");
}

// --- Invalid JSON ---

TEST(WidgetEventTest, InvalidJsonDoesNotCrash) {
  WidgetEvent ev("not valid json at all");
  EXPECT_FALSE(ev.text().has_value());
  EXPECT_FALSE(ev.clicked());
  EXPECT_FALSE(ev.has("anything"));
}

TEST(WidgetEventTest, EmptyJsonObject) {
  WidgetEvent ev("{}");
  EXPECT_FALSE(ev.text().has_value());
  EXPECT_FALSE(ev.currentIndex().has_value());
  EXPECT_FALSE(ev.checked().has_value());
  EXPECT_FALSE(ev.clicked());
}

// --- Wrong type doesn't crash ---

TEST(WidgetEventTest, WrongTypeForText) {
  WidgetEvent ev(R"({"text": 42})");
  EXPECT_FALSE(ev.text().has_value());
}

TEST(WidgetEventTest, WrongTypeForChecked) {
  WidgetEvent ev(R"({"checked": "yes"})");
  EXPECT_FALSE(ev.checked().has_value());
}

TEST(WidgetEventTest, WrongTypeForCurrentIndex) {
  WidgetEvent ev(R"({"current_index": "two"})");
  EXPECT_FALSE(ev.currentIndex().has_value());
}
