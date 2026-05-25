#ifndef PJ_DIALOG_PROTOCOL_H
#define PJ_DIALOG_PROTOCOL_H
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DIALOG_PROTOCOL_VERSION 4

/*
 * Minimum vtable size for v4.0 compatibility, pinned at v4.0 release.
 * Loaders reject plugins whose `struct_size < PJ_DIALOG_MIN_VTABLE_SIZE`.
 * New slots may be appended at the tail without increasing this floor;
 * host reads of appended slots must be gated with PJ_HAS_TAIL_SLOT.
 */
#define PJ_DIALOG_MIN_VTABLE_SIZE \
  (offsetof(PJ_dialog_vtable_t, load_config) + sizeof(bool (*)(void*, PJ_string_view_t, PJ_error_t*)))

/* Export macro for plugin shared libraries */
#if defined(_WIN32)
#define PJ_DIALOG_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_DIALOG_EXPORT __attribute__((visibility("default")))
#else
#define PJ_DIALOG_EXPORT
#endif

/*
 * String ownership convention:
 *   - Strings returned by plugin functions are plugin-owned and valid
 *     until the next call to the same function on the same ctx.
 *   - Host-provided strings are valid only for the duration of the call.
 *   - Errors flow through PJ_error_t* out-parameters on fallible calls.
 *
 * v4: every slot is PJ_NOEXCEPT. Dialogs are always driven from the GUI
 * thread, so every slot is [main-thread].
 */

typedef struct PJ_dialog_vtable_t {
  uint32_t protocol_version; /* Must equal PJ_DIALOG_PROTOCOL_VERSION */
  uint32_t struct_size;

  /* [main-thread] Allocate a new dialog instance. */
  void* (*create)(void)PJ_NOEXCEPT;
  /* [main-thread] Destroy a dialog instance. */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;

  /* [main-thread] Stable plugin-owned strings. */
  const char* (*get_manifest)(void* ctx)PJ_NOEXCEPT;
  const char* (*get_ui_content)(void* ctx)PJ_NOEXCEPT;

  /* [main-thread] Plugin-owned, valid until next call to same function
   *               on same ctx. */
  const char* (*get_widget_data)(void* ctx)PJ_NOEXCEPT;

  /* [main-thread] Returns true if host should re-read get_widget_data()
   *               after this event. */
  bool (*on_widget_event)(void* ctx, const char* widget_name, const char* event_json, PJ_error_t* out_error)
      PJ_NOEXCEPT;
  /* [main-thread] Periodic tick driven by the host's UI event loop. */
  bool (*on_tick)(void* ctx, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Dialog result — not fallible. */
  void (*on_accepted)(void* ctx, const char* final_state_json) PJ_NOEXCEPT;
  void (*on_rejected)(void* ctx) PJ_NOEXCEPT;

  /* [main-thread] Configuration round-trip. */
  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) PJ_NOEXCEPT;
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [metadata] Optional static JSON manifest for metadata-only catalog
   * discovery. When present, the host reads this instead of instantiating
   * the dialog during scans. */
  const char* manifest_json;
} PJ_dialog_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_DIALOG_MIN_VTABLE_SIZE. */

/*
 * Every dialog plugin exports this symbol.
 * Returns a pointer to a static vtable, valid for the process lifetime.
 */
typedef const PJ_dialog_vtable_t* (*PJ_get_dialog_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* PJ_DIALOG_PROTOCOL_H */
