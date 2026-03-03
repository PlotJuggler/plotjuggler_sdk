#include <PJ/host/dialog_handle.hpp>
#include <PJ/host/widget_data_view.hpp>
#include <PJ/host/widget_event_builder.hpp>
#include <PJ/host_qt/dialog_engine.hpp>
#include <PJ/host_qt/widget_binding.hpp>

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <nlohmann/json.hpp>

// Defined in mock_streamer.cpp, linked statically
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable();

// ==========================================================================
// Widget Binding Tests — programmatic widgets, no QUiLoader needed
// ==========================================================================

class WidgetBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = new QWidget();
    auto* layout = new QVBoxLayout(root_);

    line_edit_ = new QLineEdit(root_);
    line_edit_->setObjectName("host_input");
    layout->addWidget(line_edit_);

    spin_box_ = new QSpinBox(root_);
    spin_box_->setObjectName("port_input");
    spin_box_->setRange(0, 99999);
    layout->addWidget(spin_box_);

    combo_box_ = new QComboBox(root_);
    combo_box_->setObjectName("protocol_combo");
    layout->addWidget(combo_box_);

    check_box_ = new QCheckBox(root_);
    check_box_->setObjectName("use_tls_check");
    layout->addWidget(check_box_);

    label_ = new QLabel(root_);
    label_->setObjectName("status_label");
    layout->addWidget(label_);

    button_ = new QPushButton(root_);
    button_->setObjectName("connect_btn");
    layout->addWidget(button_);

    list_widget_ = new QListWidget(root_);
    list_widget_->setObjectName("topic_list");
    layout->addWidget(list_widget_);

    button_box_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, root_);
    button_box_->setObjectName("button_box");
    layout->addWidget(button_box_);
  }

  void TearDown() override { delete root_; }

  QWidget* root_ = nullptr;
  QLineEdit* line_edit_ = nullptr;
  QSpinBox* spin_box_ = nullptr;
  QComboBox* combo_box_ = nullptr;
  QCheckBox* check_box_ = nullptr;
  QLabel* label_ = nullptr;
  QPushButton* button_ = nullptr;
  QListWidget* list_widget_ = nullptr;
  QDialogButtonBox* button_box_ = nullptr;
};

// --- apply_widget_data ---

TEST_F(WidgetBindingTest, ApplyText) {
  nlohmann::json data;
  data["host_input"]["text"] = "10.0.0.1";
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(line_edit_->text().toStdString(), "10.0.0.1");
}

TEST_F(WidgetBindingTest, ApplySpinBoxValue) {
  nlohmann::json data;
  data["port_input"]["value"] = 5555;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(spin_box_->value(), 5555);
}

TEST_F(WidgetBindingTest, ApplySpinBoxRange) {
  nlohmann::json data;
  data["port_input"]["min"] = 100;
  data["port_input"]["max"] = 200;
  data["port_input"]["value"] = 150;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(spin_box_->minimum(), 100);
  EXPECT_EQ(spin_box_->maximum(), 200);
  EXPECT_EQ(spin_box_->value(), 150);
}

TEST_F(WidgetBindingTest, ApplyComboBoxItems) {
  nlohmann::json data;
  data["protocol_combo"]["items"] = {"TCP", "UDP", "WebSocket"};
  data["protocol_combo"]["current_index"] = 1;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(combo_box_->count(), 3);
  EXPECT_EQ(combo_box_->currentIndex(), 1);
  EXPECT_EQ(combo_box_->currentText().toStdString(), "UDP");
}

TEST_F(WidgetBindingTest, ApplyCheckBox) {
  nlohmann::json data;
  data["use_tls_check"]["checked"] = true;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_TRUE(check_box_->isChecked());
}

TEST_F(WidgetBindingTest, ApplyLabel) {
  nlohmann::json data;
  data["status_label"]["label"] = "Connected";
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(label_->text().toStdString(), "Connected");
}

