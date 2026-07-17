#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// A single point in a chart series (used by setChartSeries).
struct ChartPoint {
  double x;
  double y;
};

/// A table cell carrying both what it displays and what it sorts by.
///
/// `text` is rendered verbatim — the plugin owns formatting (`%g`, `%.2f`, units,
/// "N/A"). `value` is the ordering truth. Without it the host can only compare the
/// rendered string, so a numeric column sorts lexicographically ("9" lands after
/// "10"); supplying it makes the host sort on the number instead.
///
/// `value` need not resemble `text`: `TableItem(ns_since_epoch, "2026-07-17 10:23")`
/// displays a date and sorts on int64 nanoseconds. Leave it unset (the string
/// constructors do) for text columns — those sort by `text`.
///
/// Pass the native type: `NumericValue` keeps each width exactly, so a uint64 count
/// or an int64 nanosecond timestamp never round-trips through a double (which is
/// only exact to 2^53).
///
/// Non-finite doubles (NaN, ±infinity) do not survive the JSON wire — dump()
/// serializes them as null — so such a cell arrives at the host keyless and
/// sorts in the text rank with the other keyless cells, not at the numeric
/// extremes. Map infinities to a finite sentinel first if their ordering
/// matters.
struct TableItem {
  std::string text;
  std::optional<NumericValue> value;

  TableItem() = default;
  TableItem(std::string display) : text(std::move(display)) {}
  TableItem(const char* display) : text(display) {}

  /// Numeric cell rendered with the default representation of `v`. long double
  /// is excluded up front — NumericValue has no alternative for it, and letting
  /// it past the constraint would fail with an incomprehensible variant error.
  template <
      typename T, typename = std::enable_if_t<
                      std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, long double>>>
  TableItem(T v) : text(std::to_string(v)), value(NumericValue(v)) {}

  /// Numeric cell with plugin-controlled rendering — `display` is shown, `v` is sorted on.
  template <
      typename T, typename = std::enable_if_t<
                      std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, long double>>>
  TableItem(T v, std::string display) : text(std::move(display)), value(NumericValue(v)) {}
};

/// A named series of XY points for chart display (used by setChartSeries).
/// If `color` is non-empty (e.g. "#ff7f0e"), it overrides the built-in palette
/// color for this series; otherwise the host's chart palette picks one.
struct ChartSeries {
  std::string label;
  std::vector<ChartPoint> points;
  std::string color;    // optional hex "#rrggbb"
  bool dashed = false;  // draw with a dashed line (e.g. a faded "before" ghost curve)
};

/// One marker overlaid on a chart preview (used by setChartMarkers). Interpret by
/// `kind`, mirroring the PlotMarkers vocabulary:
///   "event"      → a point at (x0, y0) when `has_value`, else a vertical line at x0;
///   "region"     → a shaded x-span [x0, x1];
///   "value_band" → a shaded y-band [y0, y1] (a horizontal line when y0 == y1);
///   "label"      → a labelled mark at x0.
/// X/Y are in the chart's own units (e.g. seconds-from-start), so the producer must
/// rebase to match the series points it pushes alongside.
struct ChartMarker {
  std::string kind;  // "event" | "region" | "value_band" | "label"
  double x0 = 0.0;
  double x1 = 0.0;
  double y0 = 0.0;
  double y1 = 0.0;
  bool has_value = false;  // event: y0 is a real point value (draw a point, not a bare vline)
  std::string color;       // optional hex "#rrggbb"; empty → host derives from a default palette
  std::string label;
};

/// The single source of truth for the ChartMarker ⇆ JSON wire shape, shared by the
/// data setter and the data-view (so a field change cannot drift between them).
[[nodiscard]] inline nlohmann::json chartMarkersToJson(const std::vector<ChartMarker>& marks) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : marks) {
    nlohmann::json jm = {{"kind", m.kind}, {"x0", m.x0}, {"x1", m.x1},
                         {"y0", m.y0},     {"y1", m.y1}, {"has_value", m.has_value}};
    if (!m.color.empty()) {
      jm["color"] = m.color;
    }
    if (!m.label.empty()) {
      jm["label"] = m.label;
    }
    arr.push_back(std::move(jm));
  }
  return arr;
}

