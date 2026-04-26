/**
 * @file toolbox_plugin_base.hpp
 * @brief C++ SDK for implementing Toolbox plugins (protocol v4).
 *
 * All trampolines are noexcept at the ABI boundary.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_abi_export.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/toolbox_protocol.h"

namespace PJ {

enum class ToolboxMessageLevel : uint32_t {
  kInfo = PJ_TOOLBOX_MESSAGE_INFO,
  kWarning = PJ_TOOLBOX_MESSAGE_WARNING,
  kError = PJ_TOOLBOX_MESSAGE_ERROR,
};

constexpr uint64_t kToolboxCapabilityHasDialog = PJ_TOOLBOX_CAPABILITY_HAS_DIALOG;
constexpr uint64_t kToolboxCapabilityNonModalDialog = PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG;

/// Type-safe view over the toolbox runtime host vtable.
class ToolboxRuntimeHostView {
 public:
  ToolboxRuntimeHostView() = default;
  explicit ToolboxRuntimeHostView(PJ_toolbox_runtime_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  void reportMessage(ToolboxMessageLevel level, std::string_view message) const noexcept {
    if (valid() && host_.vtable->report_message != nullptr) {
      host_.vtable->report_message(
          host_.ctx, static_cast<PJ_toolbox_message_level_t>(level), sdk::toAbiString(message));
    }
  }

  void notifyDataChanged() const {
    if (valid() && host_.vtable->notify_data_changed != nullptr) {
      host_.vtable->notify_data_changed(host_.ctx);
    }
  }

  [[nodiscard]] const PJ_toolbox_runtime_host_t& raw() const {
    return host_;
  }

 private:
  PJ_toolbox_runtime_host_t host_{};
};

}  // namespace PJ

namespace PJ::sdk {

/// Service trait for the toolbox runtime host. Defined here (rather than in
/// service_traits.hpp) because it depends on `ToolboxRuntimeHostView` which
/// lives in this header.
struct ToolboxRuntimeHostService {
  static constexpr const char* kName = "pj.toolbox_runtime.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_toolbox_runtime_host_t;
  using Vtable = PJ_toolbox_runtime_host_vtable_t;
  using View = ::PJ::ToolboxRuntimeHostView;
};

}  // namespace PJ::sdk

namespace PJ {

/**
 * Base class for Toolbox plugins (protocol v4).
 */
class ToolboxPluginBase {
 public:
  virtual ~ToolboxPluginBase() = default;

  virtual uint64_t capabilities() const = 0;

  /// Acquire host-provided services.
  ///
  /// Default implementation pulls:
  ///   - "pj.toolbox_write.v1"       → ToolboxHost          (mandatory)
  ///   - "pj.toolbox_runtime.v1"     → RuntimeHost          (mandatory)
  ///   - "pj.colormap.v1"            → ColorMap             (optional)
  ///   - "pj.toolbox_object_read.v1" → ObjectReadHost       (optional)
  ///
  /// Override to acquire additional services or relax defaults.
  virtual Status bind(sdk::ServiceRegistry services) {
    auto host = services.require<sdk::ToolboxHostService>();
    if (!host) {
      return unexpected(std::move(host).error());
    }
    toolbox_host_view_ = *host;

    auto runtime = services.require<sdk::ToolboxRuntimeHostService>();
    if (!runtime) {
      return unexpected(std::move(runtime).error());
    }
    runtime_host_view_ = *runtime;

    // Colormap is optional — acquire opportunistically.
    if (auto cm = services.get<sdk::ColorMapRegistryService>()) {
      colormap_view_ = *cm;
    }

    // Object read is optional — transformer-style toolboxes resolve it.
    if (auto obj = services.get<sdk::ToolboxObjectReadHostService>()) {
      object_read_host_view_ = *obj;
    }

    service_registry_ = services;
    return okStatus();
  }

