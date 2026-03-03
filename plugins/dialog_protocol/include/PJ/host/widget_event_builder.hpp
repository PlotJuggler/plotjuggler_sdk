#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::host {

/// Static factory methods that produce event JSON strings for send_event().
/// Each method returns a complete JSON string matching what PJ::sdk::WidgetEvent expects.
struct WidgetEventBuilder {
  /// QLineEdit: text changed
  [[nodiscard]] static std::string text_changed(std::string_view text) {
    nlohmann::json j;
    j["text"] = text;
    return j.dump();
  }

  /// QComboBox: index changed (index only)
  [[nodiscard]] static std::string index_changed(int index) {
    nlohmann::json j;
    j["current_index"] = index;
    return j.dump();
  }

  /// QComboBox: index changed (index + current text)
  [[nodiscard]] static std::string index_changed(int index, std::string_view current_text) {
    nlohmann::json j;
    j["current_index"] = index;
    j["current_text"] = current_text;
    return j.dump();
  }

  /// QCheckBox, QRadioButton: toggled
  [[nodiscard]] static std::string toggled(bool checked) {
    nlohmann::json j;
    j["checked"] = checked;
    return j.dump();
  }

  /// QSpinBox: value changed (int)
  [[nodiscard]] static std::string value_changed(int value) {
    nlohmann::json j;
    j["value"] = value;
    return j.dump();
  }

  /// QDoubleSpinBox: value changed (double)
  [[nodiscard]] static std::string value_changed(double value) {
    nlohmann::json j;
    j["value"] = value;
    return j.dump();
  }

  /// QListWidget: selection changed
  [[nodiscard]] static std::string selection_changed(const std::vector<std::string>& selected) {
    nlohmann::json j;
    j["selected_items"] = selected;
    return j.dump();
  }

  /// QPushButton: clicked
  [[nodiscard]] static std::string clicked() {
    nlohmann::json j;
    j["clicked"] = true;
    return j.dump();
  }

  /// File picker: file selected
  [[nodiscard]] static std::string file_selected(std::string_view path) {
    nlohmann::json j;
    j["file_selected"] = path;
    return j.dump();
  }

  /// QTabWidget: tab changed
  [[nodiscard]] static std::string tab_changed(int index) {
    nlohmann::json j;
    j["tab_index"] = index;
    return j.dump();
  }
};

}  // namespace PJ::host
