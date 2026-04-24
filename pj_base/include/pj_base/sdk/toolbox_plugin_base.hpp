/**
 * @file toolbox_plugin_base.hpp
 * @brief C++ SDK for implementing Toolbox plugins.
 *
 * Plugin authors subclass ToolboxPluginBase, override the required virtuals,
 * and export with the PJ_TOOLBOX_PLUGIN(ClassName, manifest) macro. The SDK handles
 * C ABI trampoline generation and exception safety.
 *
 * See pj_plugins/examples/mock_toolbox.cpp for a complete example.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/toolbox_protocol.h"

namespace PJ {

/// Severity level for plugin-to-host diagnostic messages.
enum class ToolboxMessageLevel : uint32_t {
  kInfo = PJ_TOOLBOX_MESSAGE_INFO,
  kWarning = PJ_TOOLBOX_MESSAGE_WARNING,
  kError = PJ_TOOLBOX_MESSAGE_ERROR,
};

/// @name Capability flag constants
/// @{
constexpr uint64_t kToolboxCapabilityHasDialog = PJ_TOOLBOX_CAPABILITY_HAS_DIALOG;
constexpr uint64_t kToolboxCapabilityNonModalDialog = PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG;
/// @}

/**
 * Type-safe C++ view over the toolbox runtime host vtable.
 *
 * Plugins access this via ToolboxPluginBase::runtimeHost(). Each method
 * is a null-safe wrapper: calls on an unbound host are no-ops or return
 * safe defaults.
 */
class ToolboxRuntimeHostView {
 public:
  explicit ToolboxRuntimeHostView(PJ_toolbox_runtime_host_t host = {}) : host_(host) {}

  /// Returns true if both context and vtable pointers are set.
  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Returns the last host-side error, or empty if none.
  [[nodiscard]] std::string_view lastError() const {
    if (!valid() || host_.vtable->get_last_error == nullptr) {
      return {};
    }
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

  /// Send a diagnostic message to the host UI log.
  void reportMessage(ToolboxMessageLevel level, std::string_view message) const {
    if (valid() && host_.vtable->report_message != nullptr) {
      host_.vtable->report_message(
          host_.ctx, static_cast<PJ_toolbox_message_level_t>(level), sdk::toAbiString(message));
    }
  }

  /// Notify the host that data has been modified; host should refresh UI.
  void notifyDataChanged() const {
    if (valid() && host_.vtable->notify_data_changed != nullptr) {
      host_.vtable->notify_data_changed(host_.ctx);
    }
  }

  /// Access the underlying C ABI struct.
  [[nodiscard]] const PJ_toolbox_runtime_host_t& raw() const {
    return host_;
  }

 private:
  PJ_toolbox_runtime_host_t host_{};
};

/**
 * Base class for Toolbox plugins.
 *
 * Subclass and override the pure-virtual method: capabilities().
 * Optionally override bindToolboxHost, bindRuntimeHost, saveConfig/loadConfig,
 * dialogContext for richer behaviour.
 *
 * Use toolboxHost() and runtimeHost() (protected) to interact with the host.
 * Export with PJ_TOOLBOX_PLUGIN(YourClass, manifest).
 *
 * The base class generates C ABI trampolines with full exception safety —
 * any exception thrown from a virtual is caught, stored via setLastError(),
 * and converted to a false/null return across the ABI boundary.
 */
class ToolboxPluginBase {
 public:
  virtual ~ToolboxPluginBase() = default;

  /// Return a bitmask of kToolboxCapability* flags describing this plugin's features.
  virtual uint64_t capabilities() const = 0;

  /// Bind the data-plane toolbox host. Override only if you need custom validation.
  virtual Status bindToolboxHost(PJ_toolbox_host_t toolbox_host) {
    if (toolbox_host.ctx == nullptr || toolbox_host.vtable == nullptr) {
      return unexpected("toolbox host is not bound");
    }
    toolbox_host_ = toolbox_host;
    return okStatus();
  }

  /// Bind the control-plane runtime host. Override only if you need custom validation.
  virtual Status bindRuntimeHost(PJ_toolbox_runtime_host_t runtime_host) {
    if (runtime_host.ctx == nullptr || runtime_host.vtable == nullptr) {
      return unexpected("runtime host is not bound");
    }
    runtime_host_ = runtime_host;
    return okStatus();
  }

  /// Bind the optional colormap registry service. Override for plugins that
  /// publish colormaps. Default accepts the registry (valid or not) as a no-op.
  virtual Status bindColorMapRegistry(PJ_colormap_registry_t registry) {
    colormap_registry_ = registry;
    return okStatus();
  }

