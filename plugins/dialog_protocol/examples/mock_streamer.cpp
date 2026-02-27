#include <pj/sdk/dialog_plugin_typed.hpp>
#include <pj/sdk/widget_data.hpp>

#include <sstream>
#include <string>

namespace {

// Minimal inline .ui XML for the mock streamer dialog
const char* kUiContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>MockStreamerDialog</class>
 <widget class="QWidget" name="MockStreamerDialog">
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
   <item>
    <widget class="QPushButton" name="cert_picker_btn">
     <property name="text"><string>Select Certificate...</string></property>
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
    <widget class="QDialogButtonBox" name="button_box"/>
   </item>
  </layout>
 </widget>
</ui>
)";

}  // namespace

class MockStreamer : public pj::sdk::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return R"({
      "name": "Mock Streamer",
      "version": "1.0.0",
      "description": "A mock streaming data source for testing the dialog protocol",
      "author": "PlotJuggler"
    })";
  }

  std::string ui_content() const override { return kUiContent; }

  std::string widget_data() override {
    pj::sdk::WidgetData wd;

    wd.set_text("host_input", host_);
    wd.set_placeholder("host_input", "e.g. localhost");
    wd.set_value("port_input", port_);
    wd.set_range("port_input", 1, 65535);

    std::vector<std::string> protocols = {"TCP", "UDP", "WebSocket"};
    wd.set_items("protocol_combo", protocols);
    wd.set_current_index("protocol_combo", protocol_index_);

    wd.set_checked("use_tls_check", use_tls_);

    if (use_tls_) {
      wd.set_file_picker("cert_picker_btn", cert_path_.empty() ? "Select Certificate..." : cert_path_,
                         "*.pem *.crt *.cer", "Select TLS Certificate");
      wd.set_visible("cert_picker_btn", true);
    } else {
      wd.set_visible("cert_picker_btn", false);
    }

    wd.set_button_text("connect_btn", connected_ ? "Disconnect" : "Connect");
    wd.set_enabled("connect_btn", !host_.empty());

    wd.set_label("status_label", status_text_);

    if (connected_ && !topics_.empty()) {
      wd.set_list_items("topic_list", topics_);
      wd.set_visible("topic_list", true);
    } else {
      wd.set_visible("topic_list", false);
    }

    wd.set_ok_enabled("button_box", connected_ && !selected_topics_.empty());

    return wd.to_json();
  }

  bool on_text_changed(std::string_view widget_name, std::string_view text) override {
    if (widget_name == "host_input") {
      host_ = std::string(text);
      return true;
    }
    return false;
  }

  bool on_value_changed(std::string_view widget_name, int value) override {
    if (widget_name == "port_input") {
      port_ = value;
      return true;
    }
    return false;
  }

  bool on_index_changed(std::string_view widget_name, int index) override {
    if (widget_name == "protocol_combo") {
      protocol_index_ = index;
      return true;
    }
    return false;
  }

  bool on_toggled(std::string_view widget_name, bool checked) override {
    if (widget_name == "use_tls_check") {
      use_tls_ = checked;
      return true;
    }
    return false;
  }

  bool on_clicked(std::string_view widget_name) override {
    if (widget_name == "connect_btn") {
      if (connected_) {
        connected_ = false;
        tick_count_ = 0;
        topics_.clear();
        selected_topics_.clear();
        status_text_ = "Disconnected";
      } else {
        if (host_.empty()) {
          error_ = "Host cannot be empty";
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

  bool on_file_selected(std::string_view widget_name, std::string_view path) override {
    if (widget_name == "cert_picker_btn") {
      cert_path_ = std::string(path);
      return true;
    }
    return false;
  }

  bool on_selection_changed(std::string_view widget_name,
                            const std::vector<std::string>& selected) override {
    if (widget_name == "topic_list") {
      selected_topics_ = selected;
      return true;
    }
    return false;
  }

  bool on_tick() override {
    if (!connected_) return false;

    ++tick_count_;
    if (tick_count_ == 3 && topics_.empty()) {
      // Simulate topic discovery after 3 ticks
      topics_ = {"/sensors/imu", "/sensors/gps", "/motors/left", "/motors/right", "/battery/state"};
      status_text_ = "Connected — " + std::to_string(topics_.size()) + " topics available";
      return true;
    }
    return false;
  }

  void on_accepted(std::string_view /*final_state_json*/) override {
    // In a real plugin, start streaming the selected topics
  }

  void on_rejected() override {
    connected_ = false;
    topics_.clear();
  }

  std::string save_config() const override {
    nlohmann::json cfg;
    cfg["host"] = host_;
    cfg["port"] = port_;
    cfg["protocol_index"] = protocol_index_;
    cfg["use_tls"] = use_tls_;
    cfg["cert_path"] = cert_path_;
    return cfg.dump();
  }

  bool load_config(std::string_view config_json) override {
    auto cfg = nlohmann::json::parse(config_json, nullptr, false);
    if (cfg.is_discarded()) return false;

    if (auto it = cfg.find("host"); it != cfg.end() && it->is_string())
      host_ = it->get<std::string>();
    if (auto it = cfg.find("port"); it != cfg.end() && it->is_number_integer())
      port_ = it->get<int>();
    if (auto it = cfg.find("protocol_index"); it != cfg.end() && it->is_number_integer())
      protocol_index_ = it->get<int>();
    if (auto it = cfg.find("use_tls"); it != cfg.end() && it->is_boolean())
      use_tls_ = it->get<bool>();
    if (auto it = cfg.find("cert_path"); it != cfg.end() && it->is_string())
      cert_path_ = it->get<std::string>();
    return true;
  }

  std::string last_error() const override {
    std::string err = std::move(error_);
    error_.clear();
    return err;
  }

 private:
  std::string host_ = "localhost";
  int port_ = 9090;
  int protocol_index_ = 0;
  bool use_tls_ = false;
  std::string cert_path_;
  bool connected_ = false;
  int tick_count_ = 0;
  std::string status_text_ = "Disconnected";
  std::vector<std::string> topics_;
  std::vector<std::string> selected_topics_;
  mutable std::string error_;
};

PJ_DIALOG_PLUGIN(MockStreamer)
