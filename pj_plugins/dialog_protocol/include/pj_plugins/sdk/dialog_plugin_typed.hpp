#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_plugins/sdk/dialog_plugin_base.hpp>
#include <pj_plugins/sdk/widget_event.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// Level 3: Typed event dispatch.
/// Extends DialogPluginBase by parsing event JSON and dispatching to typed virtual methods.
/// Plugin authors override only the typed methods they need (all default to returning false).
///
/// Connection-dialog building blocks live next door — do not reimplement them:
/// the parser-encoding selector and visible-selection merge in
/// pj_plugins/sdk/streaming_dialog.hpp (+ parseEncodingsJson in encoding_utils.hpp),
/// endpoint composition in pj_plugins/sdk/endpoint.hpp, and the strict port parse
/// in pj_base/sdk/text_utils.hpp.
class DialogPluginTyped : public DialogPluginBase {
 public:
  // --- Override these instead of onWidgetEvent() ---

  virtual bool onTextChanged(std::string_view /*widget_name*/, std::string_view /*text*/) {
    return false;
  }

  virtual bool onIndexChanged(std::string_view /*widget_name*/, int /*index*/) {
    return false;
  }

  virtual bool onToggled(std::string_view /*widget_name*/, bool /*checked*/) {
    return false;
  }

  virtual bool onValueChanged(std::string_view /*widget_name*/, int /*value*/) {
    return false;
  }

  virtual bool onValueChanged(std::string_view /*widget_name*/, double /*value*/) {
    return false;
  }

  virtual bool onSelectionChanged(std::string_view /*widget_name*/, const std::vector<std::string>& /*selected*/) {
    return false;
  }

  virtual bool onClicked(std::string_view /*widget_name*/) {
    return false;
  }

  virtual bool onFileSelected(std::string_view /*widget_name*/, std::string_view /*path*/) {
    return false;
  }

  virtual bool onFolderSelected(std::string_view /*widget_name*/, std::string_view /*path*/) {
    return false;
  }

  virtual bool onTabChanged(std::string_view /*widget_name*/, int /*index*/) {
    return false;
  }

  virtual bool onItemDoubleClicked(std::string_view /*widget_name*/, int /*index*/) {
    return false;
  }

  /// A row's trailing delete (trash) button was clicked in a QListWidget marked
  /// deletable via WidgetData::setListItemsDeletable. `index` is the row.
  virtual bool onItemDeleteRequested(std::string_view /*widget_name*/, int /*index*/) {
    return false;
  }

  /// QTableWidget: a horizontal-header section (column) was clicked. Plugins
  /// that drive their own column sorting override this, re-order their row
  /// model, and re-emit — index-based selection/visibility stays consistent.
  virtual bool onHeaderClicked(std::string_view /*widget_name*/, int /*section*/) {
    return false;
  }

  /// QTableWidget: a radio button in the table's radio column was selected
  /// (see WidgetData::setTableRadioColumn). `row` is the newly-checked row.
  virtual bool onTableRadioSelected(std::string_view /*widget_name*/, int /*row*/) {
    return false;
  }

  virtual bool onCodeChanged(std::string_view /*widget_name*/, std::string_view /*code*/) {
    return false;
  }

  /// Cursor-aware code change: `cursor` is the caret offset (bytes) in `code`,
  /// or negative when the host didn't report one. The dispatch always calls
  /// this; it defaults to onCodeChanged(name, code), so existing plugins keep
  /// working. Override this (instead of onCodeChanged) to drive caret-aware
  /// completion. A distinct name (rather than an overload) avoids the
  /// overloaded-virtual hiding hazard.
  ///
  /// The caret is only reported (and cursor-only moves only fire this at all)
  /// for editors that opted in via WidgetData::setCodeCaretTracking. Without
  /// opt-in this fires on text changes only, with cursor < 0 — so an editor
  /// that merely validates code is not re-run on every cursor move.
  virtual bool onCodeChangedWithCursor(std::string_view widget_name, std::string_view code, int /*cursor*/) {
    return onCodeChanged(widget_name, code);
  }

  virtual bool onItemsDropped(std::string_view /*widget_name*/, const std::vector<std::string>& /*items*/) {
    return false;
  }

  /// ChartPreviewWidget: zoom or pan changed the visible range.
  /// Only called when the plugin has declared setChartZoomEnabled for this widget.
  virtual bool onChartViewChanged(
      std::string_view /*widget_name*/, double /*x_min*/, double /*x_max*/, double /*y_min*/, double /*y_max*/) {
    return false;
  }

