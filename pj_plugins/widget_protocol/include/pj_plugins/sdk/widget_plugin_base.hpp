/**
 * @file widget_plugin_base.hpp
 * @brief C++ SDK for implementing Widget plugins (protocol v1).
 *
 * This header and everything it includes is free of the GUI toolkit: the
 * widget object crosses as `void*`. The plugin's own source includes the
 * toolkit and casts the parent pointer to the toolkit's widget type; the SDK
 * never does. A worked example is in pj_plugins/docs/widget-guide.md.
 *
 * All trampolines are noexcept at the ABI boundary.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_abi_export.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/span.hpp"
#include "pj_plugins/widget_protocol.h"
#include "pj_plugins/widget_types.hpp"

namespace PJ {

/// Type-safe view over the "pj.widget_host.v1" service. Main-thread only.
class WidgetHostView {
 public:
  WidgetHostView() = default;
  explicit WidgetHostView(PJ_widget_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Read the history of one wire. @p on_value is called once per sample, in ascending time, before
  /// this function returns; the PJ_widget_value_t it receives (text, bytes, type_name, label included)
  /// is valid only inside that call, so copy what you keep. @p on_value returns void (take every
  /// sample) or bool (false stops the delivery early). @p max_samples == 0 lets the host pick its cap.
  ///
  /// Returns the number of samples the range held before the host's cap or decimation: more than you
  /// received means the history was reduced. Fails when the wire does not exist, the host has no
  /// history, or @p on_value threw (the delivery stops at the throw and the message is returned).
  ///
  /// Valid from any plugin method and from the widget's own Qt slots, on the main thread; never from
  /// a paint handler.
  template <typename OnValue>
  [[nodiscard]] Expected<uint32_t> queryHistory(
      uint32_t input, uint32_t slot, double from, double to, uint32_t max_samples, OnValue&& on_value) const {
    if (!valid() || host_.vtable->query_history == nullptr) {
      return unexpected("widget host does not provide query_history");
    }
    using Callback = std::remove_reference_t<OnValue>;
    using Result = std::invoke_result_t<Callback&, const PJ_widget_value_t&>;
    static_assert(
        std::is_void_v<Result> || std::is_convertible_v<Result, bool>, "the history callback returns void or bool");
    struct Delivery {
      Callback* callback;
      std::optional<std::string> thrown;
    };
    Delivery delivery{&on_value, std::nullopt};
#if defined(_MSC_VER)
// A callback that always throws, inlined, leaves the `return` after it unreachable: C4702 under /WX.
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
    auto sink = +[](void* user, const PJ_widget_value_t* value) noexcept -> bool {
      auto* target = static_cast<Delivery*>(user);
      try {
        if constexpr (std::is_void_v<Result>) {
          (*target->callback)(*value);
          return true;
        } else {
          return static_cast<bool>((*target->callback)(*value));
        }
      } catch (const std::exception& e) {
        // A throwing callback must not unwind through the host: stop the delivery and report it.
        try {
          target->thrown = e.what();  // the copy may itself throw under memory pressure
        } catch (...) {
          target->thrown.emplace();
        }
        return false;
      } catch (...) {
        target->thrown.emplace();
        return false;
      }
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    PJ_error_t err{};
    uint32_t total = 0;
    if (!host_.vtable->query_history(host_.ctx, input, slot, from, to, max_samples, sink, &delivery, &total, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    if (delivery.thrown.has_value()) {
      return unexpected(
          delivery.thrown->empty() ? std::string("history callback threw")
                                   : std::string("history callback threw: ") + *delivery.thrown);
    }
    return total;
  }

  /// Tell the host inputs() changed so it re-reads and reconciles by name. Not from inside onUpdate
  /// or inputs().
  void notifyInputsChanged() const {
    if (valid() && host_.vtable->notify_inputs_changed != nullptr) {
      host_.vtable->notify_inputs_changed(host_.ctx);
    }
  }

  /// Send a diagnostic to the host's message list.
  void reportWidgetMessage(WidgetMessageLevel level, std::string_view message) const noexcept {
    if (valid() && host_.vtable->report_widget_message != nullptr) {
      host_.vtable->report_widget_message(
          host_.ctx, static_cast<PJ_widget_message_level_t>(level), sdk::toAbiString(message));
    }
  }

  [[nodiscard]] const PJ_widget_host_t& raw() const {
    return host_;
  }

 private:
  PJ_widget_host_t host_{};
};

}  // namespace PJ

namespace PJ::sdk {

/// Service trait for "pj.widget_host.v1".
struct WidgetHostService {
  static constexpr const char* kName = PJ_WIDGET_HOST_SERVICE_V1;
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_widget_host_t;
  using Vtable = PJ_widget_host_vtable_t;
  using View = ::PJ::WidgetHostView;
};

}  // namespace PJ::sdk

namespace PJ::detail {

[[nodiscard]] inline PJ_widget_build_info_t makeWidgetBuildInfo(
    uint32_t major, uint32_t minor, uint32_t patch, const char* version_str) {
  PJ_widget_build_info_t info{};
  info.struct_size = sizeof(PJ_widget_build_info_t);
  info.qt_major = major;
  info.qt_minor = minor;
  info.qt_patch = patch;
  info.flags = PJ::widgetBuiltAsDebug() ? static_cast<uint32_t>(PJ_WIDGET_BUILD_DEBUG) : 0u;
  info.compiler = sdk::toAbiString(PJ::widgetCompilerFamily());
  info.qt_version_str = sdk::toAbiString(version_str);
  return info;
}

}  // namespace PJ::detail

namespace PJ {

/**
 * Base class for Widget plugins (protocol v1). One C++ object per open panel.
 *
 * Ownership of the widget object: createWidget() returns an object the plugin
 * owns. The host removes it from its parent right before destroying the
 * instance, so the plugin's destructor must delete it if it is still alive
 * (hold it in a guarded pointer; it may also have been deleted already).
 */