[[nodiscard]] inline std::vector<ChartMarker> chartMarkersFromJson(const nlohmann::json& arr) {
  std::vector<ChartMarker> marks;
  if (!arr.is_array()) {
    return marks;
  }
  marks.reserve(arr.size());
  for (const auto& jm : arr) {
    ChartMarker m;
    m.kind = jm.value("kind", std::string());
    m.x0 = jm.value("x0", 0.0);
    m.x1 = jm.value("x1", 0.0);
    m.y0 = jm.value("y0", 0.0);
    m.y1 = jm.value("y1", 0.0);
    m.has_value = jm.value("has_value", false);
    m.color = jm.value("color", std::string());
    m.label = jm.value("label", std::string());
    marks.push_back(std::move(m));
  }
  return marks;
}

/// One mark on a MarkerTimeline. `region` true → a resizable [start,end] span;
/// false → a single-point event at `start` (end ignored). Positions are in the
/// timeline's integer step domain (see setMarkerTimelineBounds). `id` is a stable
/// per-mark handle the host echoes back on edits.
struct TimelineMark {
  int id = 0;
  bool region = true;
  int start = 0;
  int end = 0;
};

/// The single source of truth for the TimelineMark ⇆ JSON wire shape, shared by
/// the data setter, the event builder, the event parser, and the data-view (so a
/// field change cannot drift between the four).
[[nodiscard]] inline nlohmann::json timelineMarksToJson(const std::vector<TimelineMark>& marks) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : marks) {
    arr.push_back({{"id", m.id}, {"region", m.region}, {"start", m.start}, {"end", m.end}});
  }
  return arr;
}

[[nodiscard]] inline std::vector<TimelineMark> timelineMarksFromJson(const nlohmann::json& arr) {
  std::vector<TimelineMark> marks;
  if (!arr.is_array()) {
    return marks;
  }
  marks.reserve(arr.size());
  for (const auto& jm : arr) {
    TimelineMark m;
    m.id = jm.value("id", 0);
    m.region = jm.value("region", true);
    m.start = jm.value("start", 0);
    m.end = jm.value("end", 0);
    marks.push_back(m);
  }
  return marks;
}

/// One boundary segment on a RangeSlider (used by setRangeSliderMarkers): a box
/// covering [start, end] in slider units, with an optional label. The host draws
/// each as a distinct box at its true extent — so disjoint selections leave
/// blank slider space in the gaps — and shades the boxes overlapping the current
/// [lower, upper] selection.
struct RangeSliderMarker {
  int start = 0;
  int end = 0;
  std::string label;
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

  // Set foreground colors for QListWidget items, parallel to the items vector
  // set by setListItems. Each entry is a CSS color string (e.g. "#ff0000") or
  // empty to use the default palette color. Size must match the items vector.
  WidgetData& setListItemColors(std::string_view name, const std::vector<std::string>& colors) {
    entry(name)["list_item_colors"] = colors;
    return *this;
  }

  /// Render a QListWidget with a per-row delete (trash) button pinned to the
  /// trailing edge of every item. Clicking a row's button fires
  /// onItemDeleteRequested(name, row). Opt-in and off by default; re-send on
  /// every build (it is a per-widget flag, like the radio column).
  WidgetData& setListItemsDeletable(std::string_view name, bool deletable) {
    entry(name)["list_deletable"] = deletable;
    return *this;
  }

  /// Centered empty-state hint drawn over a QListWidget while it holds no items;
  /// the host hides it the moment items appear and restores it when the list is
  /// empty again. Mirrors setChartPlaceholder for lists.
  WidgetData& setListPlaceholder(std::string_view name, std::string_view text) {
    entry(name)["list_placeholder"] = std::string(text);
    return *this;
  }

  // --- QTableWidget ---
  WidgetData& setTableHeaders(std::string_view name, const std::vector<std::string>& headers) {
    entry(name)["headers"] = headers;
    return *this;
  }

