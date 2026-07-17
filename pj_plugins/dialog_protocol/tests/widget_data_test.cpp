// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
#include <vector>

using PJ::WidgetData;
using json = nlohmann::json;

static json parse(const WidgetData& wd) {
  return json::parse(wd.toJson());
}

// --- Text properties ---

TEST(WidgetDataTest, SetText) {
  WidgetData wd;
  wd.setText("host_input", "localhost");
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["text"], "localhost");
}

TEST(WidgetDataTest, SetPlaceholder) {
  WidgetData wd;
  wd.setPlaceholder("host_input", "e.g. 127.0.0.1");
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["placeholder"], "e.g. 127.0.0.1");
}

TEST(WidgetDataTest, SetReadOnly) {
  WidgetData wd;
  wd.setReadOnly("output_field", true);
  auto j = parse(wd);
  EXPECT_EQ(j["output_field"]["read_only"], true);
}

// --- ComboBox ---

TEST(WidgetDataTest, SetCurrentIndex) {
  WidgetData wd;
  wd.setCurrentIndex("protocol_combo", 2);
  auto j = parse(wd);
  EXPECT_EQ(j["protocol_combo"]["current_index"], 2);
}

TEST(WidgetDataTest, SetItems) {
  WidgetData wd;
  std::vector<std::string> items = {"TCP", "UDP", "WebSocket"};
  wd.setItems("protocol_combo", items);
  auto j = parse(wd);
  ASSERT_EQ(j["protocol_combo"]["items"].size(), 3u);
  EXPECT_EQ(j["protocol_combo"]["items"][0], "TCP");
  EXPECT_EQ(j["protocol_combo"]["items"][2], "WebSocket");
}

// --- CheckBox ---

TEST(WidgetDataTest, SetChecked) {
  WidgetData wd;
  wd.setChecked("use_tls", true);
  auto j = parse(wd);
  EXPECT_EQ(j["use_tls"]["checked"], true);
}

// --- SpinBox ---

TEST(WidgetDataTest, SetIntValue) {
  WidgetData wd;
  wd.setValue("port_input", 8080);
  auto j = parse(wd);
  EXPECT_EQ(j["port_input"]["value"], 8080);
}

TEST(WidgetDataTest, SetDoubleValue) {
  WidgetData wd;
  wd.setValue("rate_input", 3.14);
  auto j = parse(wd);
  EXPECT_DOUBLE_EQ(j["rate_input"]["value"].get<double>(), 3.14);
}

TEST(WidgetDataTest, SetRange) {
  WidgetData wd;
  wd.setRange("port_input", 1, 65535);
  auto j = parse(wd);
  EXPECT_EQ(j["port_input"]["min"], 1);
  EXPECT_EQ(j["port_input"]["max"], 65535);
}

// --- ListWidget ---

TEST(WidgetDataTest, SetListItems) {
  WidgetData wd;
  std::vector<std::string> items = {"topic_a", "topic_b"};
  wd.setListItems("topic_list", items);
  auto j = parse(wd);
  ASSERT_EQ(j["topic_list"]["list_items"].size(), 2u);
  EXPECT_EQ(j["topic_list"]["list_items"][1], "topic_b");
}

TEST(WidgetDataTest, SetSelectedItems) {
  WidgetData wd;
  std::vector<std::string> sel = {"topic_a"};
  wd.setSelectedItems("topic_list", sel);
  auto j = parse(wd);
  ASSERT_EQ(j["topic_list"]["selected_items"].size(), 1u);
  EXPECT_EQ(j["topic_list"]["selected_items"][0], "topic_a");
}

// --- TableWidget ---

TEST(WidgetDataTest, SetTableHeaders) {
  WidgetData wd;
  std::vector<std::string> headers = {"Name", "Type", "Rate"};
  wd.setTableHeaders("data_table", headers);
  auto j = parse(wd);
  ASSERT_EQ(j["data_table"]["headers"].size(), 3u);
  EXPECT_EQ(j["data_table"]["headers"][1], "Type");
}

TEST(WidgetDataTest, SetTableRows) {
  WidgetData wd;
  std::vector<std::vector<std::string>> rows = {{"imu", "sensor", "100"}, {"gps", "sensor", "10"}};
  wd.setTableRows("data_table", rows);
  auto j = parse(wd);
  ASSERT_EQ(j["data_table"]["rows"].size(), 2u);
  EXPECT_EQ(j["data_table"]["rows"][0][0], "imu");
  EXPECT_EQ(j["data_table"]["rows"][1][2], "10");
}

// --- TableWidget: typed rows (TableItem) ---