class WidgetPluginBase {
 public:
  virtual ~WidgetPluginBase() = default;

  /// Inputs of this widget. Called after construction, after loadConfig and whenever the host
  /// asks again; may depend on settings. Names must be non-empty, unique and stable (the host refuses
  /// the list otherwise). Throw to report a failure: the host keeps the inputs it had.
  virtual std::vector<WidgetInput> inputs() const = 0;

  /// Create the widget object as a child of @p parent (possibly null) and return it. Called at most
  /// once. Throw on failure; the message reaches the host.
  virtual void* createWidget(void* parent) = 0;

  /// New values at @p time (display axis, seconds): one entry per WIRE (an unwired input has none, a
  /// `multiple` input one per series), kind NONE where there is no sample at or before @p time.
  /// Dispatch on `value.input` / `value.slot`, never on the position. Also called with unchanged
  /// samples after createWidget, after an accepted loadConfig and after a wiring change. @p values is
  /// valid only during the call. A throw is reported once through reportWidgetMessage (again only
  /// after a call that succeeded).
  virtual void onUpdate(Span<const PJ_widget_value_t> values, double time) = 0;

  /// Acquire host services. Default requires "pj.widget_host.v1".
  virtual Status bind(sdk::ServiceRegistry services);

  /// Internal state to persist in the layout, as JSON.
  virtual std::string saveConfig() const {
    return "{}";
  }

  /// Apply a configuration: every key of the manifest's `settings` (defaults filled in by the host)
  /// with the state saveConfig produced laid over it (a colliding saveConfig key wins). Restoring a
  /// layout and editing a setting both arrive here.
  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Static extension for @p id, or nullptr.
  virtual const void* pluginExtension(std::string_view id) {
    (void)id;
    return nullptr;
  }

