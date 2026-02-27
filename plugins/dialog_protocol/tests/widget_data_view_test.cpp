#include <pj/host/widget_data_view.hpp>

#include <gtest/gtest.h>

// --- QLineEdit ---

TEST(WidgetDataViewTest, Text) {
  pj::host::WidgetDataView v(R"({"my_input": {"text": "hello"}})");
  EXPECT_EQ(v.text("my_input"), "hello");
}

TEST(WidgetDataViewTest, Placeholder) {
  pj::host::WidgetDataView v(R"({"my_input": {"placeholder": "type here"}})");
  EXPECT_EQ(v.placeholder("my_input"), "type here");
}

TEST(WidgetDataViewTest, ReadOnly) {
  pj::host::WidgetDataView v(R"({"my_input": {"read_only": true}})");
  EXPECT_EQ(v.read_only("my_input"), true);
}

// --- QComboBox ---

TEST(WidgetDataViewTest, CurrentIndex) {
  pj::host::WidgetDataView v(R"({"combo": {"current_index": 2}})");
  EXPECT_EQ(v.current_index("combo"), 2);
}

TEST(WidgetDataViewTest, Items) {
  pj::host::WidgetDataView v(R"({"combo": {"items": ["TCP", "UDP", "WS"]}})");
  auto items = v.items("combo");
  ASSERT_TRUE(items.has_value());
  EXPECT_EQ(items->size(), 3u);
  EXPECT_EQ((*items)[0], "TCP");
  EXPECT_EQ((*items)[1], "UDP");
  EXPECT_EQ((*items)[2], "WS");
}

// --- QCheckBox ---

TEST(WidgetDataViewTest, Checked) {
  pj::host::WidgetDataView v(R"({"chk": {"checked": false}})");
  EXPECT_EQ(v.checked("chk"), false);
}

// --- QSpinBox ---

TEST(WidgetDataViewTest, ValueInt) {
  pj::host::WidgetDataView v(R"({"spin": {"value": 42}})");
  EXPECT_EQ(v.value_int("spin"), 42);
}

// --- QDoubleSpinBox ---

TEST(WidgetDataViewTest, ValueDouble) {
  pj::host::WidgetDataView v(R"({"dspin": {"value": 3.14}})");
  auto val = v.value_double("dspin");
  ASSERT_TRUE(val.has_value());
  EXPECT_NEAR(*val, 3.14, 0.001);
}

TEST(WidgetDataViewTest, ValueDoubleReadsInt) {
  // An integer value should also be readable as double
  pj::host::WidgetDataView v(R"({"dspin": {"value": 7}})");
  auto val = v.value_double("dspin");
  ASSERT_TRUE(val.has_value());
  EXPECT_DOUBLE_EQ(*val, 7.0);
}

// --- Range ---

TEST(WidgetDataViewTest, Range) {
  pj::host::WidgetDataView v(R"({"spin": {"min": 1, "max": 100}})");
  EXPECT_EQ(v.range_min("spin"), 1);
  EXPECT_EQ(v.range_max("spin"), 100);
}

// --- QListWidget ---

TEST(WidgetDataViewTest, ListItems) {
  pj::host::WidgetDataView v(R"({"list": {"list_items": ["a", "b", "c"]}})");
  auto items = v.list_items("list");
  ASSERT_TRUE(items.has_value());
  EXPECT_EQ(items->size(), 3u);
  EXPECT_EQ((*items)[0], "a");
}

TEST(WidgetDataViewTest, SelectedItems) {
  pj::host::WidgetDataView v(R"({"list": {"selected_items": ["b"]}})");
  auto sel = v.selected_items("list");
  ASSERT_TRUE(sel.has_value());
  EXPECT_EQ(sel->size(), 1u);
  EXPECT_EQ((*sel)[0], "b");
}

// --- QTableWidget ---

TEST(WidgetDataViewTest, TableHeaders) {
  pj::host::WidgetDataView v(R"({"tbl": {"headers": ["Name", "Value"]}})");
  auto hdrs = v.table_headers("tbl");
  ASSERT_TRUE(hdrs.has_value());
  EXPECT_EQ(hdrs->size(), 2u);
  EXPECT_EQ((*hdrs)[1], "Value");
}

TEST(WidgetDataViewTest, TableRows) {
  pj::host::WidgetDataView v(R"({"tbl": {"rows": [["a", "1"], ["b", "2"]]}})");
  auto rows = v.table_rows("tbl");
  ASSERT_TRUE(rows.has_value());
  EXPECT_EQ(rows->size(), 2u);
  EXPECT_EQ((*rows)[0][0], "a");
  EXPECT_EQ((*rows)[1][1], "2");
}

// --- QLabel ---

