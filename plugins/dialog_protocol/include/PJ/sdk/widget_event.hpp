#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::sdk {

/// Read-only parser for the event_json string passed to on_widget_event().
/// Returns std::nullopt for missing fields instead of throwing.
class WidgetEvent {
 public:
  explicit WidgetEvent(std::string_view event_json)
      : data_(nlohmann::json::parse(event_json, nullptr, false)) {
    if (data_.is_discarded()) {
      data_ = nlohmann::json::object();
    }
  }

  /// QLineEdit: text changed
  std::optional<std::string> text() const { return get_string("text"); }

  /// QComboBox: current index changed
  std::optional<int> current_index() const { return get_int("current_index"); }

  /// QComboBox: current text
  std::optional<std::string> current_text() const { return get_string("current_text"); }

  /// QCheckBox, QRadioButton: toggled
  std::optional<bool> checked() const { return get_bool("checked"); }

  /// QSpinBox: value changed
  std::optional<int> value_int() const { return get_int("value"); }

  /// QDoubleSpinBox: value changed
  std::optional<double> value_double() const {
    auto it = data_.find("value");
    if (it == data_.end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
  }

  /// QListWidget: selection changed
  std::optional<std::vector<std::string>> selected_items() const {
    auto it = data_.find("selected_items");
    if (it == data_.end() || !it->is_array()) return std::nullopt;
    std::vector<std::string> result;
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
    return result;
  }

  /// QPushButton: clicked
  bool clicked() const {
    auto it = data_.find("clicked");
    return it != data_.end() && it->is_boolean() && it->get<bool>();
  }

  /// File picker: file selected
  std::optional<std::string> file_selected() const { return get_string("file_selected"); }

  /// QTabWidget: tab changed
  std::optional<int> tab_index() const { return get_int("tab_index"); }

  /// Check if a key exists in the event data
  bool has(std::string_view key) const { return data_.contains(std::string(key)); }

  /// Raw access for custom events
  const nlohmann::json& raw() const { return data_; }

 private:
  nlohmann::json data_;

  std::optional<std::string> get_string(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
  }

  std::optional<int> get_int(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int>();
  }

  std::optional<bool> get_bool(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_boolean()) return std::nullopt;
    return it->get<bool>();
  }
};

}  // namespace PJ::sdk
