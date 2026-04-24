#pragma once

#include <pj_plugins/dialog_protocol.h>

#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// C++ base class for Dialog plugins (protocol v4).
///
/// Plugin authors subclass this and override the virtual methods. String
/// lifetime is managed by internal buffers. Trampolines catch exceptions
/// to prevent UB at the C ABI; caught exceptions populate the `PJ_error_t*`
/// out-parameter on fallible calls. All trampolines are `noexcept` at the
/// ABI boundary (v4 requirement) — the try/catch sits inside, so a throw
/// from user code is translated to an error return, never propagated.
class DialogPluginBase {
 public:
  virtual ~DialogPluginBase() = default;

  virtual std::string manifest() const = 0;
  virtual std::string ui_content() const = 0;
  virtual std::string widget_data() = 0;

  virtual bool onWidgetEvent(std::string_view widget_name, std::string_view event_json) = 0;

  virtual bool onTick() {
    return false;
  }

  virtual void onAccepted(std::string_view final_state_json) {
    (void)final_state_json;
  }

  virtual void onRejected() {}

  virtual std::string saveConfig() const {
    return "{}";
  }

  virtual bool loadConfig(std::string_view config_json) {
    (void)config_json;
    return false;
  }

  template <typename CreateFn>
  static const PJ_dialog_vtable_t* vtableWithCreate(CreateFn create_fn) {
    static const PJ_dialog_vtable_t vt = {
        PJ_DIALOG_PROTOCOL_VERSION, sizeof(PJ_dialog_vtable_t), create_fn,
        trampoline_destroy,         trampoline_get_manifest,    trampoline_get_ui_content,
        trampoline_get_widget_data, trampoline_on_widget_event, trampoline_on_tick,
        trampoline_on_accepted,     trampoline_on_rejected,     trampoline_save_config,
        trampoline_load_config,
    };
    return &vt;
  }

 private:
  std::string manifest_buf_;
  std::string ui_content_buf_;
  std::string widget_data_buf_;
  std::string config_buf_;

  bool manifest_cached_ = false;
  bool ui_content_cached_ = false;

  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    if (out_error == nullptr) {
      return;
    }
    out_error->code = code;
    auto writeField = [](char* dest, std::size_t dest_size, std::string_view src) {
      if (dest == nullptr || dest_size == 0) {
        return;
      }
      std::size_t n = src.size() < dest_size - 1 ? src.size() : dest_size - 1;
      std::memcpy(dest, src.data(), n);
      dest[n] = '\0';
    };
    writeField(out_error->domain, sizeof(out_error->domain), domain);
    writeField(out_error->message, sizeof(out_error->message), message);
    // Clear the v3.1 growth-path slots so a reused error struct does not
    // carry a stale pointer from a previous call. Matches sdk::fillError.
    out_error->extended = nullptr;
    out_error->extended_kind[0] = '\0';
  }

  static void trampoline_destroy(void* ctx) noexcept {
    try {
      delete static_cast<DialogPluginBase*>(ctx);
    } catch (...) {}
  }

  static const char* trampoline_get_manifest(void* ctx) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      if (!self->manifest_cached_) {
        self->manifest_buf_ = self->manifest();
        self->manifest_cached_ = true;
      }
      return self->manifest_buf_.c_str();
    } catch (...) {
      return "{}";
    }
  }

  static const char* trampoline_get_ui_content(void* ctx) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      if (!self->ui_content_cached_) {
        self->ui_content_buf_ = self->ui_content();
        self->ui_content_cached_ = true;
      }
      return self->ui_content_buf_.c_str();
    } catch (...) {
      return "";
    }
  }

  static const char* trampoline_get_widget_data(void* ctx) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->widget_data_buf_ = self->widget_data();
      return self->widget_data_buf_.c_str();
    } catch (...) {
      return "{}";
    }
  }

  static bool trampoline_on_widget_event(
      void* ctx, const char* widget_name, const char* event_json, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      return self->onWidgetEvent(
          widget_name == nullptr ? std::string_view{} : std::string_view(widget_name),
          event_json == nullptr ? std::string_view{} : std::string_view(event_json));
    } catch (const std::exception& e) {
      self->storeError(out_error, 1, "dialog", std::string("on_widget_event threw: ") + e.what());
      return false;
    } catch (...) {
      self->storeError(out_error, 1, "dialog", "unknown exception in on_widget_event");
      return false;
    }
  }

  static bool trampoline_on_tick(void* ctx, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      return self->onTick();
    } catch (const std::exception& e) {
      self->storeError(out_error, 1, "dialog", std::string("on_tick threw: ") + e.what());
      return false;
    } catch (...) {
      self->storeError(out_error, 1, "dialog", "unknown exception in on_tick");
      return false;
    }
  }

  static void trampoline_on_accepted(void* ctx, const char* final_state_json) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->onAccepted(final_state_json == nullptr ? std::string_view{} : std::string_view(final_state_json));
    } catch (...) {}
  }

  static void trampoline_on_rejected(void* ctx) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      self->onRejected();
    } catch (...) {}
  }

  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    if (out_json == nullptr) {
      self->storeError(out_error, 2, "dialog", "save_config called with null out_json");
      return false;
    }
    try {
      self->config_buf_ = self->saveConfig();
      out_json->data = self->config_buf_.data();
      out_json->size = self->config_buf_.size();
      return true;
    } catch (const std::exception& e) {
      self->storeError(out_error, 1, "dialog", std::string("save_config threw: ") + e.what());
      return false;
    } catch (...) {
      self->storeError(out_error, 1, "dialog", "unknown exception in save_config");
      return false;
    }
  }

  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<DialogPluginBase*>(ctx);
    try {
      std::string_view sv =
          config_json.data == nullptr ? std::string_view{} : std::string_view(config_json.data, config_json.size);
      return self->loadConfig(sv);
    } catch (const std::exception& e) {
      self->storeError(out_error, 1, "dialog", std::string("load_config threw: ") + e.what());
      return false;
    } catch (...) {
      self->storeError(out_error, 1, "dialog", "unknown exception in load_config");
      return false;
    }
  }
};