// The string overload is the compatibility baseline: it must stay a pure-text
// emission, or an old host would start seeing a key it cannot interpret.
TEST(WidgetDataTest, SetTableRowsStringOverloadEmitsNoColumnValues) {
  WidgetData wd;
  std::vector<std::vector<std::string>> rows = {{"imu", "100"}, {"gps", "10"}};
  wd.setTableRows("data_table", rows);
  auto j = parse(wd);
  EXPECT_FALSE(j["data_table"].contains("column_values"));
  EXPECT_EQ(j["data_table"], json::parse(R"({"rows": [["imu", "100"], ["gps", "10"]]})"));
}

// Non-finite keys cannot cross the wire: nlohmann's dump() serializes NaN and
// ±infinity as null, so the host sees a keyless cell (text rank). Pinned here so
// the degradation stays deliberate and documented rather than accidental.
TEST(WidgetDataTest, SetTableRowsTypedNonFiniteKeysSerializeAsNull) {
  WidgetData wd;
  wd.setTableRows(
      "t", {{PJ::TableItem(std::numeric_limits<double>::infinity(), "inf")},
            {PJ::TableItem(-std::numeric_limits<double>::infinity(), "-inf")},
            {PJ::TableItem(std::numeric_limits<double>::quiet_NaN(), "nan")}});
  auto j = parse(wd);
  const auto& col = j["t"]["column_values"]["0"];
  ASSERT_EQ(col.size(), 3u);
  EXPECT_TRUE(col[0].is_null());
  EXPECT_TRUE(col[1].is_null());
  EXPECT_TRUE(col[2].is_null());
}

// A typed delivery followed by a string delivery on the SAME widget must drop the
// stale keys: with a matching row count the host would otherwise pair the new
// rows with the old keys and silently sort them wrong.
TEST(WidgetDataTest, SetTableRowsStringOverloadClearsStaleColumnValues) {
  WidgetData wd;
  std::vector<std::vector<PJ::TableItem>> typed = {
      {PJ::TableItem("a"), PJ::TableItem(std::uint64_t{2})}, {PJ::TableItem("b"), PJ::TableItem(std::uint64_t{1})}};
  wd.setTableRows("t", typed);
  ASSERT_TRUE(parse(wd)["t"].contains("column_values"));
  std::vector<std::vector<std::string>> plain = {{"c", "20"}, {"d", "10"}};
  wd.setTableRows("t", plain);
  EXPECT_FALSE(parse(wd)["t"].contains("column_values"));
}

// `rows` is what an old host reads, so the typed overload must produce byte-identical
// display text — the sort keys ride alongside, never inside it.
TEST(WidgetDataTest, SetTableRowsTypedEmitsSameDisplayTextAsStringOverload) {
  WidgetData typed;
  typed.setTableRows(
      "t", {{PJ::TableItem("imu"), PJ::TableItem(std::uint64_t{100})},
            {PJ::TableItem("gps"), PJ::TableItem(std::uint64_t{10})}});

  WidgetData text;
  std::vector<std::vector<std::string>> rows = {{"imu", "100"}, {"gps", "10"}};
  text.setTableRows("t", rows);

  EXPECT_EQ(parse(typed)["t"]["rows"], parse(text)["t"]["rows"]);
}

// Only columns that carry a key appear: a text column must not pay for the feature,
// and its absence is how the host knows to sort that column by text.
TEST(WidgetDataTest, SetTableRowsTypedColumnValuesAreSparse) {
  WidgetData wd;
  wd.setTableRows(
      "t2", {{PJ::TableItem("chan_a"), PJ::TableItem("mcap"), PJ::TableItem(std::uint64_t{1234})},
             {PJ::TableItem("chan_b"), PJ::TableItem("mcap"), PJ::TableItem(std::uint64_t{720})}});
  wd.setTableSortIndicator("t2", 2, false);

  EXPECT_EQ(parse(wd), json::parse(R"({"t2": {
        "column_values": {"2": [1234, 720]},
        "rows": [["chan_a", "mcap", "1234"], ["chan_b", "mcap", "720"]],
        "sort_indicator": {"asc": false, "col": 2}
      }})"));
}

TEST(WidgetDataTest, SetTableRowsTypedWithNoValuesOmitsColumnValues) {
  WidgetData wd;
  wd.setTableRows("t", {{PJ::TableItem("a"), PJ::TableItem("b")}, {PJ::TableItem("c"), PJ::TableItem("d")}});
  auto j = parse(wd);
  EXPECT_FALSE(j["t"].contains("column_values"));
  ASSERT_EQ(j["t"]["rows"].size(), 2u);
  EXPECT_EQ(j["t"]["rows"][1][0], "c");
}

