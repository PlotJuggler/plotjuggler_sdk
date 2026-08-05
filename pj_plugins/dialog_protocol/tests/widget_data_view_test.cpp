// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <pj_plugins/host/widget_data_view.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <utility>
#include <variant>

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

// A cell the reader cannot render as text still owns a column. Skipping it would
// pull every later cell one column left and mis-align the row against its headers.
TEST(WidgetDataViewTest, TableRowsNonStringCellKeepsRowShape) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["a", 1234, "z"], ["b", "2", "y"]]}})");
  auto rows = v.tableRows("tbl");
  ASSERT_TRUE(rows.has_value());
  ASSERT_EQ(rows->size(), 2u);
  ASSERT_EQ((*rows)[0].size(), 3u);
  EXPECT_EQ((*rows)[0][0], "a");
  EXPECT_EQ((*rows)[0][1], "");
  EXPECT_EQ((*rows)[0][2], "z");
}

// --- QTableWidget: typed sort keys ---

TEST(WidgetDataViewTest, TableColumnValuesRoundTrip) {
  constexpr std::int64_t kNanos = 1780000000000000123;
  constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

  PJ::WidgetData wd;
  wd.setTableRows(
      "tbl", {{PJ::TableItem("a"), PJ::TableItem(kNanos, "2026-07-17 10:23"), PJ::TableItem(1.5)},
              {PJ::TableItem("b"), PJ::TableItem(kMax, "lots"), PJ::TableItem(-2.5)}});
  PJ::WidgetDataView v(wd.toJson());

  auto cols = v.tableColumnValues("tbl");
  ASSERT_EQ(cols.size(), 2u);  // column 0 is text-only and carries no key
  EXPECT_EQ(cols.count(0), 0u);

  const auto& keys = cols.at(1);
  ASSERT_EQ(keys.size(), 2u);
  ASSERT_TRUE(keys[0].has_value());
  ASSERT_TRUE(keys[1].has_value());
  EXPECT_EQ(std::get<std::uint64_t>(*keys[0]), static_cast<std::uint64_t>(kNanos));
  EXPECT_EQ(std::get<std::uint64_t>(*keys[1]), kMax);

  const auto& floats = cols.at(2);
  ASSERT_EQ(floats.size(), 2u);
  EXPECT_EQ(std::get<double>(*floats[0]), 1.5);
  EXPECT_EQ(std::get<double>(*floats[1]), -2.5);
}

// JSON has one integer syntax, so the *sign of the value* — not the plugin's C++
// type — decides which alternative survives the wire. A column that straddles zero
// therefore arrives as a mix of int64 and uint64 and any consumer comparing them
// must handle that, not assume one alternative per column.
TEST(WidgetDataViewTest, TableColumnValuesSignednessFollowsTheValue) {
  PJ::WidgetData wd;
  wd.setTableRows("tbl", {{PJ::TableItem(std::int32_t{-5})}, {PJ::TableItem(std::int32_t{10})}});
  PJ::WidgetDataView v(wd.toJson());

  const auto cols = v.tableColumnValues("tbl");
  const auto& keys = cols.at(0);
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(std::get<std::int64_t>(*keys[0]), -5);
  EXPECT_EQ(std::get<std::uint64_t>(*keys[1]), 10u);
}

TEST(WidgetDataViewTest, TableColumnValuesNullEntryIsNullopt) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["3.5"], ["N/A"]], "column_values": {"0": [3.5, null]}}})");
  const auto cols = v.tableColumnValues("tbl");
  const auto& keys = cols.at(0);
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_TRUE(keys[0].has_value());
  EXPECT_FALSE(keys[1].has_value());
}

TEST(WidgetDataViewTest, TableColumnValuesAbsentIsEmpty) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["a", "1"], ["b", "2"]]}})");
  EXPECT_TRUE(v.tableColumnValues("tbl").empty());
  EXPECT_TRUE(v.tableColumnValues("missing").empty());
}

// A short column cannot be zipped against the rows, and guessing an alignment would
// sort some rows by number and the rest by text.
TEST(WidgetDataViewTest, TableColumnValuesCountMismatchIsDropped) {
  PJ::WidgetDataView v(
      R"({"tbl": {"rows": [["a", "1"], ["b", "2"], ["c", "3"]],
                  "column_values": {"0": [1, 2, 3], "1": [1, 2]}}})");
  auto cols = v.tableColumnValues("tbl");
  EXPECT_EQ(cols.size(), 1u);
  EXPECT_EQ(cols.count(1), 0u);
  EXPECT_EQ(cols.at(0).size(), 3u);
}

