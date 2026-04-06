#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// Read-only view of the JSON returned by get_widget_data().
/// Each getter takes a widget name and reads the corresponding property.
/// Returns std::nullopt for missing widgets or missing/wrong-typed fields.
class WidgetDataView {
 public:
  explicit WidgetDataView(std::string_view json) : data_(nlohmann::json::parse(json, nullptr, false)) {
    if (data_.is_discarded()) {
      data_ = nlohmann::json::object();
    }
  }

  // --- QLineEdit ---
  [[nodiscard]] std::optional<std::string> text(std::string_view name) const {
    return getString(name, "text");
  }
  [[nodiscard]] std::optional<std::string> placeholder(std::string_view name) const {
    return getString(name, "placeholder");
  }
  [[nodiscard]] std::optional<bool> readOnly(std::string_view name) const {
    return getBool(name, "read_only");
  }

  // --- QComboBox ---
  [[nodiscard]] std::optional<int> currentIndex(std::string_view name) const {
    return getInt(name, "current_index");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> items(std::string_view name) const {
    return getStringArray(name, "items");
  }

  // --- QCheckBox, QRadioButton ---
  [[nodiscard]] std::optional<bool> checked(std::string_view name) const {
    return getBool(name, "checked");
  }

  // --- QSpinBox ---
  [[nodiscard]] std::optional<int> valueInt(std::string_view name) const {
    return getInt(name, "value");
  }

  // --- QDoubleSpinBox ---
  [[nodiscard]] std::optional<double> valueDouble(std::string_view name) const {
    return getDouble(name, "value");
  }

  [[nodiscard]] std::optional<int> rangeMin(std::string_view name) const {
    return getInt(name, "min");
  }
  [[nodiscard]] std::optional<int> rangeMax(std::string_view name) const {
    return getInt(name, "max");
  }

  // --- QListWidget ---
  [[nodiscard]] std::optional<std::vector<std::string>> listItems(std::string_view name) const {
    return getStringArray(name, "list_items");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> selectedItems(std::string_view name) const {
    return getStringArray(name, "selected_items");
  }

  // --- QTableWidget ---
  [[nodiscard]] std::optional<std::vector<std::string>> tableHeaders(std::string_view name) const {
    return getStringArray(name, "headers");
  }

  [[nodiscard]] std::optional<std::vector<std::vector<std::string>>> tableRows(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("rows");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<std::vector<std::string>> result;
    result.reserve(it->size());
    for (const auto& row : *it) {
      if (!row.is_array()) {
        return std::nullopt;
      }
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

  [[nodiscard]] std::optional<std::vector<int>> selectedRows(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("selected_rows");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<int> result;
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_number_integer()) {
        result.push_back(item.get<int>());
      }
    }
    return result;
  }

  // --- QPlainTextEdit ---
  [[nodiscard]] std::optional<std::string> plainText(std::string_view name) const {
    return getString(name, "plain_text");
  }

  // --- QLabel ---
  [[nodiscard]] std::optional<std::string> label(std::string_view name) const {
    return getString(name, "label");
  }

  // --- QPushButton ---
  [[nodiscard]] std::optional<std::string> buttonText(std::string_view name) const {
    return getString(name, "button_text");
  }

  [[nodiscard]] std::optional<std::string> shortcut(std::string_view name) const {
    return getString(name, "shortcut");
  }

  // --- File picker ---
  [[nodiscard]] bool isFilePicker(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return false;
    }
    auto it = w->find("action");
    return it != w->end() && it->is_string() && it->get<std::string>() == "file_picker";
  }

  [[nodiscard]] std::optional<std::string> filePickerFilter(std::string_view name) const {
    return getString(name, "filter");
  }
  [[nodiscard]] std::optional<std::string> filePickerTitle(std::string_view name) const {
    return getString(name, "title");
  }

  // --- Folder picker ---
  [[nodiscard]] bool isFolderPicker(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return false;
    }
    auto it = w->find("action");
    return it != w->end() && it->is_string() && it->get<std::string>() == "folder_picker";
  }

  [[nodiscard]] std::optional<std::string> folderPickerTitle(std::string_view name) const {
    return getString(name, "title");
  }

  // --- QDialogButtonBox ---
  [[nodiscard]] std::optional<bool> okEnabled(std::string_view name) const {
    return getBool(name, "ok_enabled");
  }

  // --- QTabWidget ---
  [[nodiscard]] std::optional<int> tabIndex(std::string_view name) const {
    return getInt(name, "tab_index");
  }

  // --- Generic (any widget) ---
  [[nodiscard]] std::optional<bool> enabled(std::string_view name) const {
    return getBool(name, "enabled");
  }
  [[nodiscard]] std::optional<bool> visible(std::string_view name) const {
    return getBool(name, "visible");
  }

  // --- Dialog-level commands ---
  [[nodiscard]] bool requestAccept() const {
    auto it = data_.find("__request_accept");
    return it != data_.end() && it->is_boolean() && it->get<bool>();
  }

  /// Returns the sub-dialog UI XML if a sub-dialog was requested, or nullopt.
  [[nodiscard]] std::optional<std::string> subDialogUi() const {
    auto it = data_.find("__request_sub_dialog");
    if (it == data_.end() || !it->is_object()) {
      return std::nullopt;
    }
    auto ui_it = it->find("ui");
    if (ui_it == it->end() || !ui_it->is_string()) {
      return std::nullopt;
    }
    return ui_it->get<std::string>();
  }

  // --- Enumeration ---
  [[nodiscard]] std::vector<std::string> widgetNames() const {
    std::vector<std::string> names;
    if (data_.is_object()) {
      names.reserve(data_.size());
      for (const auto& [key, _] : data_.items()) {
        names.push_back(key);
      }
    }
    return names;
  }

  [[nodiscard]] bool hasWidget(std::string_view name) const {
    return widget(name) != nullptr;
  }

  // --- Raw access ---
  [[nodiscard]] const nlohmann::json& raw() const {
    return data_;
  }

 private:
  nlohmann::json data_;

  const nlohmann::json* widget(std::string_view name) const {
    auto it = data_.find(std::string(name));
    if (it == data_.end() || !it->is_object()) {
      return nullptr;
    }
    return &(*it);
  }

  std::optional<std::string> getString(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find(field);
    if (it == w->end() || !it->is_string()) {
      return std::nullopt;
    }
    return it->get<std::string>();
  }

  std::optional<int> getInt(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find(field);
    if (it == w->end() || !it->is_number_integer()) {
      return std::nullopt;
    }
    return it->get<int>();
  }

  std::optional<bool> getBool(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find(field);
    if (it == w->end() || !it->is_boolean()) {
      return std::nullopt;
    }
    return it->get<bool>();
  }

  std::optional<double> getDouble(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find(field);
    if (it == w->end() || !it->is_number()) {
      return std::nullopt;
    }
    return it->get<double>();
  }

  std::optional<std::vector<std::string>> getStringArray(std::string_view name, const char* field) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find(field);
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
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

}  // namespace PJ
