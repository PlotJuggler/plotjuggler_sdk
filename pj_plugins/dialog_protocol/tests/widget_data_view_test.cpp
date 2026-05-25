// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <pj_plugins/host/widget_data_view.hpp>

// --- QLineEdit ---

TEST(WidgetDataViewTest, Text) {
  PJ::WidgetDataView v(R"({"my_input": {"text": "hello"}})");
  EXPECT_EQ(v.text("my_input"), "hello");
}

TEST(WidgetDataViewTest, Placeholder) {
  PJ::WidgetDataView v(R"({"my_input": {"placeholder": "type here"}})");
  EXPECT_EQ(v.placeholder("my_input"), "type here");
}

TEST(WidgetDataViewTest, ReadOnly) {
  PJ::WidgetDataView v(R"({"my_input": {"read_only": true}})");
  EXPECT_EQ(v.readOnly("my_input"), true);
}

// --- QComboBox ---

TEST(WidgetDataViewTest, CurrentIndex) {
  PJ::WidgetDataView v(R"({"combo": {"current_index": 2}})");
  EXPECT_EQ(v.currentIndex("combo"), 2);
}

TEST(WidgetDataViewTest, Items) {
  PJ::WidgetDataView v(R"({"combo": {"items": ["TCP", "UDP", "WS"]}})");
  auto items = v.items("combo");
  ASSERT_TRUE(items.has_value());
  EXPECT_EQ(items->size(), 3u);
  EXPECT_EQ((*items)[0], "TCP");
  EXPECT_EQ((*items)[1], "UDP");
  EXPECT_EQ((*items)[2], "WS");
}

// --- QCheckBox ---

TEST(WidgetDataViewTest, Checked) {
  PJ::WidgetDataView v(R"({"chk": {"checked": false}})");
  EXPECT_EQ(v.checked("chk"), false);
}

// --- QSpinBox ---

TEST(WidgetDataViewTest, ValueInt) {
  PJ::WidgetDataView v(R"({"spin": {"value": 42}})");
  EXPECT_EQ(v.valueInt("spin"), 42);
}

// --- QDoubleSpinBox ---

TEST(WidgetDataViewTest, ValueDouble) {
  PJ::WidgetDataView v(R"({"dspin": {"value": 3.14}})");
  auto val = v.valueDouble("dspin");
  ASSERT_TRUE(val.has_value());
  EXPECT_NEAR(*val, 3.14, 0.001);
}

TEST(WidgetDataViewTest, ValueDoubleReadsInt) {
  // An integer value should also be readable as double
  PJ::WidgetDataView v(R"({"dspin": {"value": 7}})");
  auto val = v.valueDouble("dspin");
  ASSERT_TRUE(val.has_value());
  EXPECT_DOUBLE_EQ(*val, 7.0);
}

// --- Range ---

TEST(WidgetDataViewTest, Range) {
  PJ::WidgetDataView v(R"({"spin": {"min": 1, "max": 100}})");
  EXPECT_EQ(v.rangeMin("spin"), 1);
  EXPECT_EQ(v.rangeMax("spin"), 100);
}

// --- QListWidget ---

TEST(WidgetDataViewTest, ListItems) {
  PJ::WidgetDataView v(R"({"list": {"list_items": ["a", "b", "c"]}})");
  auto items = v.listItems("list");
  ASSERT_TRUE(items.has_value());
  EXPECT_EQ(items->size(), 3u);
  EXPECT_EQ((*items)[0], "a");
}

TEST(WidgetDataViewTest, SelectedItems) {
  PJ::WidgetDataView v(R"({"list": {"selected_items": ["b"]}})");
  auto sel = v.selectedItems("list");
  ASSERT_TRUE(sel.has_value());
  EXPECT_EQ(sel->size(), 1u);
  EXPECT_EQ((*sel)[0], "b");
}

// --- QTableWidget ---

TEST(WidgetDataViewTest, TableHeaders) {
  PJ::WidgetDataView v(R"({"tbl": {"headers": ["Name", "Value"]}})");
  auto hdrs = v.tableHeaders("tbl");
  ASSERT_TRUE(hdrs.has_value());
  EXPECT_EQ(hdrs->size(), 2u);
  EXPECT_EQ((*hdrs)[1], "Value");
}

TEST(WidgetDataViewTest, TableRows) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["a", "1"], ["b", "2"]]}})");
  auto rows = v.tableRows("tbl");
  ASSERT_TRUE(rows.has_value());
  EXPECT_EQ(rows->size(), 2u);
  EXPECT_EQ((*rows)[0][0], "a");
  EXPECT_EQ((*rows)[1][1], "2");
}

// --- QLabel ---

