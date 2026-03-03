#pragma once

#include <PJ/host/widget_data_view.hpp>

#include <QWidget>

#include <functional>
#include <string>

namespace PJ::host_qt {

/// Callback for widget events: receives widget objectName + event JSON string.
using WidgetEventCallback = std::function<void(const std::string& widget_name,
                                               const std::string& event_json)>;

/// Apply widget data from a WidgetDataView to all matching child widgets of root.
/// Uses QSignalBlocker to prevent re-entrant signal firing during updates.
void apply_widget_data(QWidget* root, const PJ::host::WidgetDataView& view);

/// Connect primary change signals of all editable widgets under root
/// to the given callback. The callback receives the widget objectName and
/// an event JSON string built by WidgetEventBuilder.
void connect_widget_signals(QWidget* root, WidgetEventCallback callback);

}  // namespace PJ::host_qt
