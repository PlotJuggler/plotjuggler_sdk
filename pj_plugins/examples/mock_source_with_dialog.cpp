#include <pj_base/sdk/data_source_patterns.hpp>
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
#include <vector>

namespace {

constexpr const char* kUiContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>MockStreamerDialog</class>
 <widget class="QWidget" name="MockStreamerDialog">
  <layout class="QVBoxLayout">
   <item>
    <widget class="QTabWidget" name="settings_tabs">
     <widget class="QWidget" name="connection_tab">
      <attribute name="title"><string>Connection</string></attribute>
      <layout class="QVBoxLayout">
       <item>
        <layout class="QHBoxLayout">
         <item>
          <widget class="QLabel" name="host_label">
           <property name="text"><string>Host:</string></property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="host_input"/>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QHBoxLayout">
         <item>
          <widget class="QLabel" name="port_label">
           <property name="text"><string>Port:</string></property>
          </widget>
         </item>
         <item>
          <widget class="QSpinBox" name="port_input">
           <property name="minimum"><number>1</number></property>
           <property name="maximum"><number>65535</number></property>
           <property name="value"><number>9090</number></property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <widget class="QComboBox" name="protocol_combo"/>
       </item>
       <item>
        <widget class="QCheckBox" name="use_tls_check">
         <property name="text"><string>Use TLS</string></property>
        </widget>
       </item>
      </layout>
     </widget>
     <widget class="QWidget" name="advanced_tab">
      <attribute name="title"><string>Advanced</string></attribute>
      <layout class="QVBoxLayout">
       <item>
        <layout class="QHBoxLayout">
         <item>
          <widget class="QLabel" name="timeout_label">
           <property name="text"><string>Timeout (s):</string></property>
          </widget>
         </item>
         <item>
          <widget class="QDoubleSpinBox" name="timeout_input">
           <property name="minimum"><double>0.1</double></property>
           <property name="maximum"><double>60.0</double></property>
           <property name="value"><double>5.0</double></property>
           <property name="singleStep"><double>0.5</double></property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <widget class="QCheckBox" name="reconnect_check">
         <property name="text"><string>Auto-reconnect on failure</string></property>
        </widget>
       </item>
      </layout>
     </widget>
    </widget>
   </item>
   <item>
    <widget class="QPushButton" name="connect_btn">
     <property name="text"><string>Connect</string></property>
    </widget>
   </item>
   <item>
    <widget class="QLabel" name="status_label">
     <property name="text"><string>Disconnected</string></property>
    </widget>
   </item>
   <item>
    <widget class="QListWidget" name="topic_list"/>
   </item>
   <item>
    <widget class="QDialogButtonBox" name="buttonBox"/>
   </item>
  </layout>
 </widget>
</ui>
)";

}  // namespace

/// Dialog class — all the UI logic. Exposes read-only accessors for the source.
class MockStreamerDialog : public PJ::DialogPluginTyped {
  using PJ::DialogPluginTyped::onValueChanged;

 public:
  // --- Read-only accessors for the DataSource ---

  const std::string& host() const {
    return host_;
  }
  int port() const {
    return port_;
  }
  bool useTls() const {
    return use_tls_;
  }
  double timeout() const {
    return timeout_;
  }
  bool reconnect() const {
    return reconnect_;
  }
  const std::vector<std::string>& selectedTopics() const {
    return selected_topics_;
  }

  // --- Dialog protocol implementation ---