  WidgetData& setTableRows(std::string_view name, const std::vector<std::vector<std::string>>& rows) {
    auto& e = entry(name);
    e["rows"] = rows;
    // Plain rows carry no sort keys: drop any keys a previous TableItem delivery
    // left behind, or — when the row count happens to match — the host would pair
    // these rows with the STALE keys and silently sort them wrong.
    e.erase("column_values");
    return *this;
  }

  /// Rows that carry a sort key per cell (see TableItem). Emits the display text as
  /// the usual `rows`, plus the keys as a sparse per-column map — so a host that
  /// predates this overload still reads `rows` and behaves exactly as before.
  /// Columns where no cell has a value are omitted entirely and sort by text.
  WidgetData& setTableRows(std::string_view name, const std::vector<std::vector<TableItem>>& rows) {
    auto& e = entry(name);

    nlohmann::json text_rows = nlohmann::json::array();
    std::size_t max_cols = 0;
    for (const auto& row : rows) {
      nlohmann::json cells = nlohmann::json::array();
      for (const auto& item : row) {
        cells.push_back(item.text);
      }
      text_rows.push_back(std::move(cells));
      if (row.size() > max_cols) {
        max_cols = row.size();
      }
    }
    e["rows"] = std::move(text_rows);

    nlohmann::json columns = nlohmann::json::object();
    for (std::size_t c = 0; c < max_cols; ++c) {
      bool any_value = false;
      for (const auto& row : rows) {
        if (c < row.size() && row[c].value.has_value()) {
          any_value = true;
          break;
        }
      }
      if (!any_value) {
        continue;
      }
      nlohmann::json values = nlohmann::json::array();
      for (const auto& row : rows) {
        if (c < row.size() && row[c].value.has_value()) {
          // Push the native alternative: nlohmann stores int64/uint64 natively, so a
          // large count or a nanosecond timestamp survives the round-trip exactly.
          std::visit([&values](auto v) { values.push_back(v); }, *row[c].value);
        } else {
          values.push_back(nlohmann::json::value_t::null);
        }
      }
      columns[std::to_string(c)] = std::move(values);
    }
    if (columns.empty()) {
      e.erase("column_values");
    } else {
      e["column_values"] = std::move(columns);
    }
    return *this;
  }

