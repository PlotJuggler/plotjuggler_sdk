// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
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
    last_handler = "file_selected";
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

  bool onDateRangeChanged(std::string_view widget_name, std::string_view from_iso, std::string_view to_iso) override {
    last_handler = "date_range_changed";
    last_widget = std::string(widget_name);
    last_date_from = std::string(from_iso);
    last_date_to = std::string(to_iso);
    return true;
  }

  bool onCodeChangedWithCursor(std::string_view widget_name, std::string_view code, int cursor) override {
    last_handler = "code_changed";
    last_widget = std::string(widget_name);
    last_text = std::string(code);
    last_int = cursor;
    return true;
  }

  // Recorded state
  std::string last_handler;
  std::string last_widget;
  std::string last_text;
  std::string last_date_from;
  std::string last_date_to;
  int last_int = -1;
  double last_double = -1.0;
  bool last_bool = false;
  std::vector<std::string> last_strings;

  void reset() {
    last_handler.clear();
    last_widget.clear();
    last_text.clear();
    last_date_from.clear();
    last_date_to.clear();
    last_int = -1;
    last_double = -1.0;
    last_bool = false;
    last_strings.clear();
  }
};

// Helper: call the base class on_widget_event through the public interface.
// DialogPluginTyped::on_widget_event is final, but we access it via DialogPluginBase ref.
bool dispatch(PJ::DialogPluginBase& plugin, std::string_view widget, std::string_view json) {
  return plugin.onWidgetEvent(widget, json);
}

}  // namespace

class TypedDispatchTest : public ::testing::Test {
 protected:
  RecordingPlugin plugin;
};

// --- Individual dispatch tests ---

TEST_F(TypedDispatchTest, TextChanged) {
  EXPECT_TRUE(dispatch(plugin, "my_input", R"({"text": "hello"})"));
  EXPECT_EQ(plugin.last_handler, "text_changed");
  EXPECT_EQ(plugin.last_widget, "my_input");
  EXPECT_EQ(plugin.last_text, "hello");
}

TEST_F(TypedDispatchTest, IndexChanged) {
  EXPECT_TRUE(dispatch(plugin, "combo", R"({"current_index": 3})"));
  EXPECT_EQ(plugin.last_handler, "index_changed");
  EXPECT_EQ(plugin.last_int, 3);
}

TEST_F(TypedDispatchTest, Toggled) {
  EXPECT_TRUE(dispatch(plugin, "checkbox", R"({"checked": true})"));
  EXPECT_EQ(plugin.last_handler, "toggled");
  EXPECT_TRUE(plugin.last_bool);
}

TEST_F(TypedDispatchTest, ToggledFalse) {
  EXPECT_TRUE(dispatch(plugin, "checkbox", R"({"checked": false})"));
  EXPECT_EQ(plugin.last_handler, "toggled");
  EXPECT_FALSE(plugin.last_bool);
}

TEST_F(TypedDispatchTest, ValueInt) {
  EXPECT_TRUE(dispatch(plugin, "spinbox", R"({"value": 42})"));
  EXPECT_EQ(plugin.last_handler, "value_int");
  EXPECT_EQ(plugin.last_int, 42);
}

TEST_F(TypedDispatchTest, ValueDouble) {
  EXPECT_TRUE(dispatch(plugin, "dspinbox", R"({"value": 3.14})"));
  EXPECT_EQ(plugin.last_handler, "value_double");
  EXPECT_DOUBLE_EQ(plugin.last_double, 3.14);
}

TEST_F(TypedDispatchTest, Clicked) {
  EXPECT_TRUE(dispatch(plugin, "btn", R"({"clicked": true})"));
  EXPECT_EQ(plugin.last_handler, "clicked");
  EXPECT_EQ(plugin.last_widget, "btn");
}

