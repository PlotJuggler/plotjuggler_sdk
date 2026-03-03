#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::sdk {

/// Builder for the JSON string returned by get_widget_data().
/// Each method targets an existing widget in the .ui file by its objectName.
class WidgetData {
 public:
  // --- QLineEdit ---
  WidgetData& set_text(std::string_view name, std::string_view text) {
    entry(name)["text"] = text;
    return *this;
  }

  WidgetData& set_placeholder(std::string_view name, std::string_view text) {
    entry(name)["placeholder"] = text;
    return *this;
  }

  WidgetData& set_read_only(std::string_view name, bool read_only) {
    entry(name)["read_only"] = read_only;
    return *this;
  }

  // --- QComboBox ---
  WidgetData& set_current_index(std::string_view name, int index) {
    entry(name)["current_index"] = index;
    return *this;
  }

  WidgetData& set_items(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["items"] = items;
    return *this;
  }

  // --- QCheckBox, QRadioButton ---
  WidgetData& set_checked(std::string_view name, bool checked) {
    entry(name)["checked"] = checked;
    return *this;
  }

  // --- QSpinBox ---
  WidgetData& set_value(std::string_view name, int value) {
    entry(name)["value"] = value;
    return *this;
  }

  // --- QDoubleSpinBox ---
  WidgetData& set_value(std::string_view name, double value) {
    entry(name)["value"] = value;
    return *this;
  }

  WidgetData& set_range(std::string_view name, int min, int max) {
    auto& e = entry(name);
    e["min"] = min;
    e["max"] = max;
    return *this;
  }

  // --- QListWidget ---
  WidgetData& set_list_items(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["list_items"] = items;
    return *this;
  }

  WidgetData& set_selected_items(std::string_view name, const std::vector<std::string>& selected) {
    entry(name)["selected_items"] = selected;
    return *this;
  }

  // --- QTableWidget ---
  WidgetData& set_table_headers(std::string_view name, const std::vector<std::string>& headers) {
    entry(name)["headers"] = headers;
    return *this;
  }

  WidgetData& set_table_rows(std::string_view name,
                             const std::vector<std::vector<std::string>>& rows) {
    entry(name)["rows"] = rows;
    return *this;
  }

  // --- QLabel ---
  WidgetData& set_label(std::string_view name, std::string_view text) {
    entry(name)["label"] = text;
    return *this;
  }

  // --- QPushButton ---
  WidgetData& set_button_text(std::string_view name, std::string_view text) {
    entry(name)["button_text"] = text;
    return *this;
  }

  WidgetData& set_file_picker(std::string_view name, std::string_view button_text,
                              std::string_view filter, std::string_view title) {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "file_picker";
    e["filter"] = filter;
    e["title"] = title;
    return *this;
  }

  // --- QDialogButtonBox ---
  WidgetData& set_ok_enabled(std::string_view name, bool enabled) {
    entry(name)["ok_enabled"] = enabled;
    return *this;
  }

  // --- QTabWidget ---
  WidgetData& set_tab_index(std::string_view name, int index) {
    entry(name)["tab_index"] = index;
    return *this;
  }

  // --- Generic (any widget) ---
  WidgetData& set_enabled(std::string_view name, bool enabled) {
    entry(name)["enabled"] = enabled;
    return *this;
  }

  WidgetData& set_visible(std::string_view name, bool visible) {
    entry(name)["visible"] = visible;
    return *this;
  }

  /// Serialize to JSON string
  std::string to_json() const { return data_.dump(); }

  /// Reset for reuse
  void clear() { data_ = nlohmann::json::object(); }

 private:
  nlohmann::json data_ = nlohmann::json::object();

  nlohmann::json& entry(std::string_view name) {
    std::string key(name);
    if (!data_.contains(key)) {
      data_[key] = nlohmann::json::object();
    }
    return data_[key];
  }
};

}  // namespace PJ::sdk