// The reason the key exists. An int64 nanosecond timestamp and a uint64 byte count
// both exceed 2^53, so a sort key routed through a double would silently round —
// producing ties between distinct rows and a wrong answer to "which is largest".
TEST(WidgetDataTest, SetTableRowsTypedInt64AndUint64SurviveExactly) {
  constexpr std::int64_t kNanos = 1780000000000000123;
  constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

  WidgetData wd;
  wd.setTableRows("t", {{PJ::TableItem(kNanos)}, {PJ::TableItem(kMax)}});
  auto j = parse(wd);

  const auto& col = j["t"]["column_values"]["0"];
  ASSERT_EQ(col.size(), 2u);
  EXPECT_EQ(col[0].get<std::int64_t>(), kNanos);
  EXPECT_EQ(col[1].get<std::uint64_t>(), kMax);
  // Exact, not merely close: the same timestamp forced through a double lands on
  // ...0000000000000000. Anything that reintroduces a double here is a regression.
  EXPECT_NE(static_cast<std::int64_t>(static_cast<double>(kNanos)), kNanos);
}

TEST(WidgetDataTest, SetTableRowsTypedDoublesSurviveExactly) {
  constexpr double kUlogParam = 560535533.0;
  constexpr double kMax = std::numeric_limits<double>::max();

  WidgetData wd;
  // The lossy "%g" rendering next to the exact value is the point of TableItem:
  // text and value are different information, not duplicated information.
  wd.setTableRows("t", {{PJ::TableItem(kUlogParam, "5.60536e+08")}, {PJ::TableItem(kMax, "1.79769e+308")}});
  auto j = parse(wd);

  const auto& col = j["t"]["column_values"]["0"];
  ASSERT_EQ(col.size(), 2u);
  EXPECT_EQ(col[0].get<double>(), kUlogParam);
  EXPECT_EQ(col[1].get<double>(), kMax);
  EXPECT_EQ(j["t"]["rows"][0][0], "5.60536e+08");
}

TEST(WidgetDataTest, SetTableRowsTypedFloatSurvivesExactly) {
  WidgetData wd;
  wd.setTableRows("t", {{PJ::TableItem(1.5f)}, {PJ::TableItem(-0.25f)}});
  auto j = parse(wd);
  const auto& col = j["t"]["column_values"]["0"];
  ASSERT_EQ(col.size(), 2u);
  EXPECT_EQ(col[0].get<double>(), 1.5);
  EXPECT_EQ(col[1].get<double>(), -0.25);
}

// A hidden key: what the user reads and what the column orders by need not match.
TEST(WidgetDataTest, SetTableRowsTypedHiddenSortKey) {
  constexpr std::int64_t kNanos = 1784283780000000000;

  WidgetData wd;
  wd.setTableRows("t", {{PJ::TableItem(kNanos, "2026-07-17 10:23")}});
  auto j = parse(wd);
  EXPECT_EQ(j["t"]["rows"][0][0], "2026-07-17 10:23");
  EXPECT_EQ(j["t"]["column_values"]["0"][0].get<std::int64_t>(), kNanos);
}

// ulog mixes real numbers with "N/A" in one column. The keyless cell must hold its
// slot as null: shifting it would re-key every row below it.
TEST(WidgetDataTest, SetTableRowsTypedValuelessCellIsNull) {
  WidgetData wd;
  wd.setTableRows("t", {{PJ::TableItem(3.5)}, {PJ::TableItem("N/A")}, {PJ::TableItem(1.5)}});
  auto j = parse(wd);

  const auto& col = j["t"]["column_values"]["0"];
  ASSERT_EQ(col.size(), 3u);
  EXPECT_EQ(col[0].get<double>(), 3.5);
  EXPECT_TRUE(col[1].is_null());
  EXPECT_EQ(col[2].get<double>(), 1.5);
  EXPECT_EQ(j["t"]["rows"][1][0], "N/A");
}

TEST(WidgetDataTest, SetTableSortIndicator) {
  WidgetData wd;
  wd.setTableSortIndicator("t", 3, true);
  auto j = parse(wd);
  EXPECT_EQ(j["t"]["sort_indicator"]["col"], 3);
  EXPECT_EQ(j["t"]["sort_indicator"]["asc"], true);
}

// --- Label / Button ---

TEST(WidgetDataTest, SetLabel) {
  WidgetData wd;
  wd.setLabel("status_label", "Connected");
  auto j = parse(wd);
  EXPECT_EQ(j["status_label"]["label"], "Connected");
}

TEST(WidgetDataTest, SetButtonText) {
  WidgetData wd;
  wd.setButtonText("connect_btn", "Disconnect");
  auto j = parse(wd);
  EXPECT_EQ(j["connect_btn"]["button_text"], "Disconnect");
}

