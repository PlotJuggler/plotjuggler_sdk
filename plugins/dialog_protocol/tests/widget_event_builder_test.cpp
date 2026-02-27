#include <pj/host/widget_event_builder.hpp>
#include <pj/sdk/widget_event.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Round-trip pattern: build JSON with WidgetEventBuilder, parse with WidgetEvent.
// This validates the host↔plugin JSON contract.

TEST(WidgetEventBuilderTest, TextChanged) {
  std::string json = pj::host::WidgetEventBuilder::text_changed("hello world");
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.text().has_value());
  EXPECT_EQ(*ev.text(), "hello world");
}

TEST(WidgetEventBuilderTest, TextChangedEmpty) {
  std::string json = pj::host::WidgetEventBuilder::text_changed("");
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.text().has_value());
  EXPECT_EQ(*ev.text(), "");
}

TEST(WidgetEventBuilderTest, IndexChanged) {
  std::string json = pj::host::WidgetEventBuilder::index_changed(3);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.current_index().has_value());
  EXPECT_EQ(*ev.current_index(), 3);
  // No current_text when not provided
  EXPECT_FALSE(ev.current_text().has_value());
}

TEST(WidgetEventBuilderTest, IndexChangedWithText) {
  std::string json = pj::host::WidgetEventBuilder::index_changed(1, "UDP");
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.current_index().has_value());
  EXPECT_EQ(*ev.current_index(), 1);
  ASSERT_TRUE(ev.current_text().has_value());
  EXPECT_EQ(*ev.current_text(), "UDP");
}

TEST(WidgetEventBuilderTest, Toggled) {
  std::string json = pj::host::WidgetEventBuilder::toggled(true);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_TRUE(*ev.checked());
}

TEST(WidgetEventBuilderTest, ToggledFalse) {
  std::string json = pj::host::WidgetEventBuilder::toggled(false);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.checked().has_value());
  EXPECT_FALSE(*ev.checked());
}

TEST(WidgetEventBuilderTest, ValueChangedInt) {
  std::string json = pj::host::WidgetEventBuilder::value_changed(9090);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.value_int().has_value());
  EXPECT_EQ(*ev.value_int(), 9090);
}

TEST(WidgetEventBuilderTest, ValueChangedDouble) {
  std::string json = pj::host::WidgetEventBuilder::value_changed(3.14);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.value_double().has_value());
  EXPECT_NEAR(*ev.value_double(), 3.14, 0.001);
}

TEST(WidgetEventBuilderTest, SelectionChanged) {
  std::vector<std::string> sel = {"/sensors/imu", "/motors/left"};
  std::string json = pj::host::WidgetEventBuilder::selection_changed(sel);
  pj::sdk::WidgetEvent ev(json);
  auto parsed = ev.selected_items();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->size(), 2u);
  EXPECT_EQ((*parsed)[0], "/sensors/imu");
  EXPECT_EQ((*parsed)[1], "/motors/left");
}

TEST(WidgetEventBuilderTest, SelectionChangedEmpty) {
  std::string json = pj::host::WidgetEventBuilder::selection_changed({});
  pj::sdk::WidgetEvent ev(json);
  auto parsed = ev.selected_items();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->empty());
}

TEST(WidgetEventBuilderTest, Clicked) {
  std::string json = pj::host::WidgetEventBuilder::clicked();
  pj::sdk::WidgetEvent ev(json);
  EXPECT_TRUE(ev.clicked());
}

TEST(WidgetEventBuilderTest, FileSelected) {
  std::string json = pj::host::WidgetEventBuilder::file_selected("/tmp/cert.pem");
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.file_selected().has_value());
  EXPECT_EQ(*ev.file_selected(), "/tmp/cert.pem");
}

TEST(WidgetEventBuilderTest, TabChanged) {
  std::string json = pj::host::WidgetEventBuilder::tab_changed(2);
  pj::sdk::WidgetEvent ev(json);
  ASSERT_TRUE(ev.tab_index().has_value());
  EXPECT_EQ(*ev.tab_index(), 2);
}

// --- Verify JSON is parseable and contains only expected fields ---

TEST(WidgetEventBuilderTest, ClickedHasNoExtraFields) {
  std::string json = pj::host::WidgetEventBuilder::clicked();
  pj::sdk::WidgetEvent ev(json);
  // clicked() event should not trigger text/index/checked/etc.
  EXPECT_FALSE(ev.text().has_value());
  EXPECT_FALSE(ev.current_index().has_value());
  EXPECT_FALSE(ev.checked().has_value());
  EXPECT_TRUE(ev.clicked());
}

TEST(WidgetEventBuilderTest, TextChangedDoesNotTriggerOtherFields) {
  std::string json = pj::host::WidgetEventBuilder::text_changed("test");
  pj::sdk::WidgetEvent ev(json);
  EXPECT_TRUE(ev.text().has_value());
  EXPECT_FALSE(ev.current_index().has_value());
  EXPECT_FALSE(ev.checked().has_value());
  EXPECT_FALSE(ev.clicked());
}
