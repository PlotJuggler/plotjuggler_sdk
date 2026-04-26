#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>

namespace {

constexpr const char* kUiContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>MockDialog</class>
 <widget class="QWidget" name="MockDialog">
  <layout class="QVBoxLayout">
   <item>
    <layout class="QHBoxLayout">
     <item>
      <widget class="QLabel" name="name_label">
       <property name="text"><string>Name:</string></property>
      </widget>
     </item>
     <item>
      <widget class="QLineEdit" name="name_input"/>
     </item>
    </layout>
   </item>
   <item>
    <layout class="QHBoxLayout">
     <item>
      <widget class="QLabel" name="count_label">
       <property name="text"><string>Count:</string></property>
      </widget>
     </item>
     <item>
      <widget class="QSpinBox" name="count_input">
       <property name="minimum"><number>0</number></property>
       <property name="maximum"><number>1000</number></property>
       <property name="value"><number>10</number></property>
      </widget>
     </item>
    </layout>
   </item>
   <item>
    <widget class="QCheckBox" name="verbose_check">
     <property name="text"><string>Verbose output</string></property>
    </widget>
   </item>
   <item>
    <widget class="QDialogButtonBox" name="buttonBox"/>
   </item>
  </layout>
 </widget>
</ui>
)";

}  // namespace

class MockDialog : public PJ::DialogPluginTyped {
  using PJ::DialogPluginTyped::onValueChanged;

 public:
  std::string manifest() const override {
    return R"({
      "id": "mock-dialog",
      "name": "Mock Dialog",
      "version": "1.0.0",
      "description": "A minimal dialog plugin for testing"
    })";
  }

  std::string ui_content() const override {
    return kUiContent;
  }

  std::string widget_data() override {
    PJ::WidgetData wd;
    wd.setText("name_input", name_);
    wd.setPlaceholder("name_input", "e.g. my_source");
    wd.setValue("count_input", count_);
    wd.setRange("count_input", 0, 1000);
    wd.setChecked("verbose_check", verbose_);
    wd.setOkEnabled(!name_.empty());
    return wd.toJson();
  }

  bool onTextChanged(std::string_view widget_name, std::string_view text) override {
    if (widget_name == "name_input") {
      name_ = std::string(text);
      return true;
    }
    return false;
  }

  bool onValueChanged(std::string_view widget_name, int value) override {
    if (widget_name == "count_input") {
      count_ = value;
      return true;
    }
    return false;
  }

  bool onToggled(std::string_view widget_name, bool checked) override {
    if (widget_name == "verbose_check") {
      verbose_ = checked;
      return true;
    }
    return false;
  }

  std::string saveConfig() const override {
    nlohmann::json cfg;
    cfg["name"] = name_;
    cfg["count"] = count_;
    cfg["verbose"] = verbose_;
    return cfg.dump();
  }

  bool loadConfig(std::string_view config_json) override {
    auto cfg = nlohmann::json::parse(config_json, nullptr, false);
    if (cfg.is_discarded()) {
      return false;
    }
    if (auto it = cfg.find("name"); it != cfg.end() && it->is_string()) {
      name_ = it->get<std::string>();
    }
    if (auto it = cfg.find("count"); it != cfg.end() && it->is_number_integer()) {
      count_ = it->get<int>();
    }
    if (auto it = cfg.find("verbose"); it != cfg.end() && it->is_boolean()) {
      verbose_ = it->get<bool>();
    }
    return true;
  }

 private:
  std::string name_ = "default";
  int count_ = 10;
  bool verbose_ = false;
};

PJ_DIALOG_PLUGIN(MockDialog)
