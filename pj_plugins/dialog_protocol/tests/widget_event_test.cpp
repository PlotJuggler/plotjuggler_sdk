// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

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

// --- TabIndex ---

TEST(WidgetEventTest, TabIndex) {
  WidgetEvent ev(R"({"tab_index": 1})");
  ASSERT_TRUE(ev.tabIndex().has_value());
  EXPECT_EQ(ev.tabIndex().value(), 1);
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