  std::string manifest() const override {
    return R"({
      "name": "Mock Streamer",
      "version": "1.0.0",
      "description": "A mock streaming data source with dialog"
    })";
  }

  std::string ui_content() const override {
    return kUiContent;
  }

  std::string widget_data() override {
    PJ::WidgetData wd;

    wd.setText("host_input", host_);
    wd.setPlaceholder("host_input", "e.g. localhost");
    wd.setValue("port_input", port_);
    wd.setRange("port_input", 1, 65535);

    std::vector<std::string> protocols = {"TCP", "UDP", "WebSocket"};
    wd.setItems("protocol_combo", protocols);
    wd.setCurrentIndex("protocol_combo", protocol_index_);

    wd.setChecked("use_tls_check", use_tls_);

    wd.setValue("timeout_input", timeout_);
    wd.setChecked("reconnect_check", reconnect_);

    wd.setButtonText("connect_btn", connected_ ? "Disconnect" : "Connect");
    wd.setEnabled("connect_btn", !host_.empty());

    wd.setLabel("status_label", status_text_);

    if (connected_ && !topics_.empty()) {
      wd.setListItems("topic_list", topics_);
      wd.setVisible("topic_list", true);
    } else {
      wd.setVisible("topic_list", false);
    }

    wd.setOkEnabled(connected_ && !selected_topics_.empty());

    return wd.toJson();
  }

  bool onTextChanged(std::string_view widget_name, std::string_view text) override {
    if (widget_name == "host_input") {
      host_ = std::string(text);
      return true;
    }
    return false;
  }

  bool onValueChanged(std::string_view widget_name, int value) override {
    if (widget_name == "port_input") {
      port_ = value;
      return true;
    }
    return false;
  }

  bool onValueChanged(std::string_view widget_name, double value) override {
    if (widget_name == "timeout_input") {
      timeout_ = value;
      return true;
    }
    return false;
  }

  bool onIndexChanged(std::string_view widget_name, int index) override {
    if (widget_name == "protocol_combo") {
      protocol_index_ = index;
      return true;
    }
    return false;
  }

  bool onToggled(std::string_view widget_name, bool checked) override {
    if (widget_name == "use_tls_check") {
      use_tls_ = checked;
      return true;
    }
    if (widget_name == "reconnect_check") {
      reconnect_ = checked;
      return true;
    }
    return false;
  }

  bool onClicked(std::string_view widget_name) override {
    if (widget_name == "connect_btn") {
      if (connected_) {
        connected_ = false;
        tick_count_ = 0;
        topics_.clear();
        selected_topics_.clear();
        status_text_ = "Disconnected";
      } else {
        if (host_.empty()) {
          return true;
        }
        connected_ = true;
        tick_count_ = 0;
        status_text_ = "Connecting to " + host_ + ":" + std::to_string(port_) + "...";
      }
      return true;
    }
    return false;
  }

  bool onSelectionChanged(std::string_view widget_name, const std::vector<std::string>& selected) override {
    if (widget_name == "topic_list") {
      selected_topics_ = selected;
      return true;
    }
    return false;
  }

  bool onTick() override {
    if (!connected_) {
      return false;
    }
    ++tick_count_;
    if (tick_count_ == 3 && topics_.empty()) {
      topics_ = {"/sensors/imu", "/sensors/gps", "/motors/left", "/motors/right", "/battery/state"};
      status_text_ = "Connected — " + std::to_string(topics_.size()) + " topics available";
      return true;
    }
    return false;
  }

  void onAccepted(std::string_view /*final_state_json*/) override {}
  void onRejected() override {
    connected_ = false;
    topics_.clear();
  }

  std::string saveConfig() const override {
    nlohmann::json cfg;
    cfg["host"] = host_;
    cfg["port"] = port_;
    cfg["protocol_index"] = protocol_index_;
    cfg["use_tls"] = use_tls_;
    cfg["timeout"] = timeout_;
    cfg["reconnect"] = reconnect_;
    return cfg.dump();
  }

  bool loadConfig(std::string_view config_json) override {
    auto cfg = nlohmann::json::parse(config_json, nullptr, false);
    if (cfg.is_discarded()) {
      return false;
    }
    if (auto it = cfg.find("host"); it != cfg.end() && it->is_string()) {
      host_ = it->get<std::string>();
    }
    if (auto it = cfg.find("port"); it != cfg.end() && it->is_number_integer()) {
      port_ = it->get<int>();
    }
    if (auto it = cfg.find("protocol_index"); it != cfg.end() && it->is_number_integer()) {
      protocol_index_ = it->get<int>();
    }
    if (auto it = cfg.find("use_tls"); it != cfg.end() && it->is_boolean()) {
      use_tls_ = it->get<bool>();
    }
    if (auto it = cfg.find("timeout"); it != cfg.end() && it->is_number()) {
      timeout_ = it->get<double>();
    }
    if (auto it = cfg.find("reconnect"); it != cfg.end() && it->is_boolean()) {
      reconnect_ = it->get<bool>();
    }
    return true;
  }

 private:
  std::string host_ = "localhost";
  int port_ = 9090;
  int protocol_index_ = 0;
  bool use_tls_ = false;
  double timeout_ = 5.0;
  bool reconnect_ = false;
  bool connected_ = false;
  int tick_count_ = 0;
  std::string status_text_ = "Disconnected";
  std::vector<std::string> topics_;
  std::vector<std::string> selected_topics_;
};

/// DataSource class — business logic, owns the dialog as a member.
class MockStreamerSource : public PJ::StreamSourceBase {
 public:
  PJ_borrowed_dialog_t getDialog() override {
    return PJ::borrowDialog(dialog_);
  }

  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest | PJ::kCapabilityHasDialog;
  }

  PJ::Status onStart() override {
    auto topic = writeHost().ensureTopic("mock/" + dialog_.host());
    if (!topic) {
      return PJ::unexpected(topic.error());
    }
    topic_ = *topic;

    runtimeHost().reportMessage(
        PJ::DataSourceMessageLevel::kInfo, "streaming from " + dialog_.host() + ":" + std::to_string(dialog_.port()));
    return PJ::okStatus();
  }

  PJ::Status onPoll() override {
    ++poll_count_;
    return PJ::okStatus();
  }

  void onStop() override {
    poll_count_ = 0;
  }

  std::string saveConfig() const override {
    return dialog_.saveConfig();
  }

  PJ::Status loadConfig(std::string_view json) override {
    return dialog_.loadConfig(json) ? PJ::okStatus() : PJ::unexpected(std::string("bad config"));
  }

 private:
  MockStreamerDialog dialog_;
  PJ::sdk::TopicHandle topic_{};
  int poll_count_ = 0;
};

PJ_DATA_SOURCE_PLUGIN(
    MockStreamerSource, R"({"id":"mock-streamer-source","name":"Mock Streamer Source","version":"1.0.0",)"
                        R"("description":"Combined DataSource+Dialog mock for integration testing"})")

PJ_DIALOG_PLUGIN(MockStreamerDialog)
