#pragma once

#include <PJ/sdk/dialog_plugin_base.hpp>
#include <PJ/sdk/widget_event.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace PJ::sdk {

/// Level 3: Typed event dispatch.
/// Extends DialogPluginBase by parsing event JSON and dispatching to typed virtual methods.
/// Plugin authors override only the typed methods they need (all default to returning false).
class DialogPluginTyped : public DialogPluginBase {
 public:
  // --- Override these instead of on_widget_event() ---

  virtual bool on_text_changed(std::string_view /*widget_name*/, std::string_view /*text*/) {
    return false;
  }

  virtual bool on_index_changed(std::string_view /*widget_name*/, int /*index*/) { return false; }

  virtual bool on_toggled(std::string_view /*widget_name*/, bool /*checked*/) { return false; }

  virtual bool on_value_changed(std::string_view /*widget_name*/, int /*value*/) { return false; }

  virtual bool on_value_changed(std::string_view /*widget_name*/, double /*value*/) {
    return false;
  }

  virtual bool on_selection_changed(std::string_view /*widget_name*/,
                                    const std::vector<std::string>& /*selected*/) {
    return false;
  }

  virtual bool on_clicked(std::string_view /*widget_name*/) { return false; }

  virtual bool on_file_selected(std::string_view /*widget_name*/, std::string_view /*path*/) {
    return false;
  }

  virtual bool on_tab_changed(std::string_view /*widget_name*/, int /*index*/) { return false; }

 private:
  /// Parses event_json and dispatches to the appropriate typed virtual above.
  bool on_widget_event(std::string_view widget_name, std::string_view event_json) override final {
    WidgetEvent event(event_json);

    if (auto v = event.text()) {
      return on_text_changed(widget_name, *v);
    }
    if (auto v = event.current_index()) {
      return on_index_changed(widget_name, *v);
    }
    if (auto v = event.checked()) {
      return on_toggled(widget_name, *v);
    }
    if (auto v = event.file_selected()) {
      return on_file_selected(widget_name, *v);
    }
    if (event.clicked()) {
      return on_clicked(widget_name);
    }
    if (auto v = event.selected_items()) {
      return on_selection_changed(widget_name, *v);
    }
    if (auto v = event.tab_index()) {
      return on_tab_changed(widget_name, *v);
    }
    // value: try int first, then double
    if (auto v = event.value_int()) {
      return on_value_changed(widget_name, *v);
    }
    if (auto v = event.value_double()) {
      return on_value_changed(widget_name, *v);
    }

    return false;
  }
};

}  // namespace PJ::sdk