TEST(WidgetDataTest, SetButtonIconNamed) {
  WidgetData wd;
  wd.setButtonIconNamed("play_btn", "media-play");
  auto j = parse(wd);
  EXPECT_EQ(j["play_btn"]["button_icon_name"], "media-play");
}

// --- Field validity indicator ---

TEST(WidgetDataTest, SetFieldValid) {
  WidgetData wd;
  wd.setFieldValid("lua_editor", false, "syntax error on line 3");
  auto j = parse(wd);
  EXPECT_EQ(j["lua_editor"]["valid"], false);
  EXPECT_EQ(j["lua_editor"]["valid_tooltip"], "syntax error on line 3");
}

TEST(WidgetDataTest, SetFieldValidDefaultTooltip) {
  WidgetData wd;
  wd.setFieldValid("lua_editor", true);
  auto j = parse(wd);
  EXPECT_EQ(j["lua_editor"]["valid"], true);
  EXPECT_EQ(j["lua_editor"]["valid_tooltip"], "");
}

// --- File picker ---

TEST(WidgetDataTest, SetFilePicker) {
  WidgetData wd;
  wd.setFilePicker("cert_btn", "Browse...", "*.pem *.crt", "Select Certificate");
  auto j = parse(wd);
  EXPECT_EQ(j["cert_btn"]["action"], "file_picker");
  EXPECT_EQ(j["cert_btn"]["filter"], "*.pem *.crt");
  EXPECT_EQ(j["cert_btn"]["title"], "Select Certificate");
  EXPECT_EQ(j["cert_btn"]["button_text"], "Browse...");
}

// --- DialogButtonBox ---

TEST(WidgetDataTest, SetOkEnabled) {
  WidgetData wd;
  wd.setOkEnabled(false);
  auto j = parse(wd);
  EXPECT_EQ(j["buttonBox"]["ok_enabled"], false);
}

// --- TabWidget ---

TEST(WidgetDataTest, SetTabIndex) {
  WidgetData wd;
  wd.setTabIndex("tab_widget", 1);
  auto j = parse(wd);
  EXPECT_EQ(j["tab_widget"]["tab_index"], 1);
}

// --- Generic ---

TEST(WidgetDataTest, SetEnabled) {
  WidgetData wd;
  wd.setEnabled("connect_btn", false);
  auto j = parse(wd);
  EXPECT_EQ(j["connect_btn"]["enabled"], false);
}

TEST(WidgetDataTest, SetVisible) {
  WidgetData wd;
  wd.setVisible("cert_btn", true);
  auto j = parse(wd);
  EXPECT_EQ(j["cert_btn"]["visible"], true);
}

// --- Multiple properties on same widget ---

TEST(WidgetDataTest, MergePropertiesOnSameWidget) {
  WidgetData wd;
  wd.setText("host_input", "localhost");
  wd.setPlaceholder("host_input", "e.g. 127.0.0.1");
  wd.setReadOnly("host_input", false);
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["text"], "localhost");
  EXPECT_EQ(j["host_input"]["placeholder"], "e.g. 127.0.0.1");
  EXPECT_EQ(j["host_input"]["read_only"], false);
}

// --- Clear ---

TEST(WidgetDataTest, Clear) {
  WidgetData wd;
  wd.setText("host_input", "localhost");
  EXPECT_NE(parse(wd).size(), 0u);
  wd.clear();
  EXPECT_EQ(parse(wd).size(), 0u);
}

// --- Chaining ---

TEST(WidgetDataTest, Chaining) {
  WidgetData wd;
  auto json_str = wd.setText("host", "x").setValue("port", 80).setChecked("tls", true).toJson();
  auto j = json::parse(json_str);
  EXPECT_EQ(j["host"]["text"], "x");
  EXPECT_EQ(j["port"]["value"], 80);
  EXPECT_EQ(j["tls"]["checked"], true);
}

TEST(WidgetDataTest, SetCodeCursor) {
  WidgetData wd;
  wd.setCodeContent("editor", "robot ==").setCodeCursor("editor", 8);
  auto j = parse(wd);
  EXPECT_EQ(j["editor"]["code_content"], "robot ==");
  EXPECT_EQ(j["editor"]["code_cursor"], 8);
}

TEST(WidgetDataTest, SetCodeCaretTracking) {
  WidgetData wd;
  wd.setCodeCaretTracking("editor");
  auto j = parse(wd);
  EXPECT_EQ(j["editor"]["code_caret_tracking"], true);
}

TEST(WidgetDataTest, SetCodeCaretTrackingExplicitFalse) {
  WidgetData wd;
  wd.setCodeCaretTracking("editor", false);
  auto j = parse(wd);
  EXPECT_EQ(j["editor"]["code_caret_tracking"], false);
}