TEST_F(WidgetBindingTest, ApplyButtonText) {
  nlohmann::json data;
  data["connect_btn"]["button_text"] = "Disconnect";
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(button_->text().toStdString(), "Disconnect");
}

TEST_F(WidgetBindingTest, ApplyListItems) {
  nlohmann::json data;
  data["topic_list"]["list_items"] = {"/imu", "/gps", "/motor"};
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_EQ(list_widget_->count(), 3);
  EXPECT_EQ(list_widget_->item(0)->text().toStdString(), "/imu");
}

TEST_F(WidgetBindingTest, ApplyListSelectedItems) {
  // Multi-selection must be enabled for multiple items to be selected
  list_widget_->setSelectionMode(QAbstractItemView::MultiSelection);

  // First populate
  nlohmann::json data1;
  data1["topic_list"]["list_items"] = {"/imu", "/gps", "/motor"};
  PJ::host_qt::apply_widget_data(root_, PJ::host::WidgetDataView(data1.dump()));

  // Then select
  nlohmann::json data2;
  data2["topic_list"]["selected_items"] = {"/imu", "/motor"};
  PJ::host_qt::apply_widget_data(root_, PJ::host::WidgetDataView(data2.dump()));

  EXPECT_TRUE(list_widget_->item(0)->isSelected());
  EXPECT_FALSE(list_widget_->item(1)->isSelected());
  EXPECT_TRUE(list_widget_->item(2)->isSelected());
}

TEST_F(WidgetBindingTest, ApplyOkEnabled) {
  nlohmann::json data;
  data["button_box"]["ok_enabled"] = false;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  auto* ok = button_box_->button(QDialogButtonBox::Ok);
  ASSERT_NE(ok, nullptr);
  EXPECT_FALSE(ok->isEnabled());
}

TEST_F(WidgetBindingTest, ApplyVisibility) {
  nlohmann::json data;
  data["topic_list"]["visible"] = false;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_FALSE(list_widget_->isVisible());
}

TEST_F(WidgetBindingTest, ApplyEnabled) {
  nlohmann::json data;
  data["connect_btn"]["enabled"] = false;
  PJ::host::WidgetDataView view(data.dump());

  PJ::host_qt::apply_widget_data(root_, view);
  EXPECT_FALSE(button_->isEnabled());
}

TEST_F(WidgetBindingTest, MissingWidgetIsIgnored) {
  nlohmann::json data;
  data["nonexistent"]["text"] = "whatever";
  PJ::host::WidgetDataView view(data.dump());

  // Should not crash
  PJ::host_qt::apply_widget_data(root_, view);
}

// --- connect_widget_signals ---

TEST_F(WidgetBindingTest, SignalTextChanged) {
  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  line_edit_->setText("new-host");

  EXPECT_EQ(captured_name, "host_input");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["text"], "new-host");
}

TEST_F(WidgetBindingTest, SignalSpinBoxValueChanged) {
  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  spin_box_->setValue(1234);

  EXPECT_EQ(captured_name, "port_input");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["value"], 1234);
}

TEST_F(WidgetBindingTest, SignalComboBoxIndexChanged) {
  combo_box_->addItems({"A", "B", "C"});

  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  combo_box_->setCurrentIndex(2);

  EXPECT_EQ(captured_name, "protocol_combo");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["current_index"], 2);
  EXPECT_EQ(j["current_text"], "C");
}

TEST_F(WidgetBindingTest, SignalCheckBoxToggled) {
  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  check_box_->setChecked(true);

  EXPECT_EQ(captured_name, "use_tls_check");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["checked"], true);
}

TEST_F(WidgetBindingTest, SignalButtonClicked) {
  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  button_->click();

  EXPECT_EQ(captured_name, "connect_btn");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["clicked"], true);
}