  /// Draw the sort arrow on `column` WITHOUT enabling Qt's built-in sorting.
  /// For tables that sort themselves via onHeaderClicked: Qt paints the indicator
  /// only when its own sorting is on, so a plugin-sorted table shows no arrow
  /// unless it declares one here. Re-send whenever the sort state changes.
  WidgetData& setTableSortIndicator(std::string_view name, int column, bool ascending) {
    auto& e = entry(name);
    e["sort_indicator"] = {{"col", column}, {"asc", ascending}};
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

  /// Render column `column` of a QTableWidget as an exclusive radio-button group:
  /// one QRadioButton per row, only one on at a time. `checked_row` is the row
  /// whose radio is selected (-1 for none). The cells in `column` carry the radio
  /// instead of text. Clicking a radio fires onTableRadioSelected(name, row).
  /// Re-send on every build to keep the checked row in sync.
  WidgetData& setTableRadioColumn(std::string_view name, int column, int checked_row) {
    auto& e = entry(name);
    e["radio_column"] = column;
    e["radio_checked_row"] = checked_row;
    return *this;
  }

  // --- Chart (QFrame used as chart container) ---

  /// Set chart series data on a QFrame widget. The host creates or updates a Qwt
  /// plot inside the frame, drawing one line curve per entry.
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
      if (s.dashed) {
        entry["dashed"] = true;
      }
      arr.push_back(std::move(entry));
    }
    e["chart_series"] = std::move(arr);
    return *this;
  }

  /// Overlay markers on the chart inside the named QFrame (drawn on top of the
  /// series). Pass an empty vector to clear just the markers. See ChartMarker.
  WidgetData& setChartMarkers(std::string_view name, const std::vector<ChartMarker>& markers) {
    entry(name)["chart_markers"] = chartMarkersToJson(markers);
    return *this;
  }

  /// Remove all series AND markers from the chart inside the named QFrame.
  WidgetData& clearChart(std::string_view name) {
    auto& e = entry(name);
    e["chart_series"] = nlohmann::json::array();
    e["chart_markers"] = nlohmann::json::array();
    return *this;
  }

  /// Enable interactive zoom (rubber band + mouse wheel) on the chart inside the named QFrame.
  /// When enabled, onChartViewChanged() is called whenever the user zooms or pans.
  WidgetData& setChartZoomEnabled(std::string_view name, bool enabled = true) {
    entry(name)["chart_zoom_enabled"] = enabled;
    return *this;
  }

  /// Auto-fit (zoom-to-extents) the chart inside the named QFrame on every series
  /// update when `enabled` is true; when false, preserve the user's current zoom.
  /// Mirrors the Transform/Filter editor "AutoZoom" checkbox.
  WidgetData& setChartAutoZoom(std::string_view name, bool enabled) {
    entry(name)["chart_auto_zoom"] = enabled;
    return *this;
  }

  /// Placeholder text shown as a floating overlay banner centered on the named
  /// chart QFrame while it has no series (e.g. a drag-and-drop hint). The host
  /// hides it automatically once the chart receives data.
  WidgetData& setChartPlaceholder(std::string_view name, std::string_view text) {
    entry(name)["chart_placeholder"] = std::string(text);
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

  /// Move the caret of a code editor to `cursor` (byte offset). Used after the
  /// plugin programmatically rewrites the code (e.g. inserting a completion) so
  /// the caret lands where the user expects rather than jumping to the start.
  WidgetData& setCodeCursor(std::string_view name, int cursor) {
    entry(name)["code_cursor"] = cursor;
    return *this;
  }

  /// Opt this code editor into caret tracking. When enabled, the host reports
  /// the caret offset on cursor moves as well as text edits (via
  /// onCodeChangedWithCursor), so the plugin can drive caret-aware completion.
  /// Editors that don't opt in only fire on text changes — the default — so an
  /// editor that merely validates code isn't re-run on every cursor move.
  WidgetData& setCodeCaretTracking(std::string_view name, bool enabled = true) {
    entry(name)["code_caret_tracking"] = enabled;
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

  /// Set a QPushButton icon by name, resolved by the host from its own themed
  /// icon set (so plugins reuse the app's shared glyphs without shipping SVG
  /// bytes, and get consistent tinting). Unknown ids fall back to no icon.
  /// Prefer this over setButtonIcon for standard app glyphs; use setButtonIcon
  /// for custom/one-off SVGs the host won't know about.
  WidgetData& setButtonIconNamed(std::string_view name, std::string_view icon_id) {
    entry(name)["button_icon_name"] = icon_id;
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

  /// Turn a QPushButton into a native "Save As" file picker. Unlike setFilePicker
  /// (which opens an existing file), this lets the user navigate to a directory AND
  /// type a new filename, so a not-yet-existing rule/config file can be created. The
  /// chosen path is reported through the same onFileSelected(name, path) event — the
  /// plugin distinguishes save from open by the widget name. `default_suffix` is
  /// appended when the typed name carries no extension.
  WidgetData& setSaveFilePicker(
      std::string_view name, std::string_view button_text, std::string_view filter, std::string_view title,
      std::string_view default_suffix = "") {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "save_file_picker";
    e["filter"] = filter;
    e["title"] = title;
    e["default_suffix"] = default_suffix;
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

  // --- MarkerTimeline (editable multi-marker strip) ---

  /// Set the integer [min, max] step domain of a MarkerTimeline (mark positions
  /// live in these units). Set before the marks, like the RangeSlider bounds.
  WidgetData& setMarkerTimelineBounds(std::string_view name, int min, int max) {
    auto& e = entry(name);
    e["marker_timeline_min"] = min;
    e["marker_timeline_max"] = max;
    return *this;
  }

  /// Replace the whole mark set shown on a MarkerTimeline (last-writer-wins,
  /// mirroring the producer's republish model). An empty vector clears it.
  WidgetData& setMarkerTimelineMarks(std::string_view name, const std::vector<TimelineMark>& marks) {
    entry(name)["marker_timeline_marks"] = timelineMarksToJson(marks);
    return *this;
  }

  /// Map the MarkerTimeline's [min,max] step domain onto the absolute time window
  /// [min_ns, max_ns] for hover/tooltip labels. Nanoseconds are carried as strings
  /// to avoid double precision loss. Pass min_ns == max_ns == 0 to show raw steps.
  WidgetData& setMarkerTimelineTimeSpan(std::string_view name, std::int64_t min_ns, std::int64_t max_ns) {
    auto& e = entry(name);
    e["marker_timeline_time_min_ns"] = std::to_string(min_ns);
    e["marker_timeline_time_max_ns"] = std::to_string(max_ns);
    return *this;
  }

  /// Draw boundary segments on a RangeSlider: one box per marker covering its
  /// [start, end] (in slider units, same space as the handles) with an optional
  /// label centered inside. Each box is drawn at its TRUE extent, so a disjoint
  /// selection leaves blank slider space between boxes; the host shades the
  /// boxes overlapping the current [lower, upper] selection, so the slider
  /// doubles as a "which segment falls in the range" indicator. Empty clears.
  WidgetData& setRangeSliderMarkers(std::string_view name, const std::vector<RangeSliderMarker>& markers) {
    auto& e = entry(name);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : markers) {
      arr.push_back(nlohmann::json{{"start", m.start}, {"end", m.end}, {"label", m.label}});
    }
    e["range_markers"] = std::move(arr);
    return *this;
  }

  // --- Field validity indicator (generic) ---

  /// Mark a field's value valid/invalid for a small inline indicator (e.g. a
  /// tick next to a line edit). The plugin owns the validation rule; the host
  /// only renders the result. The optional tooltip explains an invalid value.
  WidgetData& setFieldValid(std::string_view name, bool ok, std::string_view tooltip = {}) {
    auto& e = entry(name);
    e["valid"] = ok;
    e["valid_tooltip"] = std::string(tooltip);
    return *this;
  }

  // --- DateRangePicker (date/time range picker) ---

  /// Set placeholder hints for a DateRangePicker's empty "from"/"to" fields to
  /// the dataset's available span (ISO-8601 dates, e.g. "2016-04-29"). Like
  /// Qt's QLineEdit::setPlaceholderText this is a soft hint, not a selectable-
  /// range constraint; an empty string on either side clears that hint.
  WidgetData& setDateRangePlaceholder(
      std::string_view name, std::string_view earliest_iso, std::string_view latest_iso) {
    auto& e = entry(name);
    e["date_range_earliest"] = std::string(earliest_iso);
    e["date_range_latest"] = std::string(latest_iso);
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

  /// Request that the host open an INTERACTIVE sub-panel with the given UI XML.
  /// Unlike requestSubDialog (a one-shot modal that only harvests inputs on OK),
  /// the sub-panel is a live, non-blocking child: its widget events flow back to
  /// the plugin through the normal handlers (onTextChanged / onSelectionChanged /
  /// onItemDoubleClicked / onClicked, keyed by the sub-panel widgets' objectNames),
  /// and the plugin keeps pushing WidgetData to it every tick (so previews/lists
  /// update live). The host emits a synthetic onClicked("subPanelClosed") when the
  /// user dismisses it. Send closeSubPanel() to dismiss it programmatically.
  /// Only one sub-panel is open at a time; re-requesting while one is open is ignored.
  WidgetData& requestSubPanel(std::string_view ui_xml) {
    data_["__request_sub_panel"] = nlohmann::json{{"ui", ui_xml}};
    return *this;
  }

  /// Request that the host close the interactive sub-panel opened by requestSubPanel
  /// (e.g. after the user picked an item). No-op if none is open.
  WidgetData& closeSubPanel() {
    data_["__request_sub_panel_close"] = true;
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
