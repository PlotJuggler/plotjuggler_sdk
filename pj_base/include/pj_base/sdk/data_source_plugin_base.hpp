/**
 * @file data_source_plugin_base.hpp
 * @brief C++ SDK for implementing DataSource plugins (protocol v4).
 *
 * Plugin authors subclass `DataSourcePluginBase`, override the required
 * virtuals, and export with `PJ_DATA_SOURCE_PLUGIN(ClassName, manifest)`.
 *
 * v4 contract (plugin-author perspective):
 *   - Override `capabilities()`, `start()`, `stop()`, `currentState()`.
 *   - Optional: `bind()`, `pause()`, `resume()`, `poll()`, `saveConfig()`,
 *     `loadConfig()`, `getDialog()`.
 *   - Default `bind()` acquires the write host and runtime host from the
 *     service registry. Override to acquire extra services (colormap, etc.)
 *     or to relax the mandatory set.
 *   - Use `writeHost()` and `runtimeHost()` inside `start()`/`poll()` to
 *     interact with the host. Both return view classes that null-check and
 *     map to `Expected<T>` / `Status`.
 *
 * The SDK generates C ABI trampolines with full exception safety — any
 * exception thrown from a virtual is caught, stored on a per-instance
 * error slot, and converted to `false` + populated `PJ_error_t*` across
 * the ABI boundary.
 *
 * See `pj_plugins/examples/mock_data_source.cpp` for a complete example.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/plugin_abi_export.h"
#include "pj_base/sdk/data_source_host_views.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/sdk/service_traits.hpp"

namespace PJ {

/**
 * Base class for DataSource plugins (protocol v4).
 */
class DataSourcePluginBase {
 public:
  virtual ~DataSourcePluginBase() = default;

  /// Return a bitmask of kCapability* flags describing this source's features.
  virtual uint64_t capabilities() const = 0;

  /// Acquire host-provided services from the registry.
  ///
  /// Default implementation pulls the two services every DataSource needs:
  ///   - `"pj.source_write.v1"` → SourceWriteHost
  ///   - `"pj.runtime.v1"` → DataSourceRuntimeHost
  ///
  /// Plus one optional service that media-capable sources resolve:
  ///   - `"pj.source_object_write.v1"` → SourceObjectWriteHost (ObjectStore)
  ///
  /// Plugins that don't write to ObjectStore simply leave `objectWriteHost()`
  /// unused; hosts without an ObjectStore bound simply don't register it.
  /// Override to request additional services (e.g. colormap), or to relax
  /// the default requirement.
  virtual Status bind(sdk::ServiceRegistry services) {
    auto write = services.require<sdk::SourceWriteHostService>();
    if (!write) {
      return unexpected(std::move(write).error());
    }
    write_host_view_ = *write;

    auto runtime = services.require<sdk::DataSourceRuntimeHostService>();
    if (!runtime) {
      return unexpected(std::move(runtime).error());
    }
    runtime_host_view_ = *runtime;

    if (auto object_write = services.get<sdk::SourceObjectWriteHostService>()) {
      object_write_host_view_ = *object_write;
    }

    service_registry_ = services;
    return okStatus();
  }

  /// Serialize plugin configuration to JSON. Default returns "{}".
  virtual std::string saveConfig() const {
    return "{}";
  }

  /// Restore plugin configuration from JSON. Default is a no-op.
  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Begin data acquisition. Services are already bound when this is called.
  virtual Status start() = 0;

  /// Stop data acquisition. Must be idempotent.
  virtual void stop() = 0;

  virtual Status pause() {
    return unexpected("pause is not supported");
  }

  virtual Status resume() {
    return unexpected("resume is not supported");
  }

  virtual Status poll() {
    return okStatus();
  }

  virtual DataSourceState currentState() const = 0;

  /// Return a typed borrowed reference to this source's embedded dialog.
  /// Default returns `{nullptr, nullptr}` (no dialog).
  virtual PJ_borrowed_dialog_t getDialog() {
    return PJ_borrowed_dialog_t{nullptr, nullptr};
  }

  /// Return a pointer to a static plugin-exposed extension for @p id, or
  /// `nullptr` if unknown. CLAP-style reverse-direction capability query.
  /// Default returns nullptr. The returned pointer must be valid for the
  /// lifetime of this plugin instance.
  virtual const void* pluginExtension(std::string_view id) {
    (void)id;
    return nullptr;
  }