  /// The vtable for one plugin. Templated so that every (CreateFn, BuildInfoFn) pair gets its own
  /// function-local static: the PJ_WIDGET_PLUGIN macros pass lambdas, whose types are unique per
  /// expansion, so several widget plugins linked into one binary (PJ_STATIC_PLUGINS) never share one.
  template <typename CreateFn, typename BuildInfoFn>
  static const PJ_widget_vtable_t* vtableWithCreate(
      CreateFn create_fn, const char* manifest, BuildInfoFn build_info_fn) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"id\"") != nullptr, "manifest must contain an \"id\" key");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    PJ_ASSERT(std::strstr(manifest, "\"widgets\"") != nullptr, "manifest must contain a \"widgets\" key");
    static const PJ_widget_vtable_t vt = {
        PJ_WIDGET_PLUGIN_PROTOCOL_VERSION,
        sizeof(PJ_widget_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        build_info_fn,
        trampoline_bind,
        trampoline_inputs,
        trampoline_create_qwidget,
        trampoline_on_update,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_get_plugin_extension,
    };
    PJ_ASSERT(vt.manifest_json == manifest, "vtableWithCreate instantiated twice with different manifests");
    return &vt;
  }

  /// Instantiate @p ClassName for widget type @p type. A class constructible from std::string_view
  /// receives the type (libraries exposing several widgets switch on it and throw for an unknown
  /// one); otherwise it is default-constructed. Returns nullptr when construction throws.
  template <typename ClassName>
  static void* instantiate(PJ_string_view_t type) noexcept {
    try {
      if constexpr (std::is_constructible_v<ClassName, std::string_view>) {
        return new ClassName(sdk::toStringView(type));
      } else {
        return new ClassName();
      }
    } catch (...) {
      return nullptr;
    }
  }

 protected:
  [[nodiscard]] const WidgetHostView& widgetHost() const {
    return host_view_;
  }
  [[nodiscard]] bool widgetHostBound() const {
    return host_view_.valid();
  }
  [[nodiscard]] sdk::ServiceRegistry services() const {
    return service_registry_;
  }

 private:
  sdk::ServiceRegistry service_registry_{};
  WidgetHostView host_view_{};
  std::string config_buf_;
  std::vector<WidgetInput> inputs_snapshot_;
  std::vector<PJ_widget_input_t> inputs_abi_;
  bool on_update_failure_reported_ = false;

  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    sdk::fillError(out_error, code, domain, message);
  }

  static void trampoline_destroy(void* ctx) noexcept {
    try {
      delete static_cast<WidgetPluginBase*>(ctx);
    } catch (...) {}
  }

  static bool trampoline_bind(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    try {
      auto status = self->bind(sdk::ServiceRegistry(registry));
      if (!status) {
        storeError(out_error, 1, "plugin", std::move(status).error());
        return false;
      }
      return true;
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("bind threw: ") + e.what());
      return false;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception in bind");
      return false;
    }
  }

  static bool trampoline_inputs(
      void* ctx, const PJ_widget_input_t** out_inputs, uint32_t* out_count, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    if (out_inputs == nullptr || out_count == nullptr) {
      storeError(out_error, 2, "plugin", "inputs called with null out_inputs or out_count");
      return false;
    }
    try {
      // Built aside and committed with non-throwing moves, so a throw leaves the previous array (which
      // the host may still be reading) intact. Moving a vector keeps its buffer, so the views in `abi`
      // stay valid after the move.
      std::vector<WidgetInput> snapshot = self->inputs();
      std::vector<PJ_widget_input_t> abi;
      abi.reserve(snapshot.size());
      for (const auto& input : snapshot) {
        PJ_widget_input_t entry{};
        entry.name = sdk::toAbiString(input.name);
        entry.label = sdk::toAbiString(input.label);
        entry.unit = sdk::toAbiString(input.unit);
        entry.kind = static_cast<PJ_widget_input_kind_t>(input.kind);
        entry.flags = input.multiple ? static_cast<uint32_t>(PJ_WIDGET_INPUT_MULTIPLE) : 0u;
        abi.push_back(entry);
      }
      self->inputs_snapshot_ = std::move(snapshot);
      self->inputs_abi_ = std::move(abi);
      *out_inputs = self->inputs_abi_.data();
      *out_count = static_cast<uint32_t>(self->inputs_abi_.size());
      return true;
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("inputs threw: ") + e.what());
      return false;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception in inputs");
      return false;
    }
  }

  static PJ_qwidget_t trampoline_create_qwidget(void* ctx, PJ_qwidget_t parent, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    try {
      void* widget = self->createWidget(parent);
      if (widget == nullptr) {
        storeError(out_error, 1, "plugin", "createWidget returned null");
      }
      return widget;
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("createWidget threw: ") + e.what());
      return nullptr;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception in createWidget");
      return nullptr;
    }
  }

  static void trampoline_on_update(void* ctx, const PJ_widget_value_t* values, uint32_t count, double time) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    try {
      self->onUpdate(Span<const PJ_widget_value_t>(values, values == nullptr ? 0 : count), time);
      self->on_update_failure_reported_ = false;
    } catch (const std::exception& e) {
      self->reportOnUpdateFailure(e.what());
    } catch (...) {
      self->reportOnUpdateFailure(nullptr);
    }
  }

  // One message per run of failing onUpdate calls: on_update runs on every tracker tick, so a widget
  // that throws each time would otherwise flood the host. The next successful call re-arms it.
  void reportOnUpdateFailure(const char* what) noexcept {
    if (on_update_failure_reported_) {
      return;
    }
    on_update_failure_reported_ = true;
    try {
      const std::string message =
          what != nullptr ? std::string("onUpdate threw: ") + what : std::string("unknown exception in onUpdate");
      host_view_.reportWidgetMessage(WidgetMessageLevel::kError, message);
    } catch (...) {}
  }

  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    if (out_json == nullptr) {
      storeError(out_error, 2, "plugin", "save_config called with null out_json");
      return false;
    }
    try {
      self->config_buf_ = self->saveConfig();
      out_json->data = self->config_buf_.data();
      out_json->size = self->config_buf_.size();
      return true;
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("save_config threw: ") + e.what());
      return false;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception in save_config");
      return false;
    }
  }

  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    try {
      // The inputs array from the previous inputs() call may describe settings that are about to change;
      // the contract ends its lifetime at load_config.
      self->inputs_abi_.clear();
      self->inputs_snapshot_.clear();
      auto status = self->loadConfig(sdk::toStringView(config_json));
      if (!status) {
        storeError(out_error, 1, "plugin", std::move(status).error());
        return false;
      }
      return true;
    } catch (const std::exception& e) {
      storeError(out_error, 1, "plugin", std::string("load_config threw: ") + e.what());
      return false;
    } catch (...) {
      storeError(out_error, 1, "plugin", "unknown exception in load_config");
      return false;
    }
  }

  static const void* trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept {
    auto* self = static_cast<WidgetPluginBase*>(ctx);
    try {
      return self->pluginExtension(sdk::toStringView(id));
    } catch (...) {
      return nullptr;
    }
  }
};

