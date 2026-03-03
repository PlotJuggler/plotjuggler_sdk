#include <PJ/sdk/widget_data.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using PJ::sdk::WidgetData;
using json = nlohmann::json;

static json parse(const WidgetData& wd) { return json::parse(wd.to_json()); }

// --- Text properties ---

TEST(WidgetDataTest, SetText) {
  WidgetData wd;
  wd.set_text("host_input", "localhost");
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["text"], "localhost");
}

TEST(WidgetDataTest, SetPlaceholder) {
  WidgetData wd;
  wd.set_placeholder("host_input", "e.g. 127.0.0.1");
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["placeholder"], "e.g. 127.0.0.1");
}

TEST(WidgetDataTest, SetReadOnly) {
  WidgetData wd;
  wd.set_read_only("output_field", true);
  auto j = parse(wd);
  EXPECT_EQ(j["output_field"]["read_only"], true);
}

// --- ComboBox ---

TEST(WidgetDataTest, SetCurrentIndex) {
  WidgetData wd;
  wd.set_current_index("protocol_combo", 2);
  auto j = parse(wd);
  EXPECT_EQ(j["protocol_combo"]["current_index"], 2);
}

TEST(WidgetDataTest, SetItems) {
  WidgetData wd;
  std::vector<std::string> items = {"TCP", "UDP", "WebSocket"};
  wd.set_items("protocol_combo", items);
  auto j = parse(wd);
  ASSERT_EQ(j["protocol_combo"]["items"].size(), 3u);
  EXPECT_EQ(j["protocol_combo"]["items"][0], "TCP");
  EXPECT_EQ(j["protocol_combo"]["items"][2], "WebSocket");
}

// --- CheckBox ---

TEST(WidgetDataTest, SetChecked) {
  WidgetData wd;
  wd.set_checked("use_tls", true);
  auto j = parse(wd);
  EXPECT_EQ(j["use_tls"]["checked"], true);
}

// --- SpinBox ---

TEST(WidgetDataTest, SetIntValue) {
  WidgetData wd;
  wd.set_value("port_input", 8080);
  auto j = parse(wd);
  EXPECT_EQ(j["port_input"]["value"], 8080);
}

TEST(WidgetDataTest, SetDoubleValue) {
  WidgetData wd;
  wd.set_value("rate_input", 3.14);
  auto j = parse(wd);
  EXPECT_DOUBLE_EQ(j["rate_input"]["value"].get<double>(), 3.14);
}

TEST(WidgetDataTest, SetRange) {
  WidgetData wd;
  wd.set_range("port_input", 1, 65535);
  auto j = parse(wd);
  EXPECT_EQ(j["port_input"]["min"], 1);
  EXPECT_EQ(j["port_input"]["max"], 65535);
}

// --- ListWidget ---

TEST(WidgetDataTest, SetListItems) {
  WidgetData wd;
  std::vector<std::string> items = {"topic_a", "topic_b"};
  wd.set_list_items("topic_list", items);
  auto j = parse(wd);
  ASSERT_EQ(j["topic_list"]["list_items"].size(), 2u);
  EXPECT_EQ(j["topic_list"]["list_items"][1], "topic_b");
}

TEST(WidgetDataTest, SetSelectedItems) {
  WidgetData wd;
  std::vector<std::string> sel = {"topic_a"};
  wd.set_selected_items("topic_list", sel);
  auto j = parse(wd);
  ASSERT_EQ(j["topic_list"]["selected_items"].size(), 1u);
  EXPECT_EQ(j["topic_list"]["selected_items"][0], "topic_a");
}

// --- TableWidget ---

TEST(WidgetDataTest, SetTableHeaders) {
  WidgetData wd;
  std::vector<std::string> headers = {"Name", "Type", "Rate"};
  wd.set_table_headers("data_table", headers);
  auto j = parse(wd);
  ASSERT_EQ(j["data_table"]["headers"].size(), 3u);
  EXPECT_EQ(j["data_table"]["headers"][1], "Type");
}

