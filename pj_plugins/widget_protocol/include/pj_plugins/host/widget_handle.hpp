/**
 * @file widget_handle.hpp
 * @brief RAII wrapper around a single Widget plugin instance (protocol v1).
 *
 * Every method is main-thread only. The handle never touches the widget
 * object it obtains from create_qwidget: it forwards the opaque pointer.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_plugins/widget_protocol.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/plugin_data_api.hpp>
#include <pj_base/span.hpp>
#include <pj_plugins/widget_types.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PJ {

namespace detail {

/// @p view as a string_view, or an error for the shapes no valid plugin produces (null data with a
/// size, or longer than @p max_bytes). Checked before any pointer arithmetic, so a corrupt size is
/// reported instead of being added to the pointer.
[[nodiscard]] inline Expected<std::string_view> checkedWidgetView(
    PJ_string_view_t view, size_t max_bytes, const char* what) {
  if (view.size == 0) {
    return std::string_view{};
  }
  if (view.data == nullptr || view.size > max_bytes) {
    return unexpected(std::string("widget plugin returned an invalid ") + what + " string view");
  }
  return std::string_view(view.data, view.size);
}

}  // namespace detail

/// RAII handle owning one Widget plugin instance (one open panel).
///
/// Destruction is synchronous and is where the plugin deletes the widget
/// object it created. Detach that object from its parent (setParent(nullptr))
/// BEFORE the handle dies; destroying the handle while a parent still holds
/// the object is the one thing the contract forbids.
class WidgetHandle {
 public:
  /// valid() is false when the plugin returns null for @p type.
  WidgetHandle(const PJ_widget_vtable_t* vt, std::string_view type, std::shared_ptr<void> library_owner = {})
      : vt_(vt), library_owner_(std::move(library_owner)) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_WIDGET_PLUGIN_PROTOCOL_VERSION);
      ctx_ = vt_->create(sdk::toAbiString(type));
    }
  }

  ~WidgetHandle() {
    reset();
  }

  WidgetHandle(WidgetHandle&& other) noexcept
      : vt_(other.vt_), ctx_(other.ctx_), library_owner_(std::move(other.library_owner_)) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  WidgetHandle& operator=(WidgetHandle&& other) noexcept {
    if (this != &other) {
      reset();
      vt_ = std::exchange(other.vt_, nullptr);
      ctx_ = std::exchange(other.ctx_, nullptr);
      library_owner_ = std::move(other.library_owner_);
    }
    return *this;
  }

  WidgetHandle(const WidgetHandle&) = delete;
  WidgetHandle& operator=(const WidgetHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
  }

  /// Destroy the plugin instance now (synchronous). Idempotent.
  void reset() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
    ctx_ = nullptr;
  }

  [[nodiscard]] std::string manifest() const {
    return vt_ != nullptr && vt_->manifest_json != nullptr ? std::string(vt_->manifest_json) : std::string();
  }

  /// Valid for the process lifetime (the library is never unloaded); never null for a handle from
  /// WidgetLibrary, which validated it (size and string views) at load.
  [[nodiscard]] const PJ_widget_build_info_t* buildInfo() const {
    return vt_ != nullptr ? vt_->build_info() : nullptr;
  }

  [[nodiscard]] Status bind(PJ_service_registry_t registry) {
    PJ_error_t err{};
    if (!vt_->bind(ctx_, registry, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    return okStatus();
  }

  /// Largest input count, input string and config document a plugin may hand back. Anything beyond
  /// is a broken plugin, not data: the handle refuses it before reading a byte, so a corrupt view
  /// can never turn into an out-of-bounds read on the host.
  static constexpr uint32_t kMaxInputs = 4096;
  static constexpr size_t kMaxInputTextBytes = size_t{1} << 20;
  static constexpr size_t kMaxConfigBytes = size_t{64} << 20;

  /// Copy of the plugin's current inputs. Fails, and the host keeps the inputs it had, when the plugin
  /// reports an error, or returns an array no valid plugin produces: more than kMaxInputs entries, a
  /// null array with a count, a string view that cannot be valid (null data with a size, or longer
  /// than kMaxInputTextBytes), an empty or duplicate `name`, or a `kind` this host does not know.
  /// Unknown `flags` bits are ignored.
  [[nodiscard]] Expected<std::vector<WidgetInput>> inputs() const {
    const PJ_widget_input_t* array = nullptr;
    uint32_t count = 0;
    PJ_error_t err{};
    if (!vt_->inputs(ctx_, &array, &count, &err)) {
      return unexpected(err.message[0] != '\0' ? sdk::errorToString(err) : std::string("inputs failed"));
    }
    std::vector<WidgetInput> result;
    if (count == 0) {
      return result;
    }
    if (array == nullptr) {
      return unexpected("widget plugin returned a null inputs array with a non-zero count");
    }
    if (count > kMaxInputs) {
      return unexpected("widget plugin returned an impossible number of inputs");
    }
    result.reserve(count);
    std::unordered_set<std::string_view> names;
    for (uint32_t i = 0; i < count; ++i) {
      const PJ_widget_input_t& in = array[i];
      auto name = detail::checkedWidgetView(in.name, kMaxInputTextBytes, "input name");
      auto label = detail::checkedWidgetView(in.label, kMaxInputTextBytes, "input label");
      auto unit = detail::checkedWidgetView(in.unit, kMaxInputTextBytes, "input unit");
      for (const auto* view : {&name, &label, &unit}) {
        if (!*view) {
          return unexpected(view->error());
        }
      }
      if (name->empty()) {
        return unexpected("widget plugin returned an input with an empty name");
      }
      if (!names.insert(*name).second) {
        return unexpected("widget plugin returned the input name '" + std::string(*name) + "' twice");
      }
      if (in.kind != PJ_WIDGET_INPUT_NUMBER && in.kind != PJ_WIDGET_INPUT_TEXT && in.kind != PJ_WIDGET_INPUT_OBJECT) {
        return unexpected(
            "widget plugin returned input '" + std::string(*name) + "' with unknown kind " +
            std::to_string(static_cast<uint32_t>(in.kind)));
      }
      WidgetInput item;
      item.name = std::string(*name);
      item.label = std::string(*label);
      item.unit = std::string(*unit);
      item.kind = static_cast<WidgetInputKind>(in.kind);
      item.multiple = (in.flags & PJ_WIDGET_INPUT_MULTIPLE) != 0;
      result.push_back(std::move(item));
    }
    return result;
  }

  /// Ask the plugin to create its widget object under @p parent (the host's widget, may be null).
  /// The returned pointer is owned by the plugin; this layer never dereferences it.
  [[nodiscard]] Expected<void*> createQWidget(void* parent) {
    PJ_error_t err{};
    void* widget = vt_->create_qwidget(ctx_, parent, &err);
    if (widget == nullptr) {
      return unexpected(err.message[0] != '\0' ? sdk::errorToString(err) : std::string("create_qwidget returned null"));
    }
    return widget;
  }

  /// Deliver values at @p time. @p values must stay valid for the duration of the call only.
  void onUpdate(Span<const PJ_widget_value_t> values, double time) {
    vt_->on_update(ctx_, values.data(), static_cast<uint32_t>(values.size()), time);
  }

  /// Throws std::invalid_argument when the returned view cannot be valid (null data with a size, or
  /// longer than kMaxConfigBytes).
  [[nodiscard]] Status saveConfig(std::string& out_json) {
    PJ_string_view_t sv{};
    PJ_error_t err{};
    if (!vt_->save_config(ctx_, &sv, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    auto json = detail::checkedWidgetView(sv, kMaxConfigBytes, "save_config");
    if (!json) {
      throw std::invalid_argument(json.error());
    }
    out_json.assign(*json);
    return okStatus();
  }

  [[nodiscard]] Status loadConfig(std::string_view config_json) {
    PJ_error_t err{};
    if (!vt_->load_config(ctx_, sdk::toAbiString(config_json), &err)) {
      return unexpected(sdk::errorToString(err));
    }
    return okStatus();
  }

  /// Query a plugin-exposed extension by reverse-DNS id. Tail-slot gated.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_widget_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    return vt_->get_plugin_extension(ctx_, sdk::toAbiString(id));
  }

  [[nodiscard]] const PJ_widget_vtable_t* vtable() const {
    return vt_;
  }

  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  const PJ_widget_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  std::shared_ptr<void> library_owner_;
};

}  // namespace PJ
