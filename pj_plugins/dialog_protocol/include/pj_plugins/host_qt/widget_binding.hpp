#pragma once

#include <QWidget>
#include <functional>
#include <pj_plugins/host/widget_data_view.hpp>
#include <string>

namespace PJ {

/// Callback for widget events: receives widget objectName + event JSON string.
using WidgetEventCallback = std::function<void(const std::string& widget_name, const std::string& event_json)>;

/// Apply widget data from a WidgetDataView to all matching child widgets of root.
/// Uses QSignalBlocker to prevent re-entrant signal firing during updates.
void applyWidgetData(QWidget* root, const PJ::WidgetDataView& view);

/// Connect primary change signals of all editable widgets under root
/// to the given callback. The callback receives the widget objectName and
/// an event JSON string built by WidgetEventBuilder.
void connectWidgetSignals(QWidget* root, WidgetEventCallback callback);

/// Create QShortcut objects for QPushButtons that declare a "shortcut" key
/// in the widget data. Each shortcut triggers click() on the target button.
/// Call once after the dialog is fully constructed and signals are connected.
void installButtonShortcuts(QWidget* root, const PJ::WidgetDataView& view);

}  // namespace PJ
