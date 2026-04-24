/**
 * @file toolbox_protocol.h
 * @brief C ABI protocol for Toolbox plugins (version 1).
 *
 * Defines the vtable contracts that a Toolbox shared library must export.
 * The host loads the library, calls PJ_get_toolbox_vtable() to obtain a
 * vtable, then drives the plugin through create/bind/interact/destroy.
 *
 * Two host bindings exist:
 *  - **Toolbox host** (PJ_toolbox_host_t, from plugin_data_api.h): data-plane
 *    callbacks for reading/writing records in the host's storage engine.
 *  - **Runtime host** (PJ_toolbox_runtime_host_t, below): control-plane
 *    callbacks for diagnostic messages and data-change notifications.
 *
 * String ownership convention: plugin-returned `const char*` pointers remain
 * valid until the next call to the same function on the same context.
 */
#ifndef PJ_TOOLBOX_PROTOCOL_H
#define PJ_TOOLBOX_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION 1

#if defined(_WIN32)
#define PJ_TOOLBOX_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_TOOLBOX_EXPORT __attribute__((visibility("default")))
#else
#define PJ_TOOLBOX_EXPORT
#endif

/** Severity level for plugin-to-host diagnostic messages. */
typedef enum {
  PJ_TOOLBOX_MESSAGE_INFO = 0,
  PJ_TOOLBOX_MESSAGE_WARNING = 1,
  PJ_TOOLBOX_MESSAGE_ERROR = 2,
} PJ_toolbox_message_level_t;

/**
 * Capability flags returned by the plugin's capabilities() function.
 * Combine with bitwise OR.
 */
enum {
  PJ_TOOLBOX_CAPABILITY_HAS_DIALOG = 1ull << 0,       /**< Plugin provides a persistent UI panel. */
  PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG = 1ull << 1, /**< Dialog should be shown non-modally so the host window remains interactive (e.g. for drag-and-drop). */
};

/**
 * Runtime host vtable — control-plane callbacks provided by the host.
 *
 * The plugin calls these to send diagnostic messages and notify the host
 * when data has been modified so the UI can refresh.
 */
typedef struct PJ_toolbox_runtime_host_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_toolbox_runtime_host_vtable_t). */

  /** Returns the last host-side error message, or NULL if none. */
  const char* (*get_last_error)(void* ctx);

  /** Send a diagnostic message to the host (shown in UI log). */
  void (*report_message)(void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t message);

  /** Notify the host that the plugin has modified data; host should refresh UI. */
  void (*notify_data_changed)(void* ctx);
} PJ_toolbox_runtime_host_vtable_t;

/** Fat pointer pairing a runtime host context with its vtable. */
typedef struct {
  void* ctx;
  const PJ_toolbox_runtime_host_vtable_t* vtable;
} PJ_toolbox_runtime_host_t;

/**
 * Toolbox plugin vtable — the interface a plugin shared library exports.
 *
 * The host obtains this via the exported PJ_get_toolbox_vtable() symbol.
 * Typical lifecycle: create -> bind hosts -> load config -> [user interacts] -> save config -> destroy.
 */
typedef struct PJ_toolbox_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_toolbox_vtable_t). */

  /** Allocate a new plugin instance. Returns opaque context pointer. */
  void* (*create)(void);
  /** Destroy an instance previously created by create(). */
  void (*destroy)(void* ctx);

  /**
   * Static JSON manifest. Compile-time constant string literal.
   *
   * Required keys:
   *   "name"    — human-readable plugin name (string).
   *   "version" — semver version string (string).
   *
   * Optional keys:
   *   "description" — short description of the plugin (string).
   */
  const char* manifest_json;
  /** Return capability bitmask (PJ_TOOLBOX_CAPABILITY_* flags). */
  uint64_t (*capabilities)(void* ctx);

  /** Bind the data-plane toolbox host. Must be called before interaction. */
  bool (*bind_toolbox_host)(void* ctx, PJ_toolbox_host_t toolbox_host);
  /** Bind the control-plane runtime host. Must be called before interaction. */
  bool (*bind_runtime_host)(void* ctx, PJ_toolbox_runtime_host_t runtime_host);
  /**
   * Bind the optional colormap registry service.
   *
   * Called by the host after bind_toolbox_host when a registry is available.
   * Plugins that don't publish colormaps can leave this NULL; the host checks
   * for NULL before calling. Returns true on success.
   */
  bool (*bind_colormap_registry)(void* ctx, PJ_colormap_registry_t registry);

  /** Serialize plugin configuration to JSON. Plugin-owned string. */
  const char* (*save_config)(void* ctx);
  /** Restore plugin configuration from JSON. */
  bool (*load_config)(void* ctx, const char* config_json);

  /**
   * Returns a context pointer for the plugin's dialog.
   * The returned pointer is owned by the Toolbox instance — the host
   * must NOT destroy it independently. Returns NULL if no dialog.
   */
  void* (*get_dialog_context)(void* ctx);

  /** Return the last error message, or NULL if none. Plugin-owned string. */
  const char* (*get_last_error)(void* ctx);

  /** Notify the plugin that new records have been appended to the datastore. */
  void (*on_data_changed)(void* ctx);
} PJ_toolbox_vtable_t;

/** Signature of the exported entry point: `PJ_get_toolbox_vtable`. */
typedef const PJ_toolbox_vtable_t* (*PJ_get_toolbox_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_TOOLBOX_PROTOCOL_H