TEST(WidgetDataViewTest, TableColumnValuesBadKeyIsDropped) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["a"]], "column_values": {"abc": [1], "-1": [2], "1.5": [3], "0": [4]}}})");
  auto cols = v.tableColumnValues("tbl");
  ASSERT_EQ(cols.size(), 1u);
  EXPECT_EQ(std::get<std::uint64_t>(*cols.at(0)[0]), 4u);
}

TEST(WidgetDataViewTest, TableSortIndicatorRoundTrip) {
  PJ::WidgetData wd;
  wd.setTableSortIndicator("tbl", 2, false);
  PJ::WidgetDataView v(wd.toJson());
  ASSERT_TRUE(v.tableSortIndicator("tbl").has_value());
  EXPECT_EQ(*v.tableSortIndicator("tbl"), std::make_pair(2, false));
}

TEST(WidgetDataViewTest, TableSortIndicatorAbsent) {
  PJ::WidgetDataView v(R"({"tbl": {"rows": [["a"]]}})");
  EXPECT_FALSE(v.tableSortIndicator("tbl").has_value());
  EXPECT_FALSE(v.tableSortIndicator("missing").has_value());
}

TEST(WidgetDataViewTest, TableSortIndicatorMalformed) {
  EXPECT_FALSE(PJ::WidgetDataView(R"({"tbl": {"sort_indicator": {"asc": true}}})").tableSortIndicator("tbl"));
  EXPECT_FALSE(PJ::WidgetDataView(R"({"tbl": {"sort_indicator": {"col": 1}}})").tableSortIndicator("tbl"));
  EXPECT_FALSE(
      PJ::WidgetDataView(R"({"tbl": {"sort_indicator": {"col": 1, "asc": "yes"}}})").tableSortIndicator("tbl"));
  EXPECT_FALSE(
      PJ::WidgetDataView(R"({"tbl": {"sort_indicator": {"col": "1", "asc": true}}})").tableSortIndicator("tbl"));
  EXPECT_FALSE(PJ::WidgetDataView(R"({"tbl": {"sort_indicator": 2}})").tableSortIndicator("tbl"));
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

TEST(WidgetDataViewTest, ButtonIconName) {
  PJ::WidgetDataView v(R"({"play_btn": {"button_icon_name": "media-play"}})");
  ASSERT_TRUE(v.buttonIconName("play_btn").has_value());
  EXPECT_EQ(*v.buttonIconName("play_btn"), "media-play");
  EXPECT_FALSE(v.buttonIconName("missing").has_value());
}

// --- Field validity indicator ---

TEST(WidgetDataViewTest, FieldValid) {
  PJ::WidgetDataView v(R"({"editor": {"valid": false, "valid_tooltip": "bad"}})");
  ASSERT_TRUE(v.fieldValid("editor").has_value());
  EXPECT_FALSE(*v.fieldValid("editor"));
  ASSERT_TRUE(v.fieldValidTooltip("editor").has_value());
  EXPECT_EQ(*v.fieldValidTooltip("editor"), "bad");
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

// --- QStackedWidget ---

TEST(WidgetDataViewTest, StackedPageAndIndexRoundTripExposeBothKeys) {
  PJ::WidgetData wd;
  wd.setStackedPage("configuration_stack", "advanced_page").setStackedIndex("configuration_stack", 2);
  PJ::WidgetDataView v(wd.toJson());
  EXPECT_EQ(v.stackedPage("configuration_stack"), "advanced_page");
  EXPECT_EQ(v.stackedIndex("configuration_stack"), 2);
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

TEST(WidgetDataViewTest, CodeCursor) {
  PJ::WidgetDataView v(R"({"editor": {"code_cursor": 12}})");
  EXPECT_EQ(v.codeCursor("editor"), 12);
}

TEST(WidgetDataViewTest, CodeCursorAbsent) {
  PJ::WidgetDataView v(R"({"editor": {"code_content": "x"}})");
  EXPECT_FALSE(v.codeCursor("editor").has_value());
}

TEST(WidgetDataViewTest, CodeCaretTracking) {
  PJ::WidgetDataView v(R"({"editor": {"code_caret_tracking": true}})");
  EXPECT_EQ(v.codeCaretTracking("editor"), true);
}

TEST(WidgetDataViewTest, CodeCaretTrackingAbsent) {
  PJ::WidgetDataView v(R"({"editor": {"code_content": "x"}})");
  EXPECT_FALSE(v.codeCaretTracking("editor").has_value());
}

// WidgetData -> toJson -> WidgetDataView round trips for the dialog-protocol
// additions (deletable lists, placeholders). A key-name mismatch between the setter and the view
// accessor would silently suppress the feature at the plugin boundary.
TEST(WidgetDataViewTest, ListDeletableRoundTrip) {
  PJ::WidgetData wd;
  wd.setListItemsDeletable("lst", true);
  PJ::WidgetDataView v(wd.toJson());
  ASSERT_TRUE(v.listDeletable("lst").has_value());
  EXPECT_TRUE(*v.listDeletable("lst"));
}

TEST(WidgetDataViewTest, ListDeletableAbsent) {
  PJ::WidgetDataView v(R"({"lst": {"items": []}})");
  EXPECT_FALSE(v.listDeletable("lst").has_value());
}

TEST(WidgetDataViewTest, ListPlaceholderRoundTrip) {
  PJ::WidgetData wd;
  wd.setListPlaceholder("lst", "Drop a series here");
  PJ::WidgetDataView v(wd.toJson());
  ASSERT_TRUE(v.listPlaceholder("lst").has_value());
  EXPECT_EQ(*v.listPlaceholder("lst"), "Drop a series here");
}

TEST(WidgetDataViewTest, ChartPlaceholderRoundTrip) {
  PJ::WidgetData wd;
  wd.setChartPlaceholder("chart", "No data yet");
  PJ::WidgetDataView v(wd.toJson());
  ASSERT_TRUE(v.chartPlaceholder("chart").has_value());
  EXPECT_EQ(*v.chartPlaceholder("chart"), "No data yet");
}

TEST(WidgetDataViewTest, TableDeltaRoundTrip) {
  PJ::WidgetData wd;
  wd.appendTableRows("tbl", 9, std::vector<std::vector<std::string>>{{"r1c1", "r1c2"}});
  wd.updateTableCells("tbl", 9, {{2, 0, "upd"}});
  wd.removeTableRows("tbl", 9, {1});
  PJ::WidgetDataView view(wd.toJson());
  auto delta = view.tableDelta("tbl");
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->seq, 9U);
  ASSERT_EQ(delta->append.size(), 1U);
  EXPECT_EQ(delta->append[0][1], "r1c2");
  ASSERT_EQ(delta->update_cells.size(), 1U);
  EXPECT_EQ(delta->update_cells[0].row, 2);
  EXPECT_EQ(delta->update_cells[0].col, 0);
  EXPECT_EQ(delta->update_cells[0].text, "upd");
  EXPECT_EQ(delta->remove_rows, std::vector<int>{1});
}

TEST(WidgetDataViewTest, TableDeltaAbsentYieldsNullopt) {
  PJ::WidgetDataView view(R"({"tbl": {"rows": [["a"]]}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaWithoutSeqYieldsNullopt) {
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"append": [["a"]]}}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaMalformedCellRejectsWholeDelta) {
  // One malformed op poisons the delta: partial application would consume the
  // seq while diverging from the plugin's model.
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 3, "update_cells": [[0, "not-int", "x"]]}}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaNegativeIndexRejectsWholeDelta) {
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 3, "remove_rows": [1, -2]}}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaRemoveRowsNormalizedDescendingUnique) {
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 4, "remove_rows": [2, 7, 2, 5]}}})");
  auto delta = view.tableDelta("tbl");
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->remove_rows, (std::vector<int>{7, 5, 2}));
}

