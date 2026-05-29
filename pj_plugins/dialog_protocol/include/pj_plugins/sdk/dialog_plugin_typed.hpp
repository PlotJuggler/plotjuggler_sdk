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

  /// QTableWidget: a horizontal-header section (column) was clicked. Plugins
  /// that drive their own column sorting override this, re-order their row
  /// model, and re-emit — index-based selection/visibility stays consistent.
  virtual bool onHeaderClicked(std::string_view /*widget_name*/, int /*section*/) {
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

  /// DateRangePicker: the date/time range filter changed. from_iso/to_iso are
  /// ISO-8601 datetime strings (empty = unbounded on that side).
  virtual bool onDateRangeChanged(
      std::string_view /*widget_name*/, std::string_view /*from_iso*/, std::string_view /*to_iso*/) {
    return false;
  }

 private:
  /// Parses event_json and dispatches to the appropriate typed virtual above.
  bool onWidgetEvent(std::string_view widget_name, std::string_view event_json) final {
    WidgetEvent event(event_json);

    if (auto v = event.chartViewChanged()) {
      return onChartViewChanged(widget_name, v->x_min, v->x_max, v->y_min, v->y_max);
    }
    if (auto v = event.rangeChanged()) {
      return onRangeChanged(widget_name, v->lower, v->upper);
    }
    if (auto v = event.dateRangeChanged()) {
      return onDateRangeChanged(widget_name, v->from_iso, v->to_iso);
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
    if (auto v = event.headerSection()) {
      return onHeaderClicked(widget_name, *v);
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