TEST(WidgetDataViewTest, Label) {
  pj::host::WidgetDataView v(R"({"lbl": {"label": "Status: OK"}})");
  EXPECT_EQ(v.label("lbl"), "Status: OK");
}

// --- QPushButton ---

TEST(WidgetDataViewTest, ButtonText) {
  pj::host::WidgetDataView v(R"({"btn": {"button_text": "Connect"}})");
  EXPECT_EQ(v.button_text("btn"), "Connect");
}

// --- File picker ---

TEST(WidgetDataViewTest, FilePicker) {
  pj::host::WidgetDataView v(
      R"({"picker": {"button_text": "Browse", "action": "file_picker", "filter": "*.csv", "title": "Open"}})");
  EXPECT_TRUE(v.is_file_picker("picker"));
  EXPECT_EQ(v.file_picker_filter("picker"), "*.csv");
  EXPECT_EQ(v.file_picker_title("picker"), "Open");
  EXPECT_EQ(v.button_text("picker"), "Browse");
}

TEST(WidgetDataViewTest, NotFilePicker) {
  pj::host::WidgetDataView v(R"({"btn": {"button_text": "OK"}})");
  EXPECT_FALSE(v.is_file_picker("btn"));
}

// --- QDialogButtonBox ---

TEST(WidgetDataViewTest, OkEnabled) {
  pj::host::WidgetDataView v(R"({"bb": {"ok_enabled": true}})");
  EXPECT_EQ(v.ok_enabled("bb"), true);
}

// --- QTabWidget ---

TEST(WidgetDataViewTest, TabIndex) {
  pj::host::WidgetDataView v(R"({"tabs": {"tab_index": 1}})");
  EXPECT_EQ(v.tab_index("tabs"), 1);
}

// --- Generic ---

TEST(WidgetDataViewTest, Enabled) {
  pj::host::WidgetDataView v(R"({"w": {"enabled": false}})");
  EXPECT_EQ(v.enabled("w"), false);
}

TEST(WidgetDataViewTest, Visible) {
  pj::host::WidgetDataView v(R"({"w": {"visible": true}})");
  EXPECT_EQ(v.visible("w"), true);
}

// --- Missing widget ---

TEST(WidgetDataViewTest, MissingWidgetReturnsNullopt) {
  pj::host::WidgetDataView v(R"({"a": {"text": "x"}})");
  EXPECT_FALSE(v.text("nonexistent").has_value());
  EXPECT_FALSE(v.checked("nonexistent").has_value());
  EXPECT_FALSE(v.value_int("nonexistent").has_value());
  EXPECT_FALSE(v.items("nonexistent").has_value());
}

// --- Missing field ---

TEST(WidgetDataViewTest, MissingFieldReturnsNullopt) {
  pj::host::WidgetDataView v(R"({"w": {"text": "x"}})");
  EXPECT_FALSE(v.checked("w").has_value());
  EXPECT_FALSE(v.value_int("w").has_value());
  EXPECT_FALSE(v.placeholder("w").has_value());
}

// --- Wrong type ---

TEST(WidgetDataViewTest, WrongTypeReturnsNullopt) {
  pj::host::WidgetDataView v(R"({"w": {"text": 42, "checked": "yes", "value": true}})");
  EXPECT_FALSE(v.text("w").has_value());
  EXPECT_FALSE(v.checked("w").has_value());
  EXPECT_FALSE(v.value_int("w").has_value());
}

// --- Invalid JSON ---

TEST(WidgetDataViewTest, InvalidJsonGraceful) {
  pj::host::WidgetDataView v("not valid json {{{");
  EXPECT_FALSE(v.text("anything").has_value());
  EXPECT_TRUE(v.widget_names().empty());
  EXPECT_FALSE(v.has_widget("anything"));
}

TEST(WidgetDataViewTest, EmptyObject) {
  pj::host::WidgetDataView v("{}");
  EXPECT_TRUE(v.widget_names().empty());
  EXPECT_FALSE(v.has_widget("x"));
}

// --- Enumeration ---

TEST(WidgetDataViewTest, WidgetNames) {
  pj::host::WidgetDataView v(R"({"alpha": {"text": "a"}, "beta": {"text": "b"}})");
  auto names = v.widget_names();
  EXPECT_EQ(names.size(), 2u);
  // JSON object iteration order — just check both names are present
  EXPECT_TRUE(v.has_widget("alpha"));
  EXPECT_TRUE(v.has_widget("beta"));
  EXPECT_FALSE(v.has_widget("gamma"));
}

// --- Raw access ---

TEST(WidgetDataViewTest, RawAccess) {
  pj::host::WidgetDataView v(R"({"w": {"custom_field": 99}})");
  const auto& raw = v.raw();
  EXPECT_TRUE(raw.is_object());
  EXPECT_EQ(raw["w"]["custom_field"], 99);
}
