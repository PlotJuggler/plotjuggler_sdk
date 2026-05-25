#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
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

  [[nodiscard]] std::optional<std::vector<int>> disabledRows(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("disabled_rows");
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

  // --- Chart (QFrame used as chart container) ---

  struct ChartSeriesView {
    std::string label;
    std::vector<std::pair<double, double>> points;  // {x, y}
    std::string color;                              // optional hex "#rrggbb"; empty means use chart theme default
  };

  [[nodiscard]] std::optional<std::vector<ChartSeriesView>> chartSeries(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("chart_series");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<ChartSeriesView> result;
    result.reserve(it->size());
    for (const auto& s : *it) {
      if (!s.is_object()) {
        continue;
      }
      ChartSeriesView sv;
      auto label_it = s.find("label");
      if (label_it != s.end() && label_it->is_string()) {
        sv.label = label_it->get<std::string>();
      }
      auto pts_it = s.find("points");
      if (pts_it != s.end() && pts_it->is_array()) {
        sv.points.reserve(pts_it->size());
        for (const auto& pt : *pts_it) {
          if (pt.is_array() && pt.size() == 2 && pt[0].is_number() && pt[1].is_number()) {
            sv.points.emplace_back(pt[0].get<double>(), pt[1].get<double>());
          }
        }
      }
      auto color_it = s.find("color");
      if (color_it != s.end() && color_it->is_string()) {
        sv.color = color_it->get<std::string>();
      }
      result.push_back(std::move(sv));
    }
    return result;
  }

  /// Returns whether interactive zoom is enabled on this chart widget.
  [[nodiscard]] std::optional<bool> chartZoomEnabled(std::string_view name) const {
    return getBool(name, "chart_zoom_enabled");
  }

  // --- QPlainTextEdit ---
  [[nodiscard]] std::optional<std::string> plainText(std::string_view name) const {
    return getString(name, "plain_text");
  }

  // --- Code editor ---
  [[nodiscard]] std::optional<std::string> codeContent(std::string_view name) const {
    return getString(name, "code_content");
  }
  [[nodiscard]] std::optional<std::string> codeLanguage(std::string_view name) const {
    return getString(name, "code_language");
  }

  // --- QLabel ---
  [[nodiscard]] std::optional<std::string> label(std::string_view name) const {
    return getString(name, "label");
  }

  // --- QPushButton ---
  [[nodiscard]] std::optional<std::string> buttonText(std::string_view name) const {
    return getString(name, "button_text");
  }

  [[nodiscard]] std::optional<std::string> buttonIconSvg(std::string_view name) const {
    return getString(name, "button_icon_svg");
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

  // --- RangeSlider ---
  [[nodiscard]] std::optional<int> rangeSliderMin(std::string_view name) const {
    return getInt(name, "range_min");
  }
  [[nodiscard]] std::optional<int> rangeSliderMax(std::string_view name) const {
    return getInt(name, "range_max");
  }
  [[nodiscard]] std::optional<int> rangeSliderLower(std::string_view name) const {
    return getInt(name, "range_lower");
  }
  [[nodiscard]] std::optional<int> rangeSliderUpper(std::string_view name) const {
    return getInt(name, "range_upper");
  }
  /// Returns the [min_ns, max_ns] time window for duration labels, parsed from
  /// the string-encoded nanosecond fields. nullopt if not set.
  [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> rangeSliderTimeSpan(std::string_view name) const {
    auto mn = getString(name, "range_time_min_ns");
    auto mx = getString(name, "range_time_max_ns");
    if (!mn.has_value() || !mx.has_value()) {
      return std::nullopt;
    }
    try {
      return std::make_pair(static_cast<std::int64_t>(std::stoll(*mn)), static_cast<std::int64_t>(std::stoll(*mx)));
    } catch (...) {
      return std::nullopt;
    }
  }

  // --- SequencePicker ---
  [[nodiscard]] std::optional<std::string> datePickerEarliest(std::string_view name) const {
    return getString(name, "picker_earliest");
  }

  // --- MetadataQueryBar ---
  [[nodiscard]] std::optional<std::vector<std::string>> queryKeys(std::string_view name) const {
    return getStringArray(name, "query_keys");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> queryOperators(std::string_view name) const {
    return getStringArray(name, "query_ops");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> queryValues(std::string_view name) const {
    return getStringArray(name, "query_values");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> queryCompletions(std::string_view name) const {
    return getStringArray(name, "query_completions");
  }
  /// Read the key→values schema for a self-contained MetadataQueryBar (the JSON
  /// is a {key: [values]} object written by WidgetData::setQuerySchema).
  [[nodiscard]] std::optional<std::map<std::string, std::vector<std::string>>> querySchema(
      std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("query_schema");
    if (it == w->end() || !it->is_object()) {
      return std::nullopt;
    }
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& [key, vals] : it->items()) {
      if (!vals.is_array()) {
        continue;
      }
      std::vector<std::string> values;
      values.reserve(vals.size());
      for (const auto& v : vals) {
        if (v.is_string()) {
          values.push_back(v.get<std::string>());
        }
      }
      result.emplace(key, std::move(values));
    }
    return result;
  }
  [[nodiscard]] std::optional<std::string> queryFeedbackText(std::string_view name) const {
    return getString(name, "query_feedback_text");
  }
  [[nodiscard]] std::optional<bool> queryFeedbackOk(std::string_view name) const {
    return getBool(name, "query_feedback_ok");
  }

  // --- QDialogButtonBox ---
  [[nodiscard]] std::optional<bool> okEnabled(std::string_view name) const {
    return getBool(name, "ok_enabled");
  }

  // --- QTabWidget ---
  [[nodiscard]] std::optional<int> tabIndex(std::string_view name) const {
    return getInt(name, "tab_index");
  }

  // --- Drop target ---
  [[nodiscard]] bool isDropTarget(std::string_view name) const {
    return getBool(name, "drop_target").value_or(false);
  }

  /// Return all widget names that declare drop_target: true.
  [[nodiscard]] std::vector<std::string> dropTargets() const {
    std::vector<std::string> result;
    if (!data_.is_object()) {
      return result;
    }
    for (const auto& [key, val] : data_.items()) {
      if (val.is_object()) {
        auto it = val.find("drop_target");
        if (it != val.end() && it->is_boolean() && it->get<bool>()) {
          result.push_back(key);
        }
      }
    }
    return result;
  }

  // --- QDateTimeEdit ---
  [[nodiscard]] std::optional<std::string> dateTime(std::string_view name) const {
    return getString(name, "datetime");
  }
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> dateTimeRange(std::string_view name) const {
    auto mn = getString(name, "datetime_min");
    auto mx = getString(name, "datetime_max");
    if (!mn.has_value() && !mx.has_value()) {
      return std::nullopt;
    }
    return std::make_pair(mn.value_or(std::string()), mx.value_or(std::string()));
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

  /// Returns the close-reason string if WidgetData::requestClose was called, or
  /// nullopt. Observed by PanelEngine; ignored by DialogEngine.
  [[nodiscard]] std::optional<std::string> requestClose() const {
    auto it = data_.find("__request_close");
    if (it == data_.end() || !it->is_string()) {
      return std::nullopt;
    }
    return it->get<std::string>();
  }

  // --- QTableWidget filtering / styling (Mosaico parity) ---

  /// List of row indexes that should remain visible (rows not listed are
  /// hidden). Returns nullopt when the field is missing OR JSON null
  /// (clearVisibleRows): in both cases the host applies no visibility change
  /// this update. To show every row, send the full index list explicitly.
  /// NOTE: missing and null are intentionally indistinguishable here; a future
  /// 3-state contract (unset / show-all / explicit-set) would need a sentinel.
  [[nodiscard]] std::optional<std::vector<int>> visibleRows(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (w == nullptr) {
      return std::nullopt;
    }
    auto it = w->find("visible_rows");
    if (it == w->end() || it->is_null()) {
      return std::nullopt;
    }
    if (!it->is_array()) {
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

  /// row index → "#rrggbb" tint. Empty string clears the override.
  [[nodiscard]] std::optional<std::vector<std::pair<int, std::string>>> rowColors(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (w == nullptr) {
      return std::nullopt;
    }
    auto it = w->find("row_colors");
    if (it == w->end() || !it->is_object()) {
      return std::nullopt;
    }
    std::vector<std::pair<int, std::string>> result;
    for (auto kv = it->begin(); kv != it->end(); ++kv) {
      try {
        int row = std::stoi(kv.key());
        if (kv.value().is_string()) {
          result.emplace_back(row, kv.value().get<std::string>());
        }
      } catch (...) {
        // skip malformed keys
      }
    }
    return result;
  }

  /// (row, col, tooltip) tuples for cell-level tooltips.
  [[nodiscard]] std::optional<std::vector<std::tuple<int, int, std::string>>> cellTooltips(
      std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (w == nullptr) {
      return std::nullopt;
    }
    auto it = w->find("cell_tooltips");
    if (it == w->end() || !it->is_object()) {
      return std::nullopt;
    }
    std::vector<std::tuple<int, int, std::string>> result;
    for (auto kv = it->begin(); kv != it->end(); ++kv) {
      const std::string& key = kv.key();
      auto comma = key.find(',');
      if (comma == std::string::npos) {
        continue;
      }
      try {
        int row = std::stoi(key.substr(0, comma));
        int col = std::stoi(key.substr(comma + 1));
        if (kv.value().is_string()) {
          result.emplace_back(row, col, kv.value().get<std::string>());
        }
      } catch (...) {
        // skip malformed keys
      }
    }
    return result;
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
