#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// A single point in a chart series (used by setChartSeries).
struct ChartPoint {
  double x;
  double y;
};

/// A named series of XY points for chart display (used by setChartSeries).
/// If `color` is non-empty (e.g. "#ff7f0e"), it overrides the chart theme color
/// for this series; otherwise the Qt Charts theme picks one.
struct ChartSeries {
  std::string label;
  std::vector<ChartPoint> points;
  std::string color;  // optional hex "#rrggbb"
};

/// Builder for the JSON string returned by get_widget_data().
/// Each method targets an existing widget in the .ui file by its objectName.
class WidgetData {
 public:
  // --- QLineEdit ---
  WidgetData& setText(std::string_view name, std::string_view text) {
    entry(name)["text"] = text;
    return *this;
  }

  WidgetData& setPlaceholder(std::string_view name, std::string_view text) {
    entry(name)["placeholder"] = text;
    return *this;
  }

  WidgetData& setReadOnly(std::string_view name, bool read_only) {
    entry(name)["read_only"] = read_only;
    return *this;
  }

  // --- QComboBox ---
  WidgetData& setCurrentIndex(std::string_view name, int index) {
    entry(name)["current_index"] = index;
    return *this;
  }

  WidgetData& setItems(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["items"] = items;
    return *this;
  }

  // --- QCheckBox, QRadioButton ---
  WidgetData& setChecked(std::string_view name, bool checked) {
    entry(name)["checked"] = checked;
    return *this;
  }

  // --- QSpinBox ---
  WidgetData& setValue(std::string_view name, int value) {
    entry(name)["value"] = value;
    return *this;
  }

  // --- QDoubleSpinBox ---
  WidgetData& setValue(std::string_view name, double value) {
    entry(name)["value"] = value;
    return *this;
  }

  WidgetData& setRange(std::string_view name, int min, int max) {
    auto& e = entry(name);
    e["min"] = min;
    e["max"] = max;
    return *this;
  }

  // --- QListWidget ---
  WidgetData& setListItems(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["list_items"] = items;
    return *this;
  }

  WidgetData& setSelectedItems(std::string_view name, const std::vector<std::string>& selected) {
    entry(name)["selected_items"] = selected;
    return *this;
  }

  // --- QTableWidget ---
  WidgetData& setTableHeaders(std::string_view name, const std::vector<std::string>& headers) {
    entry(name)["headers"] = headers;
    return *this;
  }

  WidgetData& setTableRows(std::string_view name, const std::vector<std::vector<std::string>>& rows) {
    entry(name)["rows"] = rows;
    return *this;
  }

  WidgetData& setSelectedRows(std::string_view name, const std::vector<int>& rows) {
    entry(name)["selected_rows"] = rows;
    return *this;
  }

  WidgetData& setDisabledRows(std::string_view name, const std::vector<int>& rows) {
    entry(name)["disabled_rows"] = rows;
    return *this;
  }

  /// Hide rows whose indexes are NOT in the visible set (live filtering).
  /// Empty visible set hides every row. To show every row, pass the full
  /// index list (this is what the host acts on). clearVisibleRows() merely
  /// unsets the field — the host reads that as nullopt and makes NO visibility
  /// change, so it does not by itself re-show previously hidden rows.
  WidgetData& setVisibleRows(std::string_view name, const std::vector<int>& visible) {
    entry(name)["visible_rows"] = visible;
    return *this;
  }

  /// Unset any prior setVisibleRows (serializes JSON null). The host view
  /// surfaces this as nullopt (indistinguishable from "never set") and applies
  /// no visibility change; to actively re-show all rows, setVisibleRows() with
  /// the full index list instead.
  WidgetData& clearVisibleRows(std::string_view name) {
    auto& e = entry(name);
    e["visible_rows"] = nlohmann::json::value_t::null;
    return *this;
  }

  /// Tint a single row's background. `color_hex` follows "#rrggbb" or
  /// the empty string to clear the override.
  WidgetData& setRowColor(std::string_view name, int row, std::string_view color_hex) {
    auto& e = entry(name);
    auto& colors = e["row_colors"];
    if (!colors.is_object()) {
      colors = nlohmann::json::object();
    }
    colors[std::to_string(row)] = std::string(color_hex);
    return *this;
  }

  /// Set the tooltip on a single cell.
  WidgetData& setCellTooltip(std::string_view name, int row, int col, std::string_view tooltip) {
    auto& e = entry(name);
    auto& tt = e["cell_tooltips"];
    if (!tt.is_object()) {
      tt = nlohmann::json::object();
    }
    tt[std::to_string(row) + "," + std::to_string(col)] = std::string(tooltip);
    return *this;
  }

  // --- Chart (QFrame used as chart container) ---