  /// RangeSlider: a handle (or the whole span) moved.
  virtual bool onRangeChanged(std::string_view /*widget_name*/, int /*lower*/, int /*upper*/) {
    return false;
  }

  /// MarkerTimeline: a mark was moved, resized, or deleted. `marks` is the full set.
  virtual bool onMarkerTimelineChanged(std::string_view /*widget_name*/, const std::vector<TimelineMark>& /*marks*/) {
    return false;
  }

  /// DateRangePicker: the date/time range filter changed. from_iso/to_iso are
  /// ISO-8601 datetime strings (empty = unbounded on that side).
  virtual bool onDateRangeChanged(
      std::string_view /*widget_name*/, std::string_view /*from_iso*/, std::string_view /*to_iso*/) {
    return false;
  }

  /// QDateTimeEdit (and its QDateEdit/QTimeEdit subclasses): the displayed
  /// datetime was edited. `iso8601` is wall-clock local time, always a full
  /// datetime; fractional seconds appear only for ms-precision editors.
  virtual bool onDateTimeChanged(std::string_view /*widget_name*/, std::string_view /*iso8601*/) {
    return false;
  }

  /// QStackedWidget: the current page changed. `page_object_name` is the
  /// stable identity; `index` describes the widget's current layout. Kept at
  /// the tail of the callback list so existing virtual slots retain their
  /// positions.
  /// @since 0.21.0
  virtual bool onStackedPageChanged(
      std::string_view /*widget_name*/, int /*index*/, std::string_view /*page_object_name*/) {
    return false;
  }

  /// QTreeWidget: complete logical selected-ID set after a user change. The
  /// set includes selected items hidden by the current visibility filter.
  /// Appended after onStackedPageChanged so every older virtual keeps its slot.
  /// @since 0.21.0
  virtual bool onTreeSelectionChanged(std::string_view /*widget_name*/, const std::vector<std::string>& /*ids*/) {
    return false;
  }

  /// QTreeWidget: stable item ID and activated column (keyboard or double-click).
  /// @since 0.21.0
  virtual bool onTreeItemActivated(std::string_view /*widget_name*/, std::string_view /*id*/, int /*column*/) {
    return false;
  }

  /// QTreeWidget: stable item ID and its new expanded state.
  /// @since 0.21.0
  virtual bool onTreeExpansionChanged(std::string_view /*widget_name*/, std::string_view /*id*/, bool /*expanded*/) {
    return false;
  }

  /// QTreeWidget: stable item ID and its new column-0 check state.
  /// @since 0.21.0
  virtual bool onTreeCheckStateChanged(
      std::string_view /*widget_name*/, std::string_view /*id*/, TreeCheckState /*state*/) {
    return false;
  }

  /// Structured picker outcome. The default bridge forwards the first path of
  /// a Selected result exactly once to onFileSelected() or onFolderSelected(),
  /// according to result.mode. Other statuses have no legacy callback and
  /// return false. Override this new handler to consume the structured result
  /// without a second legacy callback.
  /// Appended after every tree callback so existing virtual slots retain their
  /// positions.
  /// @since 0.21.0
  virtual bool onFilePickerResult(std::string_view widget_name, const FilePickerResult& result) {
    if (result.status != FilePickerStatus::Selected || result.paths.empty()) {
      return false;
    }
    if (result.mode == FilePickerMode::SelectDirectory) {
      return onFolderSelected(widget_name, result.paths.front());
    }
    return onFileSelected(widget_name, result.paths.front());
  }