  template <typename CreateFn>
  static const PJ_data_source_vtable_t* vtableWithCreate(CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"id\"") != nullptr, "manifest must contain an \"id\" key");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    static const PJ_data_source_vtable_t vt = {
        PJ_DATA_SOURCE_PROTOCOL_VERSION,
        sizeof(PJ_data_source_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_capabilities,
        trampoline_bind,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_start,
        trampoline_stop,
        trampoline_pause,
        trampoline_resume,
        trampoline_poll,
        trampoline_current_state,
        trampoline_get_dialog,
        trampoline_get_plugin_extension,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] sdk::ServiceRegistry services() const {
    return service_registry_;
  }

  [[nodiscard]] const sdk::SourceWriteHostView& writeHost() const {
    return write_host_view_;
  }

  [[nodiscard]] const DataSourceRuntimeHostView& runtimeHost() const {
    return runtime_host_view_;
  }

  /// Optional — returns nullptr if the host did not register
  /// `pj.source_object_write.v1`. Media-capable sources check this before
  /// using it; scalar-only sources never touch it.
  [[nodiscard]] const sdk::SourceObjectWriteHostView* objectWriteHost() const {
    return object_write_host_view_.valid() ? &object_write_host_view_ : nullptr;
  }

  [[nodiscard]] bool writeHostBound() const {
    return write_host_view_.valid();
  }

  [[nodiscard]] bool runtimeHostBound() const {
    return runtime_host_view_.valid();
  }

 private:
  sdk::ServiceRegistry service_registry_{};
  sdk::SourceWriteHostView write_host_view_{PJ_source_write_host_t{}};
  sdk::SourceObjectWriteHostView object_write_host_view_{};
  DataSourceRuntimeHostView runtime_host_view_{};
  std::string config_buf_;

  /// Populate an out-param PJ_error_t with an inline-copied message.
  /// PJ_error_t owns its storage (fixed char buffers) so there is no
  /// lifetime dependency on this instance.
  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    sdk::fillError(out_error, code, domain, message);
  }

  // C ABI trampolines — exception-safe bridges between host vtable calls and
  // C++ virtuals. All are noexcept at the ABI boundary. Definitions live in
  // detail/data_source_trampolines.hpp.
  static void trampoline_destroy(void* ctx) noexcept;
  static uint64_t trampoline_capabilities(void* ctx) noexcept;
  static bool trampoline_bind(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept;
  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_start(void* ctx, PJ_error_t* out_error) noexcept;
  static void trampoline_stop(void* ctx) noexcept;
  static bool trampoline_pause(void* ctx, PJ_error_t* out_error) noexcept;
  static bool trampoline_resume(void* ctx, PJ_error_t* out_error) noexcept;
  static bool trampoline_poll(void* ctx, PJ_error_t* out_error) noexcept;
  static PJ_data_source_state_t trampoline_current_state(void* ctx) noexcept;
  static PJ_borrowed_dialog_t trampoline_get_dialog(void* ctx) noexcept;
  static const void* trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept;
};

}  // namespace PJ

// Out-of-line trampoline definitions — separated to keep the public API header concise.
#include "pj_base/sdk/detail/data_source_trampolines.hpp"

/**
 * Export a DataSourcePluginBase subclass as a shared-library plugin.
 *
 * Place at file scope (after the class definition). Generates the extern "C"
 * entry point `PJ_get_data_source_vtable` that the host resolves via dlsym.
 *
 * @param ClassName The DataSourcePluginBase subclass to instantiate.
 * @param manifest  String literal JSON manifest (must have "id", "name", and "version").
 */
#define PJ_DATA_SOURCE_PLUGIN(ClassName, manifest)                                                       \
  PJ_EXPORT_PLUGIN_ABI_VERSION(PJ_DATA_SOURCE_EXPORT)                                                    \
  extern "C" PJ_DATA_SOURCE_EXPORT const PJ_data_source_vtable_t* PJ_get_data_source_vtable() noexcept { \
    static const PJ_data_source_vtable_t* vt = PJ::DataSourcePluginBase::vtableWithCreate(               \
        []() noexcept -> void* {                                                                         \
          try {                                                                                          \
            return new ClassName();                                                                      \
          } catch (...) {                                                                                \
            return nullptr;                                                                              \
          }                                                                                              \
        },                                                                                               \
        manifest);                                                                                       \
    return vt;                                                                                           \
  }