  /// Set chart series data on a QFrame widget. The host will create or update
  /// a chart view inside the frame, displaying one QLineSeries per entry.
  WidgetData& setChartSeries(std::string_view name, const std::vector<ChartSeries>& series) {
    auto& e = entry(name);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : series) {
      nlohmann::json pts = nlohmann::json::array();
      for (const auto& p : s.points) {
        pts.push_back({p.x, p.y});
      }
      nlohmann::json entry = {{"label", s.label}, {"points", std::move(pts)}};
      if (!s.color.empty()) {
        entry["color"] = s.color;
      }
      arr.push_back(std::move(entry));
    }
    e["chart_series"] = std::move(arr);
    return *this;
  }

  /// Remove all series from the chart inside the named QFrame.
  WidgetData& clearChart(std::string_view name) {
    entry(name)["chart_series"] = nlohmann::json::array();
    return *this;
  }

  /// Enable interactive zoom (rubber band + mouse wheel) on the chart inside the named QFrame.
  /// When enabled, onChartViewChanged() is called whenever the user zooms or pans.
  WidgetData& setChartZoomEnabled(std::string_view name, bool enabled = true) {
    entry(name)["chart_zoom_enabled"] = enabled;
    return *this;
  }

  // --- QPlainTextEdit ---
  WidgetData& setPlainText(std::string_view name, std::string_view text) {
    entry(name)["plain_text"] = text;
    return *this;
  }

  // --- Code editor (QPlainTextEdit with syntax highlighting) ---

  /// Set the code content of a QPlainTextEdit used as a code editor.
  /// Unlike setPlainText (read-only), this enables editing and wires onCodeChanged events.
  WidgetData& setCodeContent(std::string_view name, std::string_view code) {
    entry(name)["code_content"] = code;
    return *this;
  }

  /// Set the language for syntax highlighting (e.g. "lua", "python").
  WidgetData& setCodeLanguage(std::string_view name, std::string_view lang) {
    entry(name)["code_language"] = lang;
    return *this;
  }

  // --- QLabel ---
  WidgetData& setLabel(std::string_view name, std::string_view text) {
    entry(name)["label"] = text;
    return *this;
  }

  // --- QPushButton ---
  WidgetData& setButtonText(std::string_view name, std::string_view text) {
    entry(name)["button_text"] = text;
    return *this;
  }

  /// Set an icon on a QPushButton from inline SVG data.
  /// The SVG string is stored as-is and rendered by the host via QSvgRenderer.
  WidgetData& setButtonIcon(std::string_view name, std::string_view svg_data) {
    entry(name)["button_icon_svg"] = svg_data;
    return *this;
  }

  /// Assign a keyboard shortcut to a QPushButton (e.g. "Ctrl+A", "Ctrl+Shift+A").
  /// The host creates a QShortcut that triggers click() on the button.
  WidgetData& setShortcut(std::string_view name, std::string_view key_sequence) {
    entry(name)["shortcut"] = key_sequence;
    return *this;
  }

  WidgetData& setFilePicker(
      std::string_view name, std::string_view button_text, std::string_view filter, std::string_view title) {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "file_picker";
    e["filter"] = filter;
    e["title"] = title;
    return *this;
  }

  WidgetData& setFolderPicker(std::string_view name, std::string_view button_text, std::string_view title) {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "folder_picker";
    e["title"] = title;
    return *this;
  }

  // --- QDateTimeEdit ---
  /// Set the displayed date+time as an ISO-8601 string (e.g. "2026-05-21T13:45:00").
  /// Empty string clears any prior value (widget falls back to its default).
  WidgetData& setDateTime(std::string_view name, std::string_view iso8601) {
    entry(name)["datetime"] = std::string(iso8601);
    return *this;
  }

  /// Set the allowed [min, max] datetime range for a QDateTimeEdit.
  WidgetData& setDateTimeRange(std::string_view name, std::string_view min_iso, std::string_view max_iso) {
    auto& e = entry(name);
    e["datetime_min"] = std::string(min_iso);
    e["datetime_max"] = std::string(max_iso);
    return *this;
  }

  // --- RangeSlider (two-handle range slider) ---

  /// Set the integer [min, max] bounds of a RangeSlider. Setting bounds resets
  /// the handle values, so call setRangeSliderValues afterwards (or in the same
  /// widget_data tick) to restore them.
  WidgetData& setRangeSliderBounds(std::string_view name, int min, int max) {
    auto& e = entry(name);
    e["range_min"] = min;
    e["range_max"] = max;
    return *this;
  }

  /// Set the lower/upper handle positions of a RangeSlider (in slider units).
  WidgetData& setRangeSliderValues(std::string_view name, int lower, int upper) {
    auto& e = entry(name);
    e["range_lower"] = lower;
    e["range_upper"] = upper;
    return *this;
  }

  /// Enable duration floating labels on a RangeSlider, mapping the slider's
  /// [min, max] span onto the absolute time window [min_ns, max_ns]. Handle
  /// labels show the offset from min_ns; the center label shows the selected
  /// duration. Nanoseconds are carried as strings to avoid double precision
  /// loss on epoch-scale values. Pass min_ns == max_ns == 0 to disable.
  WidgetData& setRangeSliderTimeSpan(std::string_view name, std::int64_t min_ns, std::int64_t max_ns) {
    auto& e = entry(name);
    e["range_time_min_ns"] = std::to_string(min_ns);
    e["range_time_max_ns"] = std::to_string(max_ns);
    return *this;
  }

  // --- MetadataQueryBar (Lua filter + key/op/value selectors) ---

  WidgetData& setQueryKeys(std::string_view name, const std::vector<std::string>& keys) {
    entry(name)["query_keys"] = keys;
    return *this;
  }
  WidgetData& setQueryOperators(std::string_view name, const std::vector<std::string>& ops) {
    entry(name)["query_ops"] = ops;
    return *this;
  }
  WidgetData& setQueryValues(std::string_view name, const std::vector<std::string>& values) {
    entry(name)["query_values"] = values;
    return *this;
  }
  WidgetData& setQueryCompletions(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["query_completions"] = items;
    return *this;
  }
  /// Feed the full key→values schema to a self-contained MetadataQueryBar, which
  /// owns its own context-aware key/op/value combos and completion. nlohmann
  /// serializes the map as a JSON object {key: [values]}.
  WidgetData& setQuerySchema(std::string_view name, const std::map<std::string, std::vector<std::string>>& schema) {
    entry(name)["query_schema"] = schema;
    return *this;
  }
  /// Validation feedback under the editor. Empty text hides it; ok ⇒ green.
  WidgetData& setQueryFeedback(std::string_view name, std::string_view text, bool ok) {
    auto& e = entry(name);
    e["query_feedback_text"] = std::string(text);
    e["query_feedback_ok"] = ok;
    return *this;
  }

  // --- SequencePicker (date/time range picker) ---

  /// Set the "from" field placeholder of a SequencePicker to the dataset's
  /// earliest date (ISO-8601 date, e.g. "2016-04-29"). Empty resets the hint.
  WidgetData& setDatePickerEarliest(std::string_view name, std::string_view iso_date) {
    entry(name)["picker_earliest"] = std::string(iso_date);
    return *this;
  }

  // --- QDialogButtonBox ---
  /// Set OK button enabled state. The widget name defaults to "buttonBox"
  /// which is the required name for the DialogEngine to wire accept/reject.
  WidgetData& setOkEnabled(bool enabled) {
    entry("buttonBox")["ok_enabled"] = enabled;
    return *this;
  }
  WidgetData& setOkEnabled(std::string_view name, bool enabled) {
    entry(name)["ok_enabled"] = enabled;
    return *this;
  }

  // --- QTabWidget ---
  WidgetData& setTabIndex(std::string_view name, int index) {
    entry(name)["tab_index"] = index;
    return *this;
  }

  // --- Generic (any widget) ---
  WidgetData& setEnabled(std::string_view name, bool enabled) {
    entry(name)["enabled"] = enabled;
    return *this;
  }

  WidgetData& setVisible(std::string_view name, bool visible) {
    entry(name)["visible"] = visible;
    return *this;
  }

  // --- Drop target ---

  /// Mark a widget as a drag-and-drop target for field curves.
  /// The DialogEngine reads this on init and installs a DropEventFilter for it.
  WidgetData& setDropTarget(std::string_view name, bool is_target = true) {
    entry(name)["drop_target"] = is_target;
    return *this;
  }

  // --- Dialog-level commands ---

  /// Request that the dialog accept (close with OK) after applying this widget data.
  /// Typically used to implement double-click-to-accept on list items.
  WidgetData& requestAccept() {
    data_["__request_accept"] = true;
    return *this;
  }

  /// Request that the host open a sub-dialog with the given UI XML.
  /// The sub-dialog is shown as a nested modal. When the user closes it,
  /// the main dialog resumes. The sub-dialog is read-only (no plugin events).
  WidgetData& requestSubDialog(std::string_view ui_xml) {
    data_["__request_sub_dialog"] = nlohmann::json{{"ui", ui_xml}};
    return *this;
  }

  /// Request that the host close the panel hosting this plugin (PanelEngine).
  /// `reason` is a free-form plugin-defined string (e.g. "import_complete",
  /// "user_back", "error") forwarded to the host's onCloseRequested callback.
  /// DialogEngine ignores this command — it has its own accept/reject flow
  /// via requestAccept() and the buttonBox. PanelEngine observes it on every
  /// tick and tears down the panel after the callback fires.
  WidgetData& requestClose(std::string_view reason) {
    data_["__request_close"] = std::string(reason);
    return *this;
  }

  /// Serialize to JSON string
  std::string toJson() const {
    return data_.dump();
  }

  /// Reset for reuse
  void clear() {
    data_ = nlohmann::json::object();
  }

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

}  // namespace PJ
