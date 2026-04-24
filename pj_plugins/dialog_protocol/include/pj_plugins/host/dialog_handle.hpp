#pragma once

#include <pj_plugins/dialog_protocol.h>

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// RAII wrapper around a plugin vtable + context (protocol v4).
class DialogHandle {
 public:
  explicit DialogHandle(const PJ_dialog_vtable_t* vt) : vt_(vt) {
    if (vt_) {
      assert(vt_->protocol_version == PJ_DIALOG_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  /// Non-owning handle from an externally managed context (e.g. a plugin's embedded dialog).
  static DialogHandle borrowed(const PJ_dialog_vtable_t* vt, void* ctx) {
    return DialogHandle(vt, ctx, false);
  }

  /// Non-owning handle built from a PJ_borrowed_dialog_t fat pointer.
  static DialogHandle fromBorrowed(PJ_borrowed_dialog_t borrowed_ref) {
    return DialogHandle(borrowed_ref.vtable, borrowed_ref.ctx, false);
  }

  ~DialogHandle() {
    if (owned_ && vt_ && ctx_) {
      vt_->destroy(ctx_);
    }
  }

  DialogHandle(DialogHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_), owned_(other.owned_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
    other.owned_ = false;
  }

  DialogHandle& operator=(DialogHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(owned_, other.owned_);
    }
    return *this;
  }

  DialogHandle(const DialogHandle&) = delete;
  DialogHandle& operator=(const DialogHandle&) = delete;

  // --- Queries ---
  [[nodiscard]] std::string manifest() const {
    return safeString(vt_->get_manifest(ctx_));
  }
  [[nodiscard]] std::string ui_content() const {
    return safeString(vt_->get_ui_content(ctx_));
  }
  [[nodiscard]] std::string widget_data() const {
    return safeString(vt_->get_widget_data(ctx_));
  }

  // --- Events (fallible — errors swallowed here; callers that need detail call vtable directly) ---
  [[nodiscard]] bool sendEvent(std::string_view widget_name, std::string_view event_json) {
    return vt_->on_widget_event(ctx_, std::string(widget_name).c_str(), std::string(event_json).c_str(), nullptr);
  }

  [[nodiscard]] bool tick() {
    return vt_->on_tick(ctx_, nullptr);
  }

  // --- Dialog result ---
  void accept(std::string_view final_state_json) {
    vt_->on_accepted(ctx_, std::string(final_state_json).c_str());
  }
  void reject() {
    vt_->on_rejected(ctx_);
  }

  // --- Config persistence ---
  [[nodiscard]] std::string save_config() const {
    PJ_string_view_t sv{};
    if (!vt_->save_config(ctx_, &sv, nullptr)) {
      return std::string();
    }
    return sv.data == nullptr ? std::string() : std::string(sv.data, sv.size);
  }

  [[nodiscard]] bool load_config(std::string_view config_json) {
    PJ_string_view_t sv{config_json.data(), config_json.size()};
    return vt_->load_config(ctx_, sv, nullptr);
  }

  // --- Escape hatch ---
  [[nodiscard]] const PJ_dialog_vtable_t* vtable() const {
    return vt_;
  }
  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  DialogHandle(const PJ_dialog_vtable_t* vt, void* ctx, bool owned) : vt_(vt), ctx_(ctx), owned_(owned) {}

  const PJ_dialog_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  bool owned_ = true;

  static std::string safeString(const char* s) {
    return s ? std::string(s) : std::string();
  }
};

}  // namespace PJ
