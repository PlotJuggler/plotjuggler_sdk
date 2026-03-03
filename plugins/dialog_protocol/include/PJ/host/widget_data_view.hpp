#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::host {

/// Read-only view of the JSON returned by get_widget_data().
/// Each getter takes a widget name and reads the corresponding property.
/// Returns std::nullopt for missing widgets or missing/wrong-typed fields.
class WidgetDataView {
 public:
  explicit WidgetDataView(std::string_view json)
      : data_(nlohmann::json::parse(json, nullptr, false)) {
    if (data_.is_discarded()) {
      data_ = nlohmann::json::object();
    }
  }

  // --- QLineEdit ---
  [[nodiscard]] std::optional<std::string> text(std::string_view name) const {
    return get_string(name, "text");
  }
  [[nodiscard]] std::optional<std::string> placeholder(std::string_view name) const {
    return get_string(name, "placeholder");
  }
  [[nodiscard]] std::optional<bool> read_only(std::string_view name) const {
    return get_bool(name, "read_only");
  }

  // --- QComboBox ---
  [[nodiscard]] std::optional<int> current_index(std::string_view name) const {
    return get_int(name, "current_index");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> items(std::string_view name) const {
    return get_string_array(name, "items");
  }

  // --- QCheckBox, QRadioButton ---
  [[nodiscard]] std::optional<bool> checked(std::string_view name) const {
    return get_bool(name, "checked");
  }

  // --- QSpinBox ---
  [[nodiscard]] std::optional<int> value_int(std::string_view name) const {
    return get_int(name, "value");
  }

  // --- QDoubleSpinBox ---
  [[nodiscard]] std::optional<double> value_double(std::string_view name) const {
    return get_double(name, "value");
  }

  [[nodiscard]] std::optional<int> range_min(std::string_view name) const {
    return get_int(name, "min");
  }
  [[nodiscard]] std::optional<int> range_max(std::string_view name) const {
    return get_int(name, "max");
  }

  // --- QListWidget ---
  [[nodiscard]] std::optional<std::vector<std::string>> list_items(std::string_view name) const {
    return get_string_array(name, "list_items");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> selected_items(
      std::string_view name) const {
    return get_string_array(name, "selected_items");
  }

  // --- QTableWidget ---
  [[nodiscard]] std::optional<std::vector<std::string>> table_headers(
      std::string_view name) const {
    return get_string_array(name, "headers");
  }

  [[nodiscard]] std::optional<std::vector<std::vector<std::string>>> table_rows(
      std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find("rows");
    if (it == w->end() || !it->is_array()) return std::nullopt;
    std::vector<std::vector<std::string>> result;
    result.reserve(it->size());
    for (const auto& row : *it) {
      if (!row.is_array()) return std::nullopt;
      std::vector<std::string> cells;
      cells.reserve(row.size());
      for (const auto& cell : row) {
        if (cell.is_string()) {
          cells.push_back(cell.get<std::string>());
        }
      }
      result.push_back(std::move(cells));
    }
    return result;
  }

  // --- QLabel ---
  [[nodiscard]] std::optional<std::string> label(std::string_view name) const {
    return get_string(name, "label");
  }

  // --- QPushButton ---
  [[nodiscard]] std::optional<std::string> button_text(std::string_view name) const {
    return get_string(name, "button_text");
  }

  // --- File picker ---
  [[nodiscard]] bool is_file_picker(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) return false;
    auto it = w->find("action");
    return it != w->end() && it->is_string() && it->get<std::string>() == "file_picker";
  }

  [[nodiscard]] std::optional<std::string> file_picker_filter(std::string_view name) const {
    return get_string(name, "filter");
  }
  [[nodiscard]] std::optional<std::string> file_picker_title(std::string_view name) const {
    return get_string(name, "title");
  }

  // --- QDialogButtonBox ---
  [[nodiscard]] std::optional<bool> ok_enabled(std::string_view name) const {
    return get_bool(name, "ok_enabled");
  }

  // --- QTabWidget ---
  [[nodiscard]] std::optional<int> tab_index(std::string_view name) const {
    return get_int(name, "tab_index");
  }

  // --- Generic (any widget) ---
  [[nodiscard]] std::optional<bool> enabled(std::string_view name) const {
    return get_bool(name, "enabled");
  }
  [[nodiscard]] std::optional<bool> visible(std::string_view name) const {
    return get_bool(name, "visible");
  }

  // --- Enumeration ---
  [[nodiscard]] std::vector<std::string> widget_names() const {
    std::vector<std::string> names;
    if (data_.is_object()) {
      names.reserve(data_.size());
      for (const auto& [key, _] : data_.items()) {
        names.push_back(key);
      }
    }
    return names;
  }

  [[nodiscard]] bool has_widget(std::string_view name) const { return widget(name) != nullptr; }

  // --- Raw access ---
  [[nodiscard]] const nlohmann::json& raw() const { return data_; }

 private:
  nlohmann::json data_;

  const nlohmann::json* widget(std::string_view name) const {
    auto it = data_.find(std::string(name));
    if (it == data_.end() || !it->is_object()) return nullptr;
    return &(*it);
  }

  std::optional<std::string> get_string(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find(field);
    if (it == w->end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
  }

  std::optional<int> get_int(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find(field);
    if (it == w->end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int>();
  }

  std::optional<bool> get_bool(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find(field);
    if (it == w->end() || !it->is_boolean()) return std::nullopt;
    return it->get<bool>();
  }

  std::optional<double> get_double(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find(field);
    if (it == w->end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
  }

  std::optional<std::vector<std::string>> get_string_array(std::string_view name,
                                                           const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) return std::nullopt;
    auto it = w->find(field);
    if (it == w->end() || !it->is_array()) return std::nullopt;
    std::vector<std::string> result;
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
    return result;
  }
};

}  // namespace PJ::host
