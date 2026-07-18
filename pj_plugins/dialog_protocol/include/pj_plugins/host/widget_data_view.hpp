#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "pj_base/number_parse.hpp"
#include "pj_plugins/sdk/widget_data.hpp"  // TimelineMark

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
  [[nodiscard]] std::optional<bool> listDeletable(std::string_view name) const {
    return getBool(name, "list_deletable");
  }
  [[nodiscard]] std::optional<std::string> listPlaceholder(std::string_view name) const {
    return getString(name, "list_placeholder");
  }
  [[nodiscard]] std::optional<std::vector<std::string>> listItemColors(std::string_view name) const {
    return getStringArray(name, "list_item_colors");
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

  /// Column to render as an exclusive radio-button group (see setTableRadioColumn).
  [[nodiscard]] std::optional<int> tableRadioColumn(std::string_view name) const {
    return getInt(name, "radio_column");
  }
  /// Row whose radio is checked in the radio column (-1 = none).
  [[nodiscard]] std::optional<int> tableRadioCheckedRow(std::string_view name) const {
    return getInt(name, "radio_checked_row");
  }

  /// Decoded batch table delta (see WidgetData::appendTableRows et al.).
  /// Apply only when `seq` differs from the last seq applied to that widget,
  /// in the order: update_cells, then remove_rows, then append. All indexes
  /// address the table before the delta; `remove_rows` comes pre-normalized
  /// (descending, duplicate-free) so it can be applied in vector order.
  struct TableDeltaView {
    std::uint64_t seq = 0;
    std::vector<std::vector<std::string>> append;
    struct CellUpdate {
      int row = 0;
      int col = 0;
      std::string text;
    };
    std::vector<CellUpdate> update_cells;
    std::vector<int> remove_rows;
  };

  /// The delta's seq alone, without decoding the (possibly large) ops — check
  /// this against the last seq you applied before paying for tableDelta().
  [[nodiscard]] std::optional<std::uint64_t> tableDeltaSeq(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("table_delta");
    if (it == w->end() || !it->is_object()) {
      return std::nullopt;
    }
    auto seq_it = it->find("seq");
    if (seq_it == it->end() || !seq_it->is_number_unsigned()) {
      return std::nullopt;
    }
    return seq_it->get<std::uint64_t>();
  }

  [[nodiscard]] std::optional<TableDeltaView> tableDelta(std::string_view name) const {
    auto seq = tableDeltaSeq(name);
    if (!seq.has_value()) {
      return std::nullopt;
    }
    const nlohmann::json* w = widget(name);
    auto it = w->find("table_delta");
    // Strict decode: one malformed op rejects the whole delta. Partially
    // applying and then treating the seq as consumed would leave the table
    // diverged from the plugin's model with no way to repair it.
    // is_number_integer() accepts the full 64-bit range, so indexes are
    // range-checked before the narrowing get<int>() — otherwise an oversized
    // value (e.g. 2^32) would silently wrap into a valid-looking row.
    const auto is_row_index = [](const nlohmann::json& v) {
      if (!v.is_number_integer()) {
        return false;
      }
      const auto value = v.get<std::int64_t>();
      return value >= 0 && value <= std::numeric_limits<int>::max();
    };
    TableDeltaView delta;
    delta.seq = *seq;
    if (auto append_it = it->find("append"); append_it != it->end()) {
      if (!append_it->is_array()) {
        return std::nullopt;
      }
      for (const auto& row : *append_it) {
        if (!row.is_array()) {
          return std::nullopt;
        }
        std::vector<std::string> cells;
        cells.reserve(row.size());
        for (const auto& cell : row) {
          if (!cell.is_string()) {
            return std::nullopt;
          }
          cells.push_back(cell.get<std::string>());
        }
        delta.append.push_back(std::move(cells));
      }
    }
    if (auto cells_it = it->find("update_cells"); cells_it != it->end()) {
      if (!cells_it->is_array()) {
        return std::nullopt;
      }
      for (const auto& update : *cells_it) {
        if (!update.is_array() || update.size() != 3 || !is_row_index(update[0]) || !is_row_index(update[1]) ||
            !update[2].is_string()) {
          return std::nullopt;
        }
        delta.update_cells.push_back(
            {static_cast<int>(update[0].get<std::int64_t>()), static_cast<int>(update[1].get<std::int64_t>()),
             update[2].get<std::string>()});
      }
    }
    if (auto remove_it = it->find("remove_rows"); remove_it != it->end()) {
      if (!remove_it->is_array()) {
        return std::nullopt;
      }
      for (const auto& row : *remove_it) {
        if (!is_row_index(row)) {
          return std::nullopt;
        }
        delta.remove_rows.push_back(static_cast<int>(row.get<std::int64_t>()));
      }
      // Normalized for direct application: descending and duplicate-free, so a
      // host removing in vector order can never hit index shift.
      std::sort(delta.remove_rows.begin(), delta.remove_rows.end(), std::greater<>());
      delta.remove_rows.erase(std::unique(delta.remove_rows.begin(), delta.remove_rows.end()), delta.remove_rows.end());
    }
    return delta;
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
    bool dashed = false;                            // draw with a dashed line
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
      auto dashed_it = s.find("dashed");
      if (dashed_it != s.end() && dashed_it->is_boolean()) {
        sv.dashed = dashed_it->get<bool>();
      }
      result.push_back(std::move(sv));
    }
    return result;
  }

  /// A marker overlaid on the chart (see PJ::ChartMarker / setChartMarkers).
  /// Interpret by `kind`: "event" (point at x0,y0 if has_value, else vline at x0),
  /// "region" (x-span [x0,x1]), "value_band" (y-band [y0,y1]; line when y0==y1), "label".
  struct ChartMarkerView {
    std::string kind;
    double x0 = 0.0;
    double x1 = 0.0;
    double y0 = 0.0;
    double y1 = 0.0;
    bool has_value = false;
    std::string color;  // optional hex "#rrggbb"; empty means use a default palette
    std::string label;
  };

  [[nodiscard]] std::optional<std::vector<ChartMarkerView>> chartMarkers(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("chart_markers");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<ChartMarkerView> result;
    result.reserve(it->size());
    for (const auto& m : *it) {
      if (!m.is_object()) {
        continue;
      }
      ChartMarkerView mv;
      mv.kind = m.value("kind", std::string());
      mv.x0 = m.value("x0", 0.0);
      mv.x1 = m.value("x1", 0.0);
      mv.y0 = m.value("y0", 0.0);
      mv.y1 = m.value("y1", 0.0);
      mv.has_value = m.value("has_value", false);
      mv.color = m.value("color", std::string());
      mv.label = m.value("label", std::string());
      result.push_back(std::move(mv));
    }
    return result;
  }

  /// Returns whether interactive zoom is enabled on this chart widget.
  [[nodiscard]] std::optional<bool> chartZoomEnabled(std::string_view name) const {
    return getBool(name, "chart_zoom_enabled");
  }

  /// Returns whether the chart should auto-fit on every series update.
  [[nodiscard]] std::optional<bool> chartAutoZoom(std::string_view name) const {
    return getBool(name, "chart_auto_zoom");
  }

  /// Placeholder overlay text for the named chart (see WidgetData::setChartPlaceholder).
  [[nodiscard]] std::optional<std::string> chartPlaceholder(std::string_view name) const {
    return getString(name, "chart_placeholder");
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
  /// Requested caret offset (bytes) for a code editor; the host moves the caret
  /// here after applying new code content. Absent ⇒ leave the caret as-is.
  [[nodiscard]] std::optional<int> codeCursor(std::string_view name) const {
    return getInt(name, "code_cursor");
  }
  /// Whether this code editor opted into caret tracking (setCodeCaretTracking).
  /// Absent ⇒ not requested; the host wires cursor-move events only when true.
  [[nodiscard]] std::optional<bool> codeCaretTracking(std::string_view name) const {
    return getBool(name, "code_caret_tracking");
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

  /// Named icon id the host should resolve from its own themed icon set
  /// (see WidgetData::setButtonIconNamed). Unknown ids: render no icon.
  [[nodiscard]] std::optional<std::string> buttonIconName(std::string_view name) const {
    return getString(name, "button_icon_name");
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

  /// A "save-as" file picker (navigate + type a new filename), as opposed to the
  /// open-existing-file picker above. Shares the same filter/title accessors.
  [[nodiscard]] bool isSaveFilePicker(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return false;
    }
    auto it = w->find("action");
    return it != w->end() && it->is_string() && it->get<std::string>() == "save_file_picker";
  }

  /// The extension appended to a save-file picker's typed name when it has none.
  [[nodiscard]] std::optional<std::string> saveFilePickerDefaultSuffix(std::string_view name) const {
    return getString(name, "default_suffix");
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

  /// Boundary segments for a RangeSlider: (start, end, label) in slider units.
  /// nullopt when never set; an empty vector when explicitly cleared.
  struct Marker {
    int start = 0;
    int end = 0;
    std::string label;
  };
  [[nodiscard]] std::optional<std::vector<Marker>> rangeSliderMarkers(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("range_markers");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<Marker> out;
    out.reserve(it->size());
    for (const auto& m : *it) {
      if (!m.is_object()) {
        continue;
      }
      auto s = m.find("start");
      auto e = m.find("end");
      auto l = m.find("label");
      if (s == m.end() || !s->is_number_integer()) {
        continue;
      }
      Marker mk;
      mk.start = s->get<int>();
      mk.end = (e != m.end() && e->is_number_integer()) ? e->get<int>() : mk.start;
      mk.label = (l != m.end() && l->is_string()) ? l->get<std::string>() : std::string{};
      out.push_back(std::move(mk));
    }
    return out;
  }

  // --- DateRangePicker ---
  [[nodiscard]] std::optional<std::string> dateRangeEarliest(std::string_view name) const {
    return getString(name, "date_range_earliest");
  }
  [[nodiscard]] std::optional<std::string> dateRangeLatest(std::string_view name) const {
    return getString(name, "date_range_latest");
  }

  // --- MarkerTimeline ---
  [[nodiscard]] std::optional<int> markerTimelineMin(std::string_view name) const {
    return getInt(name, "marker_timeline_min");
  }
  [[nodiscard]] std::optional<int> markerTimelineMax(std::string_view name) const {
    return getInt(name, "marker_timeline_max");
  }
  [[nodiscard]] std::optional<std::vector<TimelineMark>> markerTimelineMarks(std::string_view name) const {
    const nlohmann::json* w = widget(name);
    if (!w) {
      return std::nullopt;
    }
    auto it = w->find("marker_timeline_marks");
    if (it == w->end() || !it->is_array()) {
      return std::nullopt;
    }
    return timelineMarksFromJson(*it);
  }
  /// [min_ns, max_ns] time window for hover labels, parsed from string-encoded ns.
  [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> markerTimelineTimeSpan(
      std::string_view name) const {
    auto mn = getString(name, "marker_timeline_time_min_ns");
    auto mx = getString(name, "marker_timeline_time_max_ns");
    if (!mn.has_value() || !mx.has_value()) {
      return std::nullopt;
    }
    try {
      return std::make_pair(static_cast<std::int64_t>(std::stoll(*mn)), static_cast<std::int64_t>(std::stoll(*mx)));
    } catch (...) {
      return std::nullopt;
    }
  }

  // --- Field validity indicator (generic) ---
  [[nodiscard]] std::optional<bool> fieldValid(std::string_view name) const {
    return getBool(name, "valid");
  }
  [[nodiscard]] std::optional<std::string> fieldValidTooltip(std::string_view name) const {
    return getString(name, "valid_tooltip");
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

  /// Returns the interactive sub-panel UI XML if requestSubPanel was called, or
  /// nullopt. The sub-panel is live (events flow back to the plugin); see
  /// WidgetData::requestSubPanel. Observed by PanelEngine.
  [[nodiscard]] std::optional<std::string> subPanelUi() const {
    auto it = data_.find("__request_sub_panel");
    if (it == data_.end() || !it->is_object()) {
      return std::nullopt;
    }
    auto ui_it = it->find("ui");
    if (ui_it == it->end() || !ui_it->is_string()) {
      return std::nullopt;
    }
    return ui_it->get<std::string>();
  }

  /// True if WidgetData::closeSubPanel was called (dismiss the interactive sub-panel).
  [[nodiscard]] bool subPanelClose() const {
    auto it = data_.find("__request_sub_panel_close");
    return it != data_.end() && it->is_boolean() && it->get<bool>();
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
      const auto row = parseNumber<int>(kv.key());
      if (!row.has_value() || !kv.value().is_string()) {
        continue;
      }
      result.emplace_back(*row, kv.value().get<std::string>());
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
      const auto comma = key.find(',');
      if (comma == std::string::npos) {
        continue;
      }
      const auto row = parseNumber<int>(std::string_view{key}.substr(0, comma));
      const auto col = parseNumber<int>(std::string_view{key}.substr(comma + 1));
      if (!row.has_value() || !col.has_value() || !kv.value().is_string()) {
        continue;
      }
      result.emplace_back(*row, *col, kv.value().get<std::string>());
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
