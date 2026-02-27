#pragma once

#include <pj/dialog_protocol.h>

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace pj::host {

/// RAII wrapper around a plugin vtable + context.
/// Owns the context created by vtable->create() and destroys it in the destructor.
/// All string-returning methods copy from the plugin's internal buffer — safe to hold.
class DialogHandle {
 public:
  explicit DialogHandle(const pj_dialog_vtable_t* vt) : vt_(vt) {
    if (vt_) {
      assert(vt_->protocol_version == PJ_DIALOG_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~DialogHandle() {
    if (vt_ && ctx_) {
      vt_->destroy(ctx_);
    }
  }

  // Move-only
  DialogHandle(DialogHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  DialogHandle& operator=(DialogHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
    }
    return *this;
  }

  DialogHandle(const DialogHandle&) = delete;
  DialogHandle& operator=(const DialogHandle&) = delete;

  // --- Queries — return copied strings ---

  [[nodiscard]] std::string manifest() const { return safe_string(vt_->get_manifest(ctx_)); }

  [[nodiscard]] std::string ui_content() const { return safe_string(vt_->get_ui_content(ctx_)); }

  [[nodiscard]] std::string widget_data() const { return safe_string(vt_->get_widget_data(ctx_)); }

  // --- Events — return true if host should re-read widget_data() ---

  [[nodiscard]] bool send_event(std::string_view widget_name, std::string_view event_json) {
    return vt_->on_widget_event(ctx_, std::string(widget_name).c_str(),
                                std::string(event_json).c_str());
  }

  [[nodiscard]] bool tick() { return vt_->on_tick(ctx_); }

  // --- Dialog result ---

  void accept(std::string_view final_state_json) {
    vt_->on_accepted(ctx_, std::string(final_state_json).c_str());
  }

  void reject() { vt_->on_rejected(ctx_); }

  // --- Config persistence ---

  [[nodiscard]] std::string save_config() const { return safe_string(vt_->save_config(ctx_)); }

  [[nodiscard]] bool load_config(std::string_view config_json) {
    return vt_->load_config(ctx_, std::string(config_json).c_str());
  }

  // --- Error — returns "" if no error ---

  [[nodiscard]] std::string last_error() const {
    return safe_string(vt_->get_last_error(ctx_));
  }

  // --- Escape hatch ---

  [[nodiscard]] const pj_dialog_vtable_t* vtable() const { return vt_; }
  [[nodiscard]] void* context() const { return ctx_; }

 private:
  const pj_dialog_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;

  static std::string safe_string(const char* s) { return s ? std::string(s) : std::string(); }
};

}  // namespace pj::host