/// Per-dialog-type vtable accessor. Specialised by `PJ_DIALOG_PLUGIN`.
/// Plugin authors don't call this directly; they call `borrowDialog(member)`
/// from their host's `getDialog()` override, and the compiler picks the
/// right specialisation from the dialog member's static type.
template <class DialogT>
const PJ_dialog_vtable_t* dialogVtableFor() noexcept;

/// Build a `PJ_borrowed_dialog_t` fat pointer from an embedded dialog
/// member. This is what hosts with embedded dialogs should return from
/// their `getDialog()` override — no `extern "C"` forward declaration
/// required in the plugin source.
///
///   class MySource : public PJ::FileSourceBase {
///     PJ_borrowed_dialog_t getDialog() override {
///       return PJ::borrowDialog(dialog_);
///     }
///    private:
///     MyDialog dialog_;
///   };
template <class DialogT>
PJ_borrowed_dialog_t borrowDialog(DialogT& dialog) noexcept {
  return PJ_borrowed_dialog_t{&dialog, dialogVtableFor<DialogT>()};
}

}  // namespace PJ

/// Macro to export the vtable entry point for a plugin class.
///
/// Emits two things:
///   1. The `PJ_get_dialog_vtable()` C symbol the host loader resolves
///      via `dlsym`. Always present, same shape since v1.
///   2. A specialisation of `PJ::dialogVtableFor<ClassName>()` that lets
///      other plugin code (notably a host's `getDialog()` override) obtain
///      the vtable pointer type-safely via `PJ::borrowDialog(member)` —
///      no `extern "C"` forward declaration required in the plugin source.
#define PJ_DIALOG_PLUGIN(ClassName)                                                                       \
  extern "C" PJ_DIALOG_EXPORT const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept {                 \
    static const PJ_dialog_vtable_t* vt = PJ::DialogPluginBase::vtableWithCreate([]() noexcept -> void* { \
      try {                                                                                               \
        return new ClassName();                                                                           \
      } catch (...) {                                                                                     \
        return nullptr;                                                                                   \
      }                                                                                                   \
    });                                                                                                   \
    return vt;                                                                                            \
  }                                                                                                       \
  namespace PJ {                                                                                          \
  template <>                                                                                             \
  inline const PJ_dialog_vtable_t* dialogVtableFor<ClassName>() noexcept {                                \
    return PJ_get_dialog_vtable();                                                                        \
  }                                                                                                       \
  }