  /// Serialize plugin configuration to JSON. Default returns "{}".
  virtual std::string saveConfig() const {
    return "{}";
  }

  /// Restore plugin configuration from JSON. Default accepts any input.
  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Return the last error message. Override for custom error reporting.
  virtual std::string lastError() const {
    return last_error_;
  }

  /// Override to return your dialog context pointer.
  /// Default returns nullptr (no dialog).
  virtual void* dialogContext() {
    return nullptr;
  }

  /// Override to react to new records being appended to the datastore.
  /// Default is a no-op.
  virtual void onDataChanged() {}

  template <typename CreateFn>
  static const PJ_toolbox_vtable_t* vtableWithCreate(CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    static const PJ_toolbox_vtable_t vt = {
        PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
        sizeof(PJ_toolbox_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_capabilities,
        trampoline_bind_toolbox_host,
        trampoline_bind_runtime_host,
        trampoline_bind_colormap_registry,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_get_dialog_context,
        trampoline_get_last_error,
        trampoline_on_data_changed,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] bool toolboxHostBound() const {
    return toolbox_host_.ctx != nullptr && toolbox_host_.vtable != nullptr;
  }

  [[nodiscard]] bool runtimeHostBound() const {
    return runtime_host_.ctx != nullptr && runtime_host_.vtable != nullptr;
  }

  [[nodiscard]] sdk::ToolboxHostView toolboxHost() const {
    return sdk::ToolboxHostView(toolbox_host_);
  }

  [[nodiscard]] ToolboxRuntimeHostView runtimeHost() const {
    return ToolboxRuntimeHostView(runtime_host_);
  }

  [[nodiscard]] sdk::ColorMapRegistryView colorMapRegistry() const {
    return sdk::ColorMapRegistryView(colormap_registry_);
  }

  [[nodiscard]] bool colorMapRegistryBound() const {
    return colormap_registry_.ctx != nullptr && colormap_registry_.vtable != nullptr;
  }

  void setLastError(std::string error) {
    last_error_ = std::move(error);
  }

 private:
  PJ_toolbox_host_t toolbox_host_{};
  PJ_toolbox_runtime_host_t runtime_host_{};
  PJ_colormap_registry_t colormap_registry_{};
  std::string config_buf_;
  mutable std::string last_error_;

  // C ABI trampolines — exception-safe bridges between host vtable calls and
  // C++ virtuals. Implementations live in detail/toolbox_trampolines.hpp.
  static void trampoline_destroy(void* ctx);
  static uint64_t trampoline_capabilities(void* ctx);
  static bool trampoline_bind_toolbox_host(void* ctx, PJ_toolbox_host_t toolbox_host);
  static bool trampoline_bind_runtime_host(void* ctx, PJ_toolbox_runtime_host_t runtime_host);
  static bool trampoline_bind_colormap_registry(void* ctx, PJ_colormap_registry_t registry);
  static const char* trampoline_save_config(void* ctx);
  static bool trampoline_load_config(void* ctx, const char* config_json);
  static void* trampoline_get_dialog_context(void* ctx);
  static const char* trampoline_get_last_error(void* ctx);
  static void trampoline_on_data_changed(void* ctx);
};

}  // namespace PJ

// Out-of-line trampoline definitions — separated to keep the public API header concise.
#include "pj_base/sdk/detail/toolbox_trampolines.hpp"

/**
 * Export a ToolboxPluginBase subclass as a shared-library plugin.
 *
 * Place at file scope (after the class definition). Generates the extern "C"
 * entry point `PJ_get_toolbox_vtable` that the host resolves via dlsym.
 *
 * @param ClassName   The ToolboxPluginBase subclass to instantiate.
 * @param manifest    A string literal containing the JSON manifest
 *                    (must have at least "name" and "version" keys).
 *
 * Usage:
 * @code
 *   PJ_TOOLBOX_PLUGIN(MyToolbox, R"({"name":"My Toolbox","version":"1.0.0"})")
 * @endcode
 */
#define PJ_TOOLBOX_PLUGIN(ClassName, manifest)                                                        \
  extern "C" PJ_TOOLBOX_EXPORT const PJ_toolbox_vtable_t* PJ_get_toolbox_vtable() {                   \
    static const PJ_toolbox_vtable_t* vt =                                                            \
        PJ::ToolboxPluginBase::vtableWithCreate([]() -> void* { return new ClassName(); }, manifest); \
    return vt;                                                                                        \
  }