TEST(WidgetDataViewTest, TableDeltaTypedRoundTrip) {
  PJ::WidgetData wd;
  wd.appendTableRows(
      "tbl", 6, std::vector<std::vector<PJ::TableItem>>{{PJ::TableItem(uint64_t{1} << 60, "big"), PJ::TableItem("x")}});
  wd.updateTableCells("tbl", 6, {{1, 0, {2.5, "2.5"}}});
  PJ::WidgetDataView view(wd.toJson());
  auto delta = view.tableDelta("tbl");
  ASSERT_TRUE(delta.has_value());
  ASSERT_EQ(delta->append.size(), 1U);
  EXPECT_EQ(delta->append[0][0], "big");
  ASSERT_TRUE(delta->append_values.contains(0));
  ASSERT_TRUE(delta->append_values.at(0)[0].has_value());
  EXPECT_EQ(std::get<uint64_t>(*delta->append_values.at(0)[0]), uint64_t{1} << 60);
  EXPECT_FALSE(delta->append_values.contains(1));
  ASSERT_EQ(delta->update_cells.size(), 1U);
  ASSERT_TRUE(delta->update_cells[0].value.has_value());
  EXPECT_EQ(std::get<double>(*delta->update_cells[0].value), 2.5);
}

TEST(WidgetDataViewTest, TableDeltaMisalignedAppendValuesRejectsWholeDelta) {
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 2, "append": [["a"]], "append_values": {"0": [1, 2]}}}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaBadUpdateValueTypeRejectsWholeDelta) {
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 2, "update_cells": [[0, 0, "x", "not-num"]]}}})");
  EXPECT_FALSE(view.tableDelta("tbl").has_value());
}