TEST_F(WidgetBindingTest, SignalBlockerPreventsReentrant) {
  int call_count = 0;
  PJ::host_qt::connect_widget_signals(root_, [&](const std::string&, const std::string&) {
    call_count++;
  });

  // apply_widget_data should NOT trigger signals (QSignalBlocker)
  nlohmann::json data;
  data["host_input"]["text"] = "blocked-text";
  PJ::host_qt::apply_widget_data(root_, PJ::host::WidgetDataView(data.dump()));

  EXPECT_EQ(call_count, 0);
  EXPECT_EQ(line_edit_->text().toStdString(), "blocked-text");
}

// ==========================================================================
// DialogEngine Tests — headless (no QDialog shown)
// ==========================================================================

class DialogEngineTest : public ::testing::Test {
 protected:
  const PJ_dialog_vtable_t* vt_ = PJ_get_dialog_vtable();
};

TEST_F(DialogEngineTest, RunHeadlessReturnWidgetData) {
  PJ::host::DialogHandle handle(vt_);
  PJ::host_qt::DialogEngine engine(std::move(handle));

  std::string result = engine.run_headless(0);
  auto j = nlohmann::json::parse(result, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.contains("host_input"));
}

TEST_F(DialogEngineTest, RunHeadlessWithTicks) {
  PJ::host::DialogHandle handle(vt_);

  // Connect first
  (void)handle.send_event("connect_btn", PJ::host::WidgetEventBuilder::clicked());
  PJ::host_qt::DialogEngine engine(std::move(handle));

  std::string result = engine.run_headless(5);
  auto j = nlohmann::json::parse(result);
  // After 3 ticks, mock_streamer discovers topics
  EXPECT_TRUE(j.contains("topic_list"));
  EXPECT_TRUE(j["topic_list"].contains("list_items"));
  EXPECT_GT(j["topic_list"]["list_items"].size(), 0u);
}

TEST_F(DialogEngineTest, ConfigCanBeSet) {
  PJ::host_qt::DialogEngineConfig config;
  config.tick_interval_ms = 100;
  config.enable_diff = false;
  config.enable_file_picker = false;

  PJ::host::DialogHandle handle(vt_);
  PJ::host_qt::DialogEngine engine(std::move(handle), config);

  // Should not crash — just validates config is stored
  std::string result = engine.run_headless(1);
  EXPECT_FALSE(result.empty());
}

// ==========================================================================
// Full round-trip: widget binding + mock_streamer through DialogHandle
// ==========================================================================

TEST_F(DialogEngineTest, RoundTripWidgetBinding) {
  PJ::host::DialogHandle handle(vt_);

  // Get initial widget data and apply to programmatic widgets
  PJ::host::WidgetDataView view(handle.widget_data());

  auto* root = new QWidget();
  auto* layout = new QVBoxLayout(root);

  auto* line_edit = new QLineEdit(root);
  line_edit->setObjectName("host_input");
  layout->addWidget(line_edit);

  auto* spin_box = new QSpinBox(root);
  spin_box->setObjectName("port_input");
  spin_box->setRange(0, 99999);
  layout->addWidget(spin_box);

  PJ::host_qt::apply_widget_data(root, view);

  // Verify the mock_streamer's initial state was applied
  EXPECT_EQ(line_edit->text().toStdString(), "localhost");
  EXPECT_EQ(spin_box->value(), 9090);

  // Simulate user editing host via signal
  std::string captured_name;
  std::string captured_json;
  PJ::host_qt::connect_widget_signals(root, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  line_edit->setText("192.168.1.1");
  EXPECT_EQ(captured_name, "host_input");

  // Send event to plugin
  bool refresh = handle.send_event(captured_name, captured_json);
  EXPECT_TRUE(refresh);

  // Re-read and apply updated widget data
  PJ::host::WidgetDataView updated_view(handle.widget_data());
  PJ::host_qt::apply_widget_data(root, updated_view);

  // Verify host was updated in the plugin's state
  EXPECT_EQ(updated_view.text("host_input").value_or(""), "192.168.1.1");

  delete root;
}

// ==========================================================================
// main with QApplication
// ==========================================================================

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