TEST(WidgetDataTest, SetTableRows) {
  WidgetData wd;
  std::vector<std::vector<std::string>> rows = {{"imu", "sensor", "100"}, {"gps", "sensor", "10"}};
  wd.set_table_rows("data_table", rows);
  auto j = parse(wd);
  ASSERT_EQ(j["data_table"]["rows"].size(), 2u);
  EXPECT_EQ(j["data_table"]["rows"][0][0], "imu");
  EXPECT_EQ(j["data_table"]["rows"][1][2], "10");
}

// --- Label / Button ---

TEST(WidgetDataTest, SetLabel) {
  WidgetData wd;
  wd.set_label("status_label", "Connected");
  auto j = parse(wd);
  EXPECT_EQ(j["status_label"]["label"], "Connected");
}

TEST(WidgetDataTest, SetButtonText) {
  WidgetData wd;
  wd.set_button_text("connect_btn", "Disconnect");
  auto j = parse(wd);
  EXPECT_EQ(j["connect_btn"]["button_text"], "Disconnect");
}

// --- File picker ---

TEST(WidgetDataTest, SetFilePicker) {
  WidgetData wd;
  wd.set_file_picker("cert_btn", "Browse...", "*.pem *.crt", "Select Certificate");
  auto j = parse(wd);
  EXPECT_EQ(j["cert_btn"]["action"], "file_picker");
  EXPECT_EQ(j["cert_btn"]["filter"], "*.pem *.crt");
  EXPECT_EQ(j["cert_btn"]["title"], "Select Certificate");
  EXPECT_EQ(j["cert_btn"]["button_text"], "Browse...");
}

// --- DialogButtonBox ---

TEST(WidgetDataTest, SetOkEnabled) {
  WidgetData wd;
  wd.set_ok_enabled("button_box", false);
  auto j = parse(wd);
  EXPECT_EQ(j["button_box"]["ok_enabled"], false);
}

// --- TabWidget ---

TEST(WidgetDataTest, SetTabIndex) {
  WidgetData wd;
  wd.set_tab_index("tab_widget", 1);
  auto j = parse(wd);
  EXPECT_EQ(j["tab_widget"]["tab_index"], 1);
}

// --- Generic ---

TEST(WidgetDataTest, SetEnabled) {
  WidgetData wd;
  wd.set_enabled("connect_btn", false);
  auto j = parse(wd);
  EXPECT_EQ(j["connect_btn"]["enabled"], false);
}

TEST(WidgetDataTest, SetVisible) {
  WidgetData wd;
  wd.set_visible("cert_btn", true);
  auto j = parse(wd);
  EXPECT_EQ(j["cert_btn"]["visible"], true);
}

// --- Multiple properties on same widget ---

TEST(WidgetDataTest, MergePropertiesOnSameWidget) {
  WidgetData wd;
  wd.set_text("host_input", "localhost");
  wd.set_placeholder("host_input", "e.g. 127.0.0.1");
  wd.set_read_only("host_input", false);
  auto j = parse(wd);
  EXPECT_EQ(j["host_input"]["text"], "localhost");
  EXPECT_EQ(j["host_input"]["placeholder"], "e.g. 127.0.0.1");
  EXPECT_EQ(j["host_input"]["read_only"], false);
}

// --- Clear ---

TEST(WidgetDataTest, Clear) {
  WidgetData wd;
  wd.set_text("host_input", "localhost");
  EXPECT_NE(parse(wd).size(), 0u);
  wd.clear();
  EXPECT_EQ(parse(wd).size(), 0u);
}

// --- Chaining ---

TEST(WidgetDataTest, Chaining) {
  WidgetData wd;
  auto json_str = wd.set_text("host", "x").set_value("port", 80).set_checked("tls", true).to_json();
  auto j = json::parse(json_str);
  EXPECT_EQ(j["host"]["text"], "x");
  EXPECT_EQ(j["port"]["value"], 80);
  EXPECT_EQ(j["tls"]["checked"], true);
}
