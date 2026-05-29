// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/sdk/widget_event.hpp>
#include <string>
#include <vector>

// Round-trip pattern: build JSON with WidgetEventBuilder, parse with WidgetEvent.
// This validates the host↔plugin JSON contract.

TEST(WidgetEventBuilderTest, TextChanged) {
  std::string json = PJ::WidgetEventBuilder::textChanged("hello world");
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.text().has_value());
  EXPECT_EQ(*ev.text(), "hello world");
}

TEST(WidgetEventBuilderTest, TextChangedEmpty) {
  std::string json = PJ::WidgetEventBuilder::textChanged("");
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.text().has_value());
  EXPECT_EQ(*ev.text(), "");
}

TEST(WidgetEventBuilderTest, IndexChanged) {
  std::string json = PJ::WidgetEventBuilder::indexChanged(3);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.currentIndex().has_value());
  EXPECT_EQ(*ev.currentIndex(), 3);
  // No current_text when not provided
  EXPECT_FALSE(ev.currentText().has_value());
}

TEST(WidgetEventBuilderTest, IndexChangedWithText) {
  std::string json = PJ::WidgetEventBuilder::indexChanged(1, "UDP");
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.currentIndex().has_value());
  EXPECT_EQ(*ev.currentIndex(), 1);
  ASSERT_TRUE(ev.currentText().has_value());
  EXPECT_EQ(*ev.currentText(), "UDP");
}

TEST(WidgetEventBuilderTest, Toggled) {
  std::string json = PJ::WidgetEventBuilder::toggled(true);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_TRUE(*ev.checked());
}

TEST(WidgetEventBuilderTest, ToggledFalse) {
  std::string json = PJ::WidgetEventBuilder::toggled(false);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_FALSE(*ev.checked());
}

TEST(WidgetEventBuilderTest, ValueChangedInt) {
  std::string json = PJ::WidgetEventBuilder::valueChanged(9090);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.valueInt().has_value());
  EXPECT_EQ(*ev.valueInt(), 9090);
}

TEST(WidgetEventBuilderTest, ValueChangedDouble) {
  std::string json = PJ::WidgetEventBuilder::valueChanged(3.14);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.valueDouble().has_value());
  EXPECT_NEAR(*ev.valueDouble(), 3.14, 0.001);
}

TEST(WidgetEventBuilderTest, SelectionChanged) {
  std::vector<std::string> sel = {"/sensors/imu", "/motors/left"};
  std::string json = PJ::WidgetEventBuilder::selectionChanged(sel);
  PJ::WidgetEvent ev(json);
  auto parsed = ev.selectedItems();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->size(), 2u);
  EXPECT_EQ((*parsed)[0], "/sensors/imu");
  EXPECT_EQ((*parsed)[1], "/motors/left");
}

TEST(WidgetEventBuilderTest, SelectionChangedEmpty) {
  std::string json = PJ::WidgetEventBuilder::selectionChanged({});
  PJ::WidgetEvent ev(json);
  auto parsed = ev.selectedItems();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->empty());
}

TEST(WidgetEventBuilderTest, Clicked) {
  std::string json = PJ::WidgetEventBuilder::clicked();
  PJ::WidgetEvent ev(json);
  EXPECT_TRUE(ev.clicked());
}

TEST(WidgetEventBuilderTest, FileSelected) {
  std::string json = PJ::WidgetEventBuilder::fileSelected("/tmp/cert.pem");
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.fileSelected().has_value());
  EXPECT_EQ(*ev.fileSelected(), "/tmp/cert.pem");
}

TEST(WidgetEventBuilderTest, TabChanged) {
  std::string json = PJ::WidgetEventBuilder::tabChanged(2);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.tabIndex().has_value());
  EXPECT_EQ(*ev.tabIndex(), 2);
}

TEST(WidgetEventBuilderTest, DateRangeChanged) {
  std::string json = PJ::WidgetEventBuilder::dateRangeChanged("2016-04-29T00:00:00", "2016-05-01T12:00:00");
  PJ::WidgetEvent ev(json);
  auto range = ev.dateRangeChanged();
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->from_iso, "2016-04-29T00:00:00");
  EXPECT_EQ(range->to_iso, "2016-05-01T12:00:00");
}

TEST(WidgetEventBuilderTest, DateRangeChangedUnboundedSides) {
  // Empty strings mean that side of the range is unbounded.
  std::string json = PJ::WidgetEventBuilder::dateRangeChanged("", "");
  PJ::WidgetEvent ev(json);
  auto range = ev.dateRangeChanged();
  ASSERT_TRUE(range.has_value());
  EXPECT_TRUE(range->from_iso.empty());
  EXPECT_TRUE(range->to_iso.empty());
}

TEST(WidgetEventTest_DateRange, MissingFieldYieldsNullopt) {
  // Only one side present -> not a valid date-range event.
  PJ::WidgetEvent ev(R"({"date_from_iso": "2016-04-29T00:00:00"})");
  EXPECT_FALSE(ev.dateRangeChanged().has_value());
}

// --- Verify JSON is parseable and contains only expected fields ---

TEST(WidgetEventBuilderTest, ClickedHasNoExtraFields) {
  std::string json = PJ::WidgetEventBuilder::clicked();
  PJ::WidgetEvent ev(json);
  // clicked() event should not trigger text/index/checked/etc.
  EXPECT_FALSE(ev.text().has_value());
  EXPECT_FALSE(ev.currentIndex().has_value());
  EXPECT_FALSE(ev.checked().has_value());
  EXPECT_TRUE(ev.clicked());
}

TEST(WidgetEventBuilderTest, TextChangedDoesNotTriggerOtherFields) {
  std::string json = PJ::WidgetEventBuilder::textChanged("test");
  PJ::WidgetEvent ev(json);
  EXPECT_TRUE(ev.text().has_value());
  EXPECT_FALSE(ev.currentIndex().has_value());
  EXPECT_FALSE(ev.checked().has_value());
  EXPECT_FALSE(ev.clicked());
}

TEST(WidgetEventBuilderTest, CodeChangedWithCursor) {
  std::string json = PJ::WidgetEventBuilder::codeChanged("robot ==", 8);
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.codeChanged().has_value());
  EXPECT_EQ(*ev.codeChanged(), "robot ==");
  ASSERT_TRUE(ev.codeCursor().has_value());
  EXPECT_EQ(*ev.codeCursor(), 8);
}

TEST(WidgetEventBuilderTest, CodeChangedWithoutCursorOmitsField) {
  std::string json = PJ::WidgetEventBuilder::codeChanged("x");
  PJ::WidgetEvent ev(json);
  ASSERT_TRUE(ev.codeChanged().has_value());
  EXPECT_EQ(*ev.codeChanged(), "x");
  EXPECT_FALSE(ev.codeCursor().has_value());
}
