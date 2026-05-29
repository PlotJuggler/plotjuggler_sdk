#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// Static factory methods that produce event JSON strings for sendEvent().
/// Each method returns a complete JSON string matching what PJ::WidgetEvent expects.
struct WidgetEventBuilder {
  /// QLineEdit: text changed
  [[nodiscard]] static std::string textChanged(std::string_view text) {
    nlohmann::json j;
    j["text"] = text;
    return j.dump();
  }

  /// QComboBox: index changed (index only)
  [[nodiscard]] static std::string indexChanged(int index) {
    nlohmann::json j;
    j["current_index"] = index;
    return j.dump();
  }

  /// QComboBox: index changed (index + current text)
  [[nodiscard]] static std::string indexChanged(int index, std::string_view current_text) {
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
  [[nodiscard]] static std::string valueChanged(int value) {
    nlohmann::json j;
    j["value"] = value;
    return j.dump();
  }

  /// QDoubleSpinBox: value changed (double)
  [[nodiscard]] static std::string valueChanged(double value) {
    nlohmann::json j;
    j["value"] = value;
    return j.dump();
  }

  /// QListWidget: selection changed
  [[nodiscard]] static std::string selectionChanged(const std::vector<std::string>& selected) {
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
  [[nodiscard]] static std::string fileSelected(std::string_view path) {
    nlohmann::json j;
    j["file_selected"] = path;
    return j.dump();
  }

  /// Folder picker: folder selected
  [[nodiscard]] static std::string folderSelected(std::string_view path) {
    nlohmann::json j;
    j["folder_selected"] = path;
    return j.dump();
  }

  /// QTabWidget: tab changed
  [[nodiscard]] static std::string tabChanged(int index) {
    nlohmann::json j;
    j["tab_index"] = index;
    return j.dump();
  }

  /// QListWidget: item double-clicked
  [[nodiscard]] static std::string itemDoubleClicked(int index) {
    nlohmann::json j;
    j["item_double_clicked_index"] = index;
    return j.dump();
  }

  /// QTableWidget: a horizontal-header section was clicked (column index).
  /// Lets a plugin own column sorting — it re-sorts its row model and re-emits
  /// rows, keeping index-based selection/visibility consistent.
  [[nodiscard]] static std::string headerClicked(int section) {
    nlohmann::json j;
    j["header_section"] = section;
    return j.dump();
  }

  /// Code editor: code changed. `cursor` is the caret offset (bytes) in the new
  /// text, or negative when unknown; it is serialized only when >= 0, so callers
  /// that omit it stay wire-compatible with readers that ignore the field.
  [[nodiscard]] static std::string codeChanged(std::string_view code, int cursor = -1) {
    nlohmann::json j;
    j["code_changed"] = code;
    if (cursor >= 0) {
      j["code_cursor"] = cursor;
    }
    return j.dump();
  }

  /// Drag-and-drop: items dropped on a widget (curves, files, or any draggable payload).
  [[nodiscard]] static std::string itemsDropped(const std::vector<std::string>& labels) {
    nlohmann::json j;
    j["items_dropped"] = labels;
    return j.dump();
  }

  /// DateRangePicker: date/time range filter changed. from/to are ISO-8601
  /// datetime strings (empty = unbounded on that side).
  [[nodiscard]] static std::string dateRangeChanged(std::string_view from_iso, std::string_view to_iso) {
    nlohmann::json j;
    j["date_from_iso"] = from_iso;
    j["date_to_iso"] = to_iso;
    return j.dump();
  }

  /// RangeSlider: lower/upper handle position changed (slider units).
  [[nodiscard]] static std::string rangeChanged(int lower, int upper) {
    nlohmann::json j;
    j["range_lower"] = lower;
    j["range_upper"] = upper;
    return j.dump();
  }

  /// ChartPreviewWidget: visible range changed via zoom or pan.
  [[nodiscard]] static std::string chartViewChanged(double x_min, double x_max, double y_min, double y_max) {
    nlohmann::json j;
    j["chart_x_min"] = x_min;
    j["chart_x_max"] = x_max;
    j["chart_y_min"] = y_min;
    j["chart_y_max"] = y_max;
    return j.dump();
  }
};

}  // namespace PJ
