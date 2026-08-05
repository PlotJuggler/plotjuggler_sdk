#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_plugins/dialog_protocol.h>

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// RAII wrapper around a plugin vtable + context (protocol v4).
class DialogHandle {
 public:
  explicit DialogHandle(const PJ_dialog_vtable_t* vt, std::shared_ptr<void> library_owner = {})
      : vt_(vt), library_owner_(std::move(library_owner)) {
    if (vt_) {
      assert(vt_->protocol_version == PJ_DIALOG_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  /// Non-owning handle from an externally managed context (e.g. a plugin's embedded dialog).
  static DialogHandle borrowed(const PJ_dialog_vtable_t* vt, void* ctx, std::shared_ptr<void> library_owner = {}) {
    return DialogHandle(vt, ctx, false, std::move(library_owner));
  }

  /// Non-owning handle built from a PJ_borrowed_dialog_t fat pointer.
  static DialogHandle fromBorrowed(PJ_borrowed_dialog_t borrowed_ref, std::shared_ptr<void> library_owner = {}) {
    return DialogHandle(borrowed_ref.vtable, borrowed_ref.ctx, false, std::move(library_owner));
  }

  ~DialogHandle() {
    if (owned_ && vt_ && ctx_) {
      vt_->destroy(ctx_);
    }
  }

  DialogHandle(DialogHandle&& other) noexcept
      : vt_(other.vt_), ctx_(other.ctx_), owned_(other.owned_), library_owner_(std::move(other.library_owner_)) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
    other.owned_ = false;
  }

  DialogHandle& operator=(DialogHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(owned_, other.owned_);
      std::swap(library_owner_, other.library_owner_);
    }
    return *this;
  }

  DialogHandle(const DialogHandle&) = delete;
  DialogHandle& operator=(const DialogHandle&) = delete;

  /// Supply immutable host information to this dialog instance.
  ///
  /// This method owns the optional-tail-slot gate: runtime engines must call it
  /// instead of reading PJ_dialog_vtable_t::set_host_info directly. Returns
  /// false without invoking plugin code or modifying @p out_error when the
  /// dialog predates the slot or exposes it as null. Otherwise forwards the
  /// plugin's boolean result and error unchanged.
  ///
  /// Call after construction/borrowing and before ui_content(), load_config(),
  /// or widget_data(). The info and its string views need only remain valid for
  /// the duration of this call.
  ///
  /// @since 0.21.0
  [[nodiscard]] bool setHostInfo(const PJ_dialog_host_info_t& info, PJ_error_t* out_error = nullptr) {
    if (vt_ == nullptr || ctx_ == nullptr || !PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, vt_, set_host_info)) {
      return false;
    }
    return vt_->set_host_info(ctx_, &info, out_error);
  }

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
  DialogHandle(const PJ_dialog_vtable_t* vt, void* ctx, bool owned, std::shared_ptr<void> library_owner = {})
      : vt_(vt), ctx_(ctx), owned_(owned), library_owner_(std::move(library_owner)) {}

  const PJ_dialog_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  bool owned_ = true;
  std::shared_ptr<void> library_owner_;

  static std::string safeString(const char* s) {
    return s ? std::string(s) : std::string();
  }
};

}  // namespace PJ
