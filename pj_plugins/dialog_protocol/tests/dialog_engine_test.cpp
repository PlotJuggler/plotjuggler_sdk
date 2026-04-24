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
#include <pj_plugins/host/dialog_handle.hpp>
#include <pj_plugins/host/widget_data_view.hpp>
#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/host_qt/dialog_engine.hpp>
#include <pj_plugins/host_qt/widget_binding.hpp>

// Defined in mock_dialog.cpp, linked statically
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept;

// ==========================================================================
// Widget Binding Tests — programmatic widgets, no QUiLoader needed
// ==========================================================================

class WidgetBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = new QWidget();
    auto* layout = new QVBoxLayout(root_);

    line_edit_ = new QLineEdit(root_);
    line_edit_->setObjectName("name_input");
    layout->addWidget(line_edit_);

    spin_box_ = new QSpinBox(root_);
    spin_box_->setObjectName("count_input");
    spin_box_->setRange(0, 99999);
    layout->addWidget(spin_box_);

    check_box_ = new QCheckBox(root_);
    check_box_->setObjectName("verbose_check");
    layout->addWidget(check_box_);

    button_box_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, root_);
    button_box_->setObjectName("buttonBox");
    layout->addWidget(button_box_);
  }

  void TearDown() override {
    delete root_;
  }

  QWidget* root_ = nullptr;
  QLineEdit* line_edit_ = nullptr;
  QSpinBox* spin_box_ = nullptr;
  QCheckBox* check_box_ = nullptr;
  QDialogButtonBox* button_box_ = nullptr;
};

// --- apply_widget_data ---

TEST_F(WidgetBindingTest, ApplyText) {
  nlohmann::json data;
  data["name_input"]["text"] = "my_source";
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_EQ(line_edit_->text().toStdString(), "my_source");
}

TEST_F(WidgetBindingTest, ApplySpinBoxValue) {
  nlohmann::json data;
  data["count_input"]["value"] = 42;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_EQ(spin_box_->value(), 42);
}

TEST_F(WidgetBindingTest, ApplySpinBoxRange) {
  nlohmann::json data;
  data["count_input"]["min"] = 0;
  data["count_input"]["max"] = 1000;
  data["count_input"]["value"] = 500;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_EQ(spin_box_->minimum(), 0);
  EXPECT_EQ(spin_box_->maximum(), 1000);
  EXPECT_EQ(spin_box_->value(), 500);
}

TEST_F(WidgetBindingTest, ApplyCheckBox) {
  nlohmann::json data;
  data["verbose_check"]["checked"] = true;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_TRUE(check_box_->isChecked());
}

TEST_F(WidgetBindingTest, ApplyOkEnabled) {
  nlohmann::json data;
  data["buttonBox"]["ok_enabled"] = false;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  auto* ok = button_box_->button(QDialogButtonBox::Ok);
  ASSERT_NE(ok, nullptr);
  EXPECT_FALSE(ok->isEnabled());
}

TEST_F(WidgetBindingTest, ApplyVisibility) {
  nlohmann::json data;
  data["count_input"]["visible"] = false;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_FALSE(spin_box_->isVisible());
}

TEST_F(WidgetBindingTest, ApplyEnabled) {
  nlohmann::json data;
  data["name_input"]["enabled"] = false;
  PJ::WidgetDataView view(data.dump());

  PJ::applyWidgetData(root_, view);
  EXPECT_FALSE(line_edit_->isEnabled());
}

TEST_F(WidgetBindingTest, MissingWidgetIsIgnored) {
  nlohmann::json data;
  data["nonexistent"]["text"] = "whatever";
  PJ::WidgetDataView view(data.dump());

  // Should not crash
  PJ::applyWidgetData(root_, view);
}

// --- connect_widget_signals ---

TEST_F(WidgetBindingTest, SignalTextChanged) {
  std::string captured_name;
  std::string captured_json;
  PJ::connectWidgetSignals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  line_edit_->setText("new_name");

  EXPECT_EQ(captured_name, "name_input");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["text"], "new_name");
}

TEST_F(WidgetBindingTest, SignalSpinBoxValueChanged) {
  std::string captured_name;
  std::string captured_json;
  PJ::connectWidgetSignals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  spin_box_->setValue(99);

  EXPECT_EQ(captured_name, "count_input");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["value"], 99);
}