TEST_F(TypedDispatchTest, FileSelected) {
  EXPECT_TRUE(dispatch(plugin, "file_btn", R"({"file_selected": "/tmp/data.csv"})"));
  EXPECT_EQ(plugin.last_handler, "file_selected");
  EXPECT_EQ(plugin.last_text, "/tmp/data.csv");
}

TEST_F(TypedDispatchTest, SelectionChanged) {
  EXPECT_TRUE(dispatch(plugin, "list", R"({"selected_items": ["a", "b"]})"));
  EXPECT_EQ(plugin.last_handler, "selection_changed");
  ASSERT_EQ(plugin.last_strings.size(), 2u);
  EXPECT_EQ(plugin.last_strings[0], "a");
  EXPECT_EQ(plugin.last_strings[1], "b");
}

TEST_F(TypedDispatchTest, TabChanged) {
  EXPECT_TRUE(dispatch(plugin, "tabs", R"({"tab_index": 2})"));
  EXPECT_EQ(plugin.last_handler, "tab_changed");
  EXPECT_EQ(plugin.last_int, 2);
}

TEST_F(TypedDispatchTest, DateRangeChanged) {
  EXPECT_TRUE(dispatch(plugin, "picker", R"({"date_from_iso": "2016-04-29T00:00:00", "date_to_iso": ""})"));
  EXPECT_EQ(plugin.last_handler, "date_range_changed");
  EXPECT_EQ(plugin.last_widget, "picker");
  EXPECT_EQ(plugin.last_date_from, "2016-04-29T00:00:00");
  EXPECT_EQ(plugin.last_date_to, "");
}

// --- Edge cases ---

TEST_F(TypedDispatchTest, UnrecognizedFieldReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin, "widget", R"({"unknown_field": 123})"));
  EXPECT_TRUE(plugin.last_handler.empty());
}

TEST_F(TypedDispatchTest, EmptyJsonReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin, "widget", "{}"));
  EXPECT_TRUE(plugin.last_handler.empty());
}

TEST_F(TypedDispatchTest, InvalidJsonReturnsFalse) {
  EXPECT_FALSE(dispatch(plugin, "widget", "not json"));
  EXPECT_TRUE(plugin.last_handler.empty());
}

// --- Priority tests (multi-field events) ---

TEST_F(TypedDispatchTest, TextTakesPriorityOverClicked) {
  // text is checked before clicked in the dispatch chain
  EXPECT_TRUE(dispatch(plugin, "w", R"({"text": "x", "clicked": true})"));
  EXPECT_EQ(plugin.last_handler, "text_changed");
}

TEST_F(TypedDispatchTest, FileSelectedTakesPriorityOverClicked) {
  // file_selected is checked before clicked
  EXPECT_TRUE(dispatch(plugin, "w", R"({"file_selected": "/a", "clicked": true})"));
  EXPECT_EQ(plugin.last_handler, "file_selected");
}

TEST_F(TypedDispatchTest, CurrentIndexTakesPriorityOverValue) {
  // current_index is checked before value
  EXPECT_TRUE(dispatch(plugin, "w", R"({"current_index": 1, "value": 5})"));
  EXPECT_EQ(plugin.last_handler, "index_changed");
}

TEST_F(TypedDispatchTest, CodeChangedCarriesCursorToTypedHandler) {
  EXPECT_TRUE(dispatch(plugin, "editor", R"({"code_changed": "robot ==", "code_cursor": 8})"));
  EXPECT_EQ(plugin.last_handler, "code_changed");
  EXPECT_EQ(plugin.last_text, "robot ==");
  EXPECT_EQ(plugin.last_int, 8);
}

TEST_F(TypedDispatchTest, CodeChangedWithoutCursorPassesNegativeOne) {
  EXPECT_TRUE(dispatch(plugin, "editor", R"({"code_changed": "x"})"));
  EXPECT_EQ(plugin.last_handler, "code_changed");
  EXPECT_EQ(plugin.last_int, -1);
}
