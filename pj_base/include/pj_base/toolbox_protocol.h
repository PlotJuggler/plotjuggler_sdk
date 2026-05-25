/**
 * @file toolbox_protocol.h
 * @brief C ABI protocol for Toolbox plugins (version 4).
 *
 * v4 summary:
 *   - Toolbox host (pj.toolbox_write.v1) now uses Arrow C Data Interface
 *     for bulk write (append_arrow_stream) and read (read_series_arrow).
 *     The materialised-vector read_series and byte-based append_arrow_ipc
 *     are removed. See pj_base/plugin_data_api.h.
 *   - Every vtable slot is PJ_NOEXCEPT and carries a thread-class tag.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

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
#define PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION 4

/**
 * Minimum vtable size for v4.0 compatibility, pinned at v4.0 release.
 *
 * Loaders reject plugins whose `struct_size < PJ_TOOLBOX_MIN_VTABLE_SIZE`.
 * MUST NOT GROW when new tail slots are appended. See PJ_ABI_VERSION comment
 * in plugin_data_api.h for the rationale.
 *
 * Last v4.0 slot is `get_plugin_extension`.
 */
#define PJ_TOOLBOX_MIN_VTABLE_SIZE \
  (offsetof(PJ_toolbox_vtable_t, get_plugin_extension) + sizeof(const void* (*)(void*, PJ_string_view_t)))

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

enum {
  PJ_TOOLBOX_CAPABILITY_HAS_DIALOG = 1ull << 0,
  PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG = 1ull << 1,
};

/**
 * Toolbox runtime host vtable — control-plane callbacks, delivered as the
 * "pj.toolbox_runtime.v1" service.
 */
typedef struct PJ_toolbox_runtime_host_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /** [thread-safe] Send a diagnostic message to the host (shown in UI log). */
  void (*report_message)(void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t message) PJ_NOEXCEPT;

  /** [thread-safe] Notify the host that data has been modified; host refreshes UI. */
  void (*notify_data_changed)(void* ctx) PJ_NOEXCEPT;
} PJ_toolbox_runtime_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_toolbox_runtime_host_vtable_t* vtable;
} PJ_toolbox_runtime_host_t;

/**
 * Toolbox plugin vtable (v4).
 *
 * Typical lifecycle: create -> bind(registry) -> load_config (optional)
 *                    -> [user interacts] -> save_config -> destroy.
 * Every slot is PJ_NOEXCEPT.
 */
typedef struct PJ_toolbox_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /** [main-thread] Allocate a new toolbox instance. */
  void* (*create)(void)PJ_NOEXCEPT;
  /** [main-thread] Destroy an instance previously created by create(). */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;

  /**
   * Static JSON manifest. Compile-time constant.
   *
   * Required keys:
   *   "id"      — stable plugin identifier (string). Used by the host catalog
   *               and the marketplace; must be unique per plugin.
   *   "name"    — human-readable plugin name (string).
   *   "version" — semver version string (string).
   */
  const char* manifest_json;
  /** [main-thread] Return capability bitmask (PJ_TOOLBOX_CAPABILITY_* flags). */
  uint64_t (*capabilities)(void* ctx) PJ_NOEXCEPT;

  /**
   * [main-thread] Bind host services. The host registers at least
   * "pj.toolbox_write.v1" and "pj.toolbox_runtime.v1"; optional services
   * such as "pj.colormap.v1" may also be present.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [main-thread] Serialize toolbox configuration to JSON. */
  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [main-thread] Restore toolbox configuration from JSON. */
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Return a typed borrowed reference to this toolbox's
   * dialog. The host must NOT call the dialog vtable's create() or
   * destroy() on a borrowed handle. Returns {NULL, NULL} if this toolbox
   * has no dialog.
   */
  PJ_borrowed_dialog_t (*get_dialog)(void* ctx) PJ_NOEXCEPT;

  /** [main-thread] Notify the plugin that new records have been appended
   *  to the datastore. */
  void (*on_data_changed)(void* ctx) PJ_NOEXCEPT;

  /** [thread-safe] Query a plugin-exposed extension by reverse-DNS id.
   *  See PJ_data_source_vtable_t::get_plugin_extension for the full
   *  contract and ID-versioning convention. */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id)PJ_NOEXCEPT;

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */
} PJ_toolbox_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_TOOLBOX_MIN_VTABLE_SIZE. */

typedef const PJ_toolbox_vtable_t* (*PJ_get_toolbox_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_TOOLBOX_PROTOCOL_H