TEST(WidgetDataViewTest, TableDeltaNullUpdateValueMeansKeyless) {
  // NaN/Inf keys serialize as JSON null (nlohmann's dump); per TableItem's
  // documented semantics such a cell arrives keyless — not a fatal delta.
  PJ::WidgetDataView view(R"({"tbl": {"table_delta": {"seq": 2, "update_cells": [[0, 0, "N/A", null]]}}})");
  auto delta = view.tableDelta("tbl");
  ASSERT_TRUE(delta.has_value());
  ASSERT_EQ(delta->update_cells.size(), 1U);
  EXPECT_FALSE(delta->update_cells[0].value.has_value());
}

// --- QTreeWidget ---

namespace {

void expectTreeItemsRejected(std::string_view json, std::initializer_list<std::string_view> reason_fragments) {
  PJ::WidgetDataView view(json);
  std::string reason;
  EXPECT_FALSE(view.treeItems("tree", &reason).has_value());
  EXPECT_FALSE(reason.empty());
  for (const auto fragment : reason_fragments) {
    EXPECT_NE(reason.find(fragment), std::string::npos) << reason;
  }
}

}  // namespace

TEST(WidgetDataViewTest, TreeWriterViewRoundTripPreservesEveryFieldAndArrayOrder) {
  constexpr std::uint64_t kLargeSortKey = std::uint64_t{1} << 60;
  PJ::WidgetData data;
  data.setTreeHeaders("tree", {"Topic", "Type"})
      .setTreeItems(
          "tree", {{"child-b",
                    "root",
                    {{"B", PJ::NumericValue(-4.5), "second sibling", "topic"}},
                    false,
                    false,
                    PJ::TreeCheckState::Unchecked,
                    true},
                   {"root",
                    "",
                    {{"Sensors", PJ::NumericValue(kLargeSortKey), "root tooltip", "folder"}},
                    true,
                    true,
                    PJ::TreeCheckState::PartiallyChecked,
                    true},
                   {"child-a",
                    "root",
                    {{"A", std::nullopt, "", ""}, {"sensor_msgs/Imu", PJ::NumericValue(7), "type", "schema"}},
                    true,
                    true,
                    PJ::TreeCheckState::Checked,
                    false},
                   {"plain", "", {}, true, true, PJ::TreeCheckState::None, false}})
      .setTreeSelectedIds("tree", {"child-a", "not-loaded"})
      .setTreeExpandedIds("tree", {"root", "not-loaded"})
      .setTreeVisibleIds("tree", {"child-a", "not-loaded"})
      .setTreeSelectionMode("tree", true);

  PJ::WidgetDataView view(data.toJson());
  EXPECT_EQ(view.treeHeaders("tree"), (std::vector<std::string>{"Topic", "Type"}));
  std::string validation_error = "stale";
  auto items = view.treeItems("tree", &validation_error);
  ASSERT_TRUE(items.has_value()) << validation_error;
  EXPECT_TRUE(validation_error.empty());
  ASSERT_EQ(items->size(), 4U);
  // Flat array order, including child-before-parent and sibling B-before-A, is
  // preserved exactly. The host uses that order within each sibling group.
  EXPECT_EQ((*items)[0].id, "child-b");
  EXPECT_EQ((*items)[1].id, "root");
  EXPECT_EQ((*items)[2].id, "child-a");
  EXPECT_EQ((*items)[0].parent_id, "root");
  EXPECT_FALSE((*items)[0].enabled);
  EXPECT_FALSE((*items)[0].selectable);
  EXPECT_TRUE((*items)[0].may_have_children);
  EXPECT_EQ((*items)[0].check_state, PJ::TreeCheckState::Unchecked);
  ASSERT_EQ((*items)[0].cells.size(), 1U);
  EXPECT_EQ((*items)[0].cells[0].text, "B");
  ASSERT_TRUE((*items)[0].cells[0].sort_value.has_value());
  EXPECT_EQ(std::get<double>(*(*items)[0].cells[0].sort_value), -4.5);
  EXPECT_EQ((*items)[0].cells[0].tooltip, "second sibling");
  EXPECT_EQ((*items)[0].cells[0].icon, "topic");
  EXPECT_EQ((*items)[1].check_state, PJ::TreeCheckState::PartiallyChecked);
  EXPECT_EQ(std::get<std::uint64_t>(*(*items)[1].cells[0].sort_value), kLargeSortKey);
  EXPECT_EQ((*items)[2].check_state, PJ::TreeCheckState::Checked);
  EXPECT_EQ((*items)[3].check_state, PJ::TreeCheckState::None);

  // Unknown IDs are deliberately exposed unchanged; pruning belongs to the host.
  EXPECT_EQ(view.treeSelectedIds("tree"), (std::vector<std::string>{"child-a", "not-loaded"}));
  EXPECT_EQ(view.treeExpandedIds("tree"), (std::vector<std::string>{"root", "not-loaded"}));
  auto visibility = view.treeVisibilityUpdate("tree");
  ASSERT_TRUE(visibility.has_value());
  EXPECT_EQ(visibility->mode, PJ::WidgetDataView::TreeVisibilityUpdate::Mode::Filter);
  EXPECT_EQ(visibility->ids, (std::vector<std::string>{"child-a", "not-loaded"}));
  EXPECT_EQ(view.treeMultiSelection("tree"), true);
}