inline Status WidgetPluginBase::bind(sdk::ServiceRegistry services) {
  auto host = services.require<sdk::WidgetHostService>();
  if (!host) {
    return unexpected(std::move(host).error());
  }
  host_view_ = *host;
  service_registry_ = services;
  return okStatus();
}

}  // namespace PJ

// Build identity of the toolkit the plugin translation unit compiles against. The macros come from the
// toolkit's own headers, so the TU that expands PJ_WIDGET_PLUGIN must have included one of them.
#define PJ_WIDGET_BUILD_INFO_GETTER_                                                                           \
  []() noexcept -> const PJ_widget_build_info_t* {                                                             \
    static const PJ_widget_build_info_t info =                                                                 \
        PJ::detail::makeWidgetBuildInfo(QT_VERSION_MAJOR, QT_VERSION_MINOR, QT_VERSION_PATCH, QT_VERSION_STR); \
    return &info;                                                                                              \
  }

// A lambda, not &instantiate<ClassName>: its type is unique per expansion, which gives every plugin its
// own vtableWithCreate instantiation (and so its own static vtable).
#define PJ_WIDGET_CREATE_FN_(ClassName) \
  [](PJ_string_view_t type) noexcept -> void* { return PJ::WidgetPluginBase::instantiate<ClassName>(type); }

#define PJ_WIDGET_PLUGIN(ClassName, manifest)                                             \
  PJ_EXPORT_PLUGIN_ABI_VERSION(PJ_WIDGET_EXPORT)                                          \
  extern "C" PJ_WIDGET_EXPORT const PJ_widget_vtable_t* PJ_get_widget_vtable() noexcept { \
    static const PJ_widget_vtable_t* vt = PJ::WidgetPluginBase::vtableWithCreate(         \
        PJ_WIDGET_CREATE_FN_(ClassName), manifest, PJ_WIDGET_BUILD_INFO_GETTER_);         \
    return vt;                                                                            \
  }

// Variant for namespaced plugin classes. SymbolName must be an unqualified
// identifier and is used only to form the unique static getter name.
#define PJ_WIDGET_PLUGIN_NAMED(ClassName, SymbolName, manifest) PJ_WIDGET_PLUGIN(ClassName, manifest)

// --- Static-link variant (WASM / no dlopen) ---  see toolbox_plugin_base.hpp
#ifdef PJ_STATIC_PLUGINS
#undef PJ_WIDGET_PLUGIN
#undef PJ_WIDGET_PLUGIN_NAMED
#define PJ_WIDGET_PLUGIN_NAMED(ClassName, SymbolName, manifest)                   \
  const PJ_widget_vtable_t* pj_static_get_widget_vtable_##SymbolName() noexcept { \
    static const PJ_widget_vtable_t* vt = PJ::WidgetPluginBase::vtableWithCreate( \
        PJ_WIDGET_CREATE_FN_(ClassName), manifest, PJ_WIDGET_BUILD_INFO_GETTER_); \
    return vt;                                                                    \
  }
#define PJ_WIDGET_PLUGIN(ClassName, manifest) PJ_WIDGET_PLUGIN_NAMED(ClassName, ClassName, manifest)
#endif
