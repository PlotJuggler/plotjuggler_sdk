#pragma once

#include <PJ/dialog_protocol.h>

#include <exception>
#include <string>
#include <string_view>

namespace PJ::sdk {

/// C++ base class that implements the C vtable trampolines.
/// Plugin authors subclass this and override the virtual methods.
/// String lifetime is managed by internal buffers — callers don't need to worry about it.
///
/// All trampolines catch C++ exceptions to prevent undefined behavior at the C ABI boundary.
/// Caught exceptions are stored and retrievable via get_last_error().
class DialogPluginBase {
 public:
  virtual ~DialogPluginBase() = default;

  // --- Override these in your plugin ---

  /// Return a JSON manifest describing the plugin (name, version, widget mapping, etc.)
  virtual std::string manifest() const = 0;

  /// Return the Qt Designer .ui XML content
  virtual std::string ui_content() const = 0;

  /// Return a JSON object mapping widget objectNames to their current property values
  virtual std::string widget_data() = 0;

  /// Called when a widget fires an event. Return true if widget_data changed.
  virtual bool on_widget_event(std::string_view widget_name, std::string_view event_json) = 0;

  /// Called periodically. Return true if widget_data changed.
  virtual bool on_tick() { return false; }

  /// Called when the user clicks OK. final_state_json contains the dialog's final widget state.
  virtual void on_accepted(std::string_view final_state_json) { (void)final_state_json; }

  /// Called when the user clicks Cancel.
  virtual void on_rejected() {}

  /// Return a JSON string capturing plugin config for persistence.
  virtual std::string save_config() const { return "{}"; }

  /// Restore plugin state from a previously saved config. Return true if widget_data changed.
  virtual bool load_config(std::string_view config_json) {
    (void)config_json;
    return false;
  }

  /// Return an error message, or "" if no error.
  virtual std::string last_error() const { return ""; }

  /// Returns a vtable with the create function set to `create_fn`.
  /// Used by PJ_DIALOG_PLUGIN to wire up the concrete type.
  template <typename CreateFn>
  static const PJ_dialog_vtable_t* vtable_with_create(CreateFn create_fn) {
    static const PJ_dialog_vtable_t vt = {
        PJ_DIALOG_PROTOCOL_VERSION,
        sizeof(PJ_dialog_vtable_t),
        create_fn,
        trampoline_destroy,
        trampoline_get_manifest,
        trampoline_get_ui_content,
        trampoline_get_widget_data,
        trampoline_on_widget_event,
        trampoline_on_tick,
        trampoline_on_accepted,
        trampoline_on_rejected,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_get_last_error,
    };
    return &vt;
  }

 private:
  // String buffers for lifetime management across the C ABI.
  std::string manifest_buf_;
  std::string ui_content_buf_;
  std::string widget_data_buf_;
  std::string config_buf_;
  std::string error_buf_;
  bool manifest_cached_ = false;
  bool ui_content_cached_ = false;

  // --- Trampolines: every one catches exceptions to prevent UB at the C boundary ---

  static void trampoline_destroy(void* ctx) {
    // destroy must not throw — and delete of a virtual dtor should not either,
    // but we guard defensively.
    try {
      delete static_cast<DialogPluginBase*>(ctx);
    } catch (...) {
    }
  }

  static const char* trampoline_get_manifest(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      if (!self->manifest_cached_) {
        self->manifest_buf_ = self->manifest();
        self->manifest_cached_ = true;
      }
      return self->manifest_buf_.c_str();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return "{}";
    } catch (...) {
      self->error_buf_ = "Unknown exception in get_manifest";
      return "{}";
    }
  }

  static const char* trampoline_get_ui_content(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      if (!self->ui_content_cached_) {
        self->ui_content_buf_ = self->ui_content();
        self->ui_content_cached_ = true;
      }
      return self->ui_content_buf_.c_str();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return "";
    } catch (...) {
      self->error_buf_ = "Unknown exception in get_ui_content";
      return "";
    }
  }

  static const char* trampoline_get_widget_data(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->widget_data_buf_ = self->widget_data();
      return self->widget_data_buf_.c_str();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return "{}";
    } catch (...) {
      self->error_buf_ = "Unknown exception in get_widget_data";
      return "{}";
    }
  }

  static bool trampoline_on_widget_event(void* ctx, const char* widget_name,
                                         const char* event_json) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      return self->on_widget_event(widget_name, event_json);
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return false;
    } catch (...) {
      self->error_buf_ = "Unknown exception in on_widget_event";
      return false;
    }
  }

  static bool trampoline_on_tick(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      return self->on_tick();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return false;
    } catch (...) {
      self->error_buf_ = "Unknown exception in on_tick";
      return false;
    }
  }

  static void trampoline_on_accepted(void* ctx, const char* final_state_json) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->on_accepted(final_state_json);
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
    } catch (...) {
      self->error_buf_ = "Unknown exception in on_accepted";
    }
  }

  static void trampoline_on_rejected(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->on_rejected();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
    } catch (...) {
      self->error_buf_ = "Unknown exception in on_rejected";
    }
  }

  static const char* trampoline_save_config(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->config_buf_ = self->save_config();
      return self->config_buf_.c_str();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return "{}";
    } catch (...) {
      self->error_buf_ = "Unknown exception in save_config";
      return "{}";
    }
  }

  static bool trampoline_load_config(void* ctx, const char* config_json) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      return self->load_config(config_json);
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
      return false;
    } catch (...) {
      self->error_buf_ = "Unknown exception in load_config";
      return false;
    }
  }

  static const char* trampoline_get_last_error(void* ctx) {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->error_buf_ = self->last_error();
    } catch (const std::exception& e) {
      self->error_buf_ = e.what();
    } catch (...) {
      self->error_buf_ = "Unknown exception in get_last_error";
    }
    return self->error_buf_.empty() ? nullptr : self->error_buf_.c_str();
  }
};

}  // namespace PJ::sdk

/// Macro to export the vtable entry point for a plugin class.
/// Usage: PJ_DIALOG_PLUGIN(MyPluginClass)
#define PJ_DIALOG_PLUGIN(ClassName)                                                     \
  extern "C" PJ_DIALOG_EXPORT const PJ_dialog_vtable_t* PJ_get_dialog_vtable() {       \
    static const PJ_dialog_vtable_t* vt =                                               \
        PJ::sdk::DialogPluginBase::vtable_with_create([]() -> void* {                   \
          return new ClassName();                                                        \
        });                                                                              \
    return vt;                                                                           \
  }