TEST(WidgetDataViewTest, TreeRaggedCellsAreAccepted) {
  PJ::WidgetDataView view(
      R"({"tree":{"tree_items":[{"id":"root","parent_id":"","cells":[{"text":"one"}]},{"id":"other","parent_id":"","cells":[{"text":"one"},{"text":"two"}]}]}})");
  auto items = view.treeItems("tree");
  ASSERT_TRUE(items.has_value());
  ASSERT_EQ(items->size(), 2U);
  EXPECT_EQ((*items)[0].cells.size(), 1U);
  EXPECT_EQ((*items)[1].cells.size(), 2U);
}

TEST(WidgetDataViewTest, TreeUnicodeAndEscapedByteIdsRoundTripWithoutIdentityChanges) {
  const std::string root_id = "root-\xE2\x98\x83-\x01";
  const std::string child_id = "child-\xF0\x9F\x9A\x80-\x1b";
  PJ::WidgetData data;
  data.setTreeItems(
      "tree", {{root_id, "", {{"Root", std::nullopt, "", ""}}, true, true, PJ::TreeCheckState::None, false},
               {child_id, root_id, {{"Child", std::nullopt, "", ""}}, true, true, PJ::TreeCheckState::None, false}});

  const std::string encoded = data.toJson();
  EXPECT_NE(encoded.find("\\u0001"), std::string::npos);
  EXPECT_NE(encoded.find("\\u001b"), std::string::npos);
  const auto items = PJ::WidgetDataView(encoded).treeItems("tree");
  ASSERT_TRUE(items.has_value());
  ASSERT_EQ(items->size(), 2U);
  EXPECT_EQ((*items)[0].id, root_id);
  EXPECT_EQ((*items)[1].id, child_id);
  EXPECT_EQ((*items)[1].parent_id, root_id);
}

TEST(WidgetDataViewTest, TreeDuplicateIdsRejectWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"dup","parent_id":"","cells":[]},{"id":"dup","parent_id":"","cells":[]}]}})",
      {"duplicate id", "'dup'"});
}

TEST(WidgetDataViewTest, TreeDuplicateParentIdReferencedByEarlierChildRejectsWholeSnapshot) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"child","parent_id":"dup","cells":[]},{"id":"dup","parent_id":"","cells":[]},{"id":"dup","parent_id":"","cells":[]}]}})",
      {"duplicate id", "'dup'"});
}

TEST(WidgetDataViewTest, TreeEmptyIdRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"","parent_id":"","cells":[]}]}})", {"tree_items[0].id", "must not be empty"});
}

TEST(WidgetDataViewTest, TreeMissingParentRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"child","parent_id":"missing","cells":[]},{"id":"valid","parent_id":"","cells":[]}]}})",
      {"'child'", "missing parent"});
}

TEST(WidgetDataViewTest, TreeSelfParentRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"self","parent_id":"self","cells":[]}]}})", {"'self'", "own parent"});
}