TEST(WidgetDataViewTest, Label) {
  PJ::WidgetDataView v(R"({"lbl": {"label": "Status: OK"}})");
  EXPECT_EQ(v.label("lbl"), "Status: OK");
}

// --- QPushButton ---

TEST(WidgetDataViewTest, ButtonText) {
  PJ::WidgetDataView v(R"({"btn": {"button_text": "Connect"}})");
  EXPECT_EQ(v.buttonText("btn"), "Connect");
}

// --- File picker ---

TEST(WidgetDataViewTest, FilePicker) {
  PJ::WidgetDataView v(
      R"({"picker": {"button_text": "Browse", "action": "file_picker", "filter": "*.csv", "title": "Open"}})");
  EXPECT_TRUE(v.isFilePicker("picker"));
  EXPECT_EQ(v.filePickerFilter("picker"), "*.csv");
  EXPECT_EQ(v.filePickerTitle("picker"), "Open");
  EXPECT_EQ(v.buttonText("picker"), "Browse");
}

TEST(WidgetDataViewTest, NotFilePicker) {
  PJ::WidgetDataView v(R"({"btn": {"button_text": "OK"}})");
  EXPECT_FALSE(v.isFilePicker("btn"));
}

// --- QDialogButtonBox ---

TEST(WidgetDataViewTest, OkEnabled) {
  PJ::WidgetDataView v(R"({"bb": {"ok_enabled": true}})");
  EXPECT_EQ(v.okEnabled("bb"), true);
}

// --- QTabWidget ---

TEST(WidgetDataViewTest, TabIndex) {
  PJ::WidgetDataView v(R"({"tabs": {"tab_index": 1}})");
  EXPECT_EQ(v.tabIndex("tabs"), 1);
}

// --- Generic ---

TEST(WidgetDataViewTest, Enabled) {
  PJ::WidgetDataView v(R"({"w": {"enabled": false}})");
  EXPECT_EQ(v.enabled("w"), false);
}

TEST(WidgetDataViewTest, Visible) {
  PJ::WidgetDataView v(R"({"w": {"visible": true}})");
  EXPECT_EQ(v.visible("w"), true);
}

// --- Missing widget ---

TEST(WidgetDataViewTest, MissingWidgetReturnsNullopt) {
  PJ::WidgetDataView v(R"({"a": {"text": "x"}})");
  EXPECT_FALSE(v.text("nonexistent").has_value());
  EXPECT_FALSE(v.checked("nonexistent").has_value());
  EXPECT_FALSE(v.valueInt("nonexistent").has_value());
  EXPECT_FALSE(v.items("nonexistent").has_value());
}

// --- Missing field ---

TEST(WidgetDataViewTest, MissingFieldReturnsNullopt) {
  PJ::WidgetDataView v(R"({"w": {"text": "x"}})");
  EXPECT_FALSE(v.checked("w").has_value());
  EXPECT_FALSE(v.valueInt("w").has_value());
  EXPECT_FALSE(v.placeholder("w").has_value());
}

// --- Wrong type ---

TEST(WidgetDataViewTest, WrongTypeReturnsNullopt) {
  PJ::WidgetDataView v(R"({"w": {"text": 42, "checked": "yes", "value": true}})");
  EXPECT_FALSE(v.text("w").has_value());
  EXPECT_FALSE(v.checked("w").has_value());
  EXPECT_FALSE(v.valueInt("w").has_value());
}

// --- Invalid JSON ---

TEST(WidgetDataViewTest, InvalidJsonGraceful) {
  PJ::WidgetDataView v("not valid json {{{");
  EXPECT_FALSE(v.text("anything").has_value());
  EXPECT_TRUE(v.widgetNames().empty());
  EXPECT_FALSE(v.hasWidget("anything"));
}

TEST(WidgetDataViewTest, EmptyObject) {
  PJ::WidgetDataView v("{}");
  EXPECT_TRUE(v.widgetNames().empty());
  EXPECT_FALSE(v.hasWidget("x"));
}

// --- Enumeration ---

TEST(WidgetDataViewTest, WidgetNames) {
  PJ::WidgetDataView v(R"({"alpha": {"text": "a"}, "beta": {"text": "b"}})");
  auto names = v.widgetNames();
  EXPECT_EQ(names.size(), 2u);
  // JSON object iteration order — just check both names are present
  EXPECT_TRUE(v.hasWidget("alpha"));
  EXPECT_TRUE(v.hasWidget("beta"));
  EXPECT_FALSE(v.hasWidget("gamma"));
}

// --- Raw access ---

TEST(WidgetDataViewTest, RawAccess) {
  PJ::WidgetDataView v(R"({"w": {"custom_field": 99}})");
  const auto& raw = v.raw();
  EXPECT_TRUE(raw.is_object());
  EXPECT_EQ(raw["w"]["custom_field"], 99);
}