  virtual std::string saveConfig() const {
    return "{}";
  }

  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Return a typed borrowed reference to this toolbox's embedded dialog.
  /// Default returns `{nullptr, nullptr}` (no dialog).
  virtual PJ_borrowed_dialog_t getDialog() {
    return PJ_borrowed_dialog_t{nullptr, nullptr};
  }

  /// React to data appended to the datastore. Default is no-op.
  virtual void onDataChanged() {}

  /// Return a pointer to a static plugin-exposed extension for @p id, or
  /// nullptr if unknown. Default returns nullptr.
  virtual const void* pluginExtension(std::string_view id) {
    (void)id;
    return nullptr;
  }

  template <typename CreateFn>
  static const PJ_toolbox_vtable_t* vtableWithCreate(CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"id\"") != nullptr, "manifest must contain an \"id\" key");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    static const PJ_toolbox_vtable_t vt = {
        PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
        sizeof(PJ_toolbox_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_capabilities,
        trampoline_bind,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_get_dialog,
        trampoline_on_data_changed,
        trampoline_get_plugin_extension,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] sdk::ServiceRegistry services() const {
    return service_registry_;
  }

  [[nodiscard]] const sdk::ToolboxHostView& toolboxHost() const {
    return toolbox_host_view_;
  }

  [[nodiscard]] const ToolboxRuntimeHostView& runtimeHost() const {
    return runtime_host_view_;
  }

  [[nodiscard]] const sdk::ColorMapRegistryView& colorMapRegistry() const {
    return colormap_view_;
  }

  /// Optional — returns nullptr when the host does not register
  /// `pj.toolbox_object_read.v1`. Transformer-style toolboxes check this
  /// before touching ObjectStore; scalar-only toolboxes never call it.
  [[nodiscard]] const sdk::ToolboxObjectReadHostView* objectReadHost() const {
    return object_read_host_view_.valid() ? &object_read_host_view_ : nullptr;
  }

  [[nodiscard]] bool toolboxHostBound() const {
    return toolbox_host_view_.valid();
  }
  [[nodiscard]] bool runtimeHostBound() const {
    return runtime_host_view_.valid();
  }
  [[nodiscard]] bool colorMapRegistryBound() const {
    return colormap_view_.valid();
  }

 private:
  sdk::ServiceRegistry service_registry_{};
  sdk::ToolboxHostView toolbox_host_view_{PJ_toolbox_host_t{}};
  ToolboxRuntimeHostView runtime_host_view_{};
  sdk::ColorMapRegistryView colormap_view_{};
  sdk::ToolboxObjectReadHostView object_read_host_view_{};
  std::string config_buf_;

  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    sdk::fillError(out_error, code, domain, message);
  }

  static void trampoline_destroy(void* ctx) noexcept;
  static uint64_t trampoline_capabilities(void* ctx) noexcept;
  static bool trampoline_bind(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept;
  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept;
  static PJ_borrowed_dialog_t trampoline_get_dialog(void* ctx) noexcept;
  static void trampoline_on_data_changed(void* ctx) noexcept;
  static const void* trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept;
};

}  // namespace PJ

#include "pj_base/sdk/detail/toolbox_trampolines.hpp"

#define PJ_TOOLBOX_PLUGIN(ClassName, manifest)                                               \
  PJ_EXPORT_PLUGIN_ABI_VERSION(PJ_TOOLBOX_EXPORT)                                            \
  extern "C" PJ_TOOLBOX_EXPORT const PJ_toolbox_vtable_t* PJ_get_toolbox_vtable() noexcept { \
    static const PJ_toolbox_vtable_t* vt = PJ::ToolboxPluginBase::vtableWithCreate(          \
        []() noexcept -> void* {                                                             \
          try {                                                                              \
            return new ClassName();                                                          \
          } catch (...) {                                                                    \
            return nullptr;                                                                  \
          }                                                                                  \
        },                                                                                   \
        manifest);                                                                           \
    return vt;                                                                               \
  }