TEST(WidgetDataViewTest, TreeLongerParentCycleRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"a","parent_id":"c","cells":[]},{"id":"b","parent_id":"a","cells":[]},{"id":"c","parent_id":"b","cells":[]}]}})",
      {"parent cycle", "'a'"});
}

TEST(WidgetDataViewTest, TreeBadCheckStateRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"valid","parent_id":"","cells":[]},{"id":"bad","parent_id":"","cells":[],"check_state":"partial"}]}})",
      {"tree_items[1].check_state", "'partial'"});
}

TEST(WidgetDataViewTest, TreeBadSortValueRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"valid","parent_id":"","cells":[]},{"id":"bad","parent_id":"","cells":[{"text":"x","sort_value":"large"}]}]}})",
      {"tree_items[1].cells[0].sort_value", "number"});
}

TEST(WidgetDataViewTest, TreeBadCellTextRejectsWholeSnapshotWithReason) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"valid","parent_id":"","cells":[]},{"id":"bad","parent_id":"","cells":[{"text":12}]}]}})",
      {"tree_items[1].cells[0].text", "string"});
}

TEST(WidgetDataViewTest, TreeDeeplyNestedBadSortValueRejectsWholeSnapshotWithCellPath) {
  expectTreeItemsRejected(
      R"({"tree":{"tree_items":[{"id":"root","parent_id":"","cells":[]},{"id":"branch","parent_id":"root","cells":[]},{"id":"leaf","parent_id":"branch","cells":[]},{"id":"deep","parent_id":"leaf","cells":[{"text":"a"},{"text":"b"},{"text":"c","sort_value":{"bad":true}}]}]}})",
      {"tree_items[3].cells[2].sort_value", "number"});
}

TEST(WidgetDataViewTest, TreeStateOnlyChannelsAreIndependentAndEmptyArraysClear) {
  PJ::WidgetData data;
  data.setTreeHeaders("tree", {"Name"})
      .setTreeSelectedIds("tree", {})
      .setTreeExpandedIds("tree", {})
      .setTreeSelectionMode("tree", false);
  PJ::WidgetDataView view(data.toJson());
  EXPECT_FALSE(view.treeItems("tree").has_value());
  ASSERT_TRUE(view.treeSelectedIds("tree").has_value());
  EXPECT_TRUE(view.treeSelectedIds("tree")->empty());
  ASSERT_TRUE(view.treeExpandedIds("tree").has_value());
  EXPECT_TRUE(view.treeExpandedIds("tree")->empty());
  EXPECT_EQ(view.treeHeaders("tree"), (std::vector<std::string>{"Name"}));
  EXPECT_EQ(view.treeMultiSelection("tree"), false);
}

TEST(WidgetDataViewTest, TreeVisibilityDistinguishesAbsentEmptyFilterAndReset) {
  PJ::WidgetDataView absent(R"({"tree":{"tree_selected_ids":[]}})");
  EXPECT_FALSE(absent.treeVisibilityUpdate("tree").has_value());

  PJ::WidgetData filtered;
  filtered.setTreeVisibleIds("tree", {});
  auto filter = PJ::WidgetDataView(filtered.toJson()).treeVisibilityUpdate("tree");
  ASSERT_TRUE(filter.has_value());
  EXPECT_EQ(filter->mode, PJ::WidgetDataView::TreeVisibilityUpdate::Mode::Filter);
  EXPECT_TRUE(filter->ids.empty());

  PJ::WidgetData reset;
  reset.clearTreeVisibleIds("tree");
  auto clear = PJ::WidgetDataView(reset.toJson()).treeVisibilityUpdate("tree");
  ASSERT_TRUE(clear.has_value());
  EXPECT_EQ(clear->mode, PJ::TreeVisibilityUpdate::Mode::Reset);
  EXPECT_TRUE(clear->ids.empty());
}

TEST(WidgetDataViewTest, TreeStringArrayChannelsRejectEveryMalformedArrayAtomically) {
  PJ::WidgetDataView view(
      R"({"tree":{"tree_headers":["Name",1,"Type"],"tree_selected_ids":[1],"tree_expanded_ids":[1],"tree_visible_ids":[1,2]}})");
  EXPECT_FALSE(view.treeHeaders("tree").has_value());
  EXPECT_FALSE(view.treeSelectedIds("tree").has_value());
  EXPECT_FALSE(view.treeExpandedIds("tree").has_value());
  EXPECT_FALSE(view.treeVisibilityUpdate("tree").has_value());
}