 private:
  /// Parses event_json and dispatches to the appropriate typed virtual above.
  bool onWidgetEvent(std::string_view widget_name, std::string_view event_json) final {
    WidgetEvent event(event_json);

    // Normative dispatch ordering and fail-closed rules live in
    // docs/dialog-sdk-reference.md. The structured picker key claims the event
    // first; an optional legacy key must be mode-correct and path-consistent.
    if (event.has("file_picker_result")) {
      auto result = event.filePickerResult();
      if (!result.has_value()) {
        return false;
      }
      if (result->status == FilePickerStatus::Selected) {
        auto legacy = detail::filePickerLegacySelection(*result);
        if (!legacy.has_value()) {
          return false;
        }
        const bool has_file = event.has("file_selected");
        const bool has_folder = event.has("folder_selected");
        if (has_file && has_folder) {
          return false;
        }
        if (has_file || has_folder) {
          const bool expects_folder = legacy->key == "folder_selected";
          if (has_folder != expects_folder) {
            return false;
          }
          const auto compatibility_path = expects_folder ? event.folderSelected() : event.fileSelected();
          if (!compatibility_path.has_value() || *compatibility_path != legacy->path) {
            return false;
          }
        }
      } else if (event.has("file_selected") || event.has("folder_selected")) {
        return false;
      }
      return onFilePickerResult(widget_name, *result);
    }

    // After picker results, tree keys claim the payload before stacked and
    // legacy channels. See docs/dialog-sdk-reference.md for the normative
    // mixed-event and fail-closed contract.
    const bool has_tree_selection = event.has("tree_selection_changed");
    const bool has_tree_activation = event.has("tree_item_activated");
    const bool has_tree_expansion = event.has("tree_expansion_changed");
    const bool has_tree_check_state = event.has("tree_check_state_changed");
    const int tree_channel_count = static_cast<int>(has_tree_selection) + static_cast<int>(has_tree_activation) +
                                   static_cast<int>(has_tree_expansion) + static_cast<int>(has_tree_check_state);
    if (tree_channel_count != 0) {
      if (tree_channel_count != 1 || event.raw().size() != 1) {
        return false;
      }
      if (has_tree_selection) {
        auto ids = event.treeSelectionChanged();
        return ids.has_value() ? onTreeSelectionChanged(widget_name, *ids) : false;
      }
      if (has_tree_activation) {
        auto activation = event.treeItemActivated();
        return activation.has_value() ? onTreeItemActivated(widget_name, activation->id, activation->column) : false;
      }
      if (has_tree_expansion) {
        auto expansion = event.treeExpansionChanged();
        return expansion.has_value() ? onTreeExpansionChanged(widget_name, expansion->id, expansion->expanded) : false;
      }
      auto check_state = event.treeCheckStateChanged();
      return check_state.has_value() ? onTreeCheckStateChanged(widget_name, check_state->id, check_state->state)
                                     : false;
    }

    // Either stacked key claims the event after picker/tree and before legacy.
    // The normative validation and fallthrough rules are documented in
    // docs/dialog-sdk-reference.md.
    if (event.has("stacked_index") || event.has("stacked_page")) {
      const auto index = event.stackedIndex();
      const auto page = event.stackedPage();
      if (!index || *index < 0 || !page || page->empty()) {
        return false;
      }
      return onStackedPageChanged(widget_name, *index, *page);
    }

    if (auto v = event.chartViewChanged()) {
      return onChartViewChanged(widget_name, v->x_min, v->x_max, v->y_min, v->y_max);
    }
    if (auto v = event.rangeChanged()) {
      return onRangeChanged(widget_name, v->lower, v->upper);
    }
    if (auto v = event.markerTimelineChanged()) {
      return onMarkerTimelineChanged(widget_name, *v);
    }
    if (auto v = event.dateRangeChanged()) {
      return onDateRangeChanged(widget_name, v->from_iso, v->to_iso);
    }
    if (auto v = event.dateTimeChanged()) {
      return onDateTimeChanged(widget_name, *v);
    }
    if (auto v = event.itemsDropped()) {
      return onItemsDropped(widget_name, *v);
    }
    if (auto v = event.codeChanged()) {
      return onCodeChangedWithCursor(widget_name, *v, event.codeCursor().value_or(-1));
    }
    if (auto v = event.text()) {
      return onTextChanged(widget_name, *v);
    }
    if (auto v = event.currentIndex()) {
      return onIndexChanged(widget_name, *v);
    }
    if (auto v = event.checked()) {
      return onToggled(widget_name, *v);
    }
    if (auto v = event.fileSelected()) {
      return onFileSelected(widget_name, *v);
    }
    if (auto v = event.folderSelected()) {
      return onFolderSelected(widget_name, *v);
    }
    if (event.clicked()) {
      return onClicked(widget_name);
    }
    if (auto v = event.selectedItems()) {
      return onSelectionChanged(widget_name, *v);
    }
    if (auto v = event.tabIndex()) {
      return onTabChanged(widget_name, *v);
    }
    if (auto v = event.itemDoubleClickedIndex()) {
      return onItemDoubleClicked(widget_name, *v);
    }
    if (auto v = event.itemDeleteRequestedIndex()) {
      return onItemDeleteRequested(widget_name, *v);
    }
    if (auto v = event.headerSection()) {
      return onHeaderClicked(widget_name, *v);
    }
    if (auto v = event.tableRadioRow()) {
      return onTableRadioSelected(widget_name, *v);
    }
    // value: try int first, then double
    if (auto v = event.valueInt()) {
      return onValueChanged(widget_name, *v);
    }
    if (auto v = event.valueDouble()) {
      return onValueChanged(widget_name, *v);
    }

    return false;
  }
};

}  // namespace PJ