TEST_F(WidgetBindingTest, SignalCheckBoxToggled) {
  std::string captured_name;
  std::string captured_json;
  PJ::connectWidgetSignals(root_, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  check_box_->setChecked(true);

  EXPECT_EQ(captured_name, "verbose_check");
  auto j = nlohmann::json::parse(captured_json);
  EXPECT_EQ(j["checked"], true);
}

TEST_F(WidgetBindingTest, SignalBlockerPreventsReentrant) {
  int call_count = 0;
  PJ::connectWidgetSignals(root_, [&](const std::string&, const std::string&) { call_count++; });

  // apply_widget_data should NOT trigger signals (QSignalBlocker)
  nlohmann::json data;
  data["name_input"]["text"] = "blocked-text";
  PJ::applyWidgetData(root_, PJ::WidgetDataView(data.dump()));

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
  PJ::DialogHandle handle(vt_);
  PJ::DialogEngine engine(std::move(handle));

  std::string result = engine.runHeadless(0);
  auto j = nlohmann::json::parse(result, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.contains("name_input"));
}

TEST_F(DialogEngineTest, RunHeadlessWithTicks) {
  PJ::DialogHandle handle(vt_);
  PJ::DialogEngine engine(std::move(handle));

  // mock_dialog has no tick behavior, but should still return valid widget data
  std::string result = engine.runHeadless(5);
  auto j = nlohmann::json::parse(result);
  EXPECT_TRUE(j.contains("name_input"));
  EXPECT_TRUE(j.contains("count_input"));
}

TEST_F(DialogEngineTest, ConfigCanBeSet) {
  PJ::DialogEngineConfig config;
  config.tick_interval_ms = 100;
  config.enable_diff = false;
  config.enable_file_picker = false;

  PJ::DialogHandle handle(vt_);
  PJ::DialogEngine engine(std::move(handle), config);

  // Should not crash — just validates config is stored
  std::string result = engine.runHeadless(1);
  EXPECT_FALSE(result.empty());
}

TEST_F(DialogEngineTest, SavedConfigReturnsPluginConfig) {
  PJ::DialogHandle handle(vt_);
  (void)handle.sendEvent("name_input", R"({"text": "config_test"})");
  PJ::DialogEngine engine(std::move(handle));

  std::string cfg = engine.savedConfig();
  auto j = nlohmann::json::parse(cfg);
  EXPECT_EQ(j["name"], "config_test");
}

// ==========================================================================
// Full round-trip: widget binding + mock_dialog through DialogHandle
// ==========================================================================

TEST_F(DialogEngineTest, RoundTripWidgetBinding) {
  PJ::DialogHandle handle(vt_);

  // Get initial widget data and apply to programmatic widgets
  PJ::WidgetDataView view(handle.widget_data());

  auto* root = new QWidget();
  auto* layout = new QVBoxLayout(root);

  auto* line_edit = new QLineEdit(root);
  line_edit->setObjectName("name_input");
  layout->addWidget(line_edit);

  auto* spin_box = new QSpinBox(root);
  spin_box->setObjectName("count_input");
  spin_box->setRange(0, 99999);
  layout->addWidget(spin_box);

  PJ::applyWidgetData(root, view);

  // Verify the mock_dialog's initial state was applied
  EXPECT_EQ(line_edit->text().toStdString(), "default");
  EXPECT_EQ(spin_box->value(), 10);

  // Simulate user editing name via signal
  std::string captured_name;
  std::string captured_json;
  PJ::connectWidgetSignals(root, [&](const std::string& name, const std::string& json) {
    captured_name = name;
    captured_json = json;
  });

  line_edit->setText("my_source");
  EXPECT_EQ(captured_name, "name_input");

  // Send event to plugin
  bool refresh = handle.sendEvent(captured_name, captured_json);
  EXPECT_TRUE(refresh);

  // Re-read and apply updated widget data
  PJ::WidgetDataView updated_view(handle.widget_data());
  PJ::applyWidgetData(root, updated_view);

  // Verify name was updated in the plugin's state
  EXPECT_EQ(updated_view.text("name_input").value_or(""), "my_source");

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
