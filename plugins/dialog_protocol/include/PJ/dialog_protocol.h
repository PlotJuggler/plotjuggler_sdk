#ifndef PJ_DIALOG_PROTOCOL_H
#define PJ_DIALOG_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DIALOG_PROTOCOL_VERSION 1

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
 *
 * - Strings returned by plugin functions are OWNED BY THE PLUGIN.
 * - Pointer is valid until the next call to the SAME function on the SAME context.
 * - Host must copy if it needs to retain the string.
 * - Host-provided strings (event_json, config_json, final_state_json) are valid
 *   only for the duration of the call.
 */

typedef struct {
  uint32_t protocol_version; /* Must equal PJ_DIALOG_PROTOCOL_VERSION */
  uint32_t struct_size;      /* sizeof(PJ_dialog_vtable_t) — for safe ABI extension */

  /* Lifecycle */
  void* (*create)(void);
  void (*destroy)(void* ctx);

  /* Plugin-owned, stable pointer (does not change between calls) */
  const char* (*get_manifest)(void* ctx);   /* JSON */
  const char* (*get_ui_content)(void* ctx); /* Qt Designer XML */

  /* Plugin-owned, valid until next call to same function on same ctx */
  const char* (*get_widget_data)(void* ctx); /* JSON */

  /* Returns true if host should re-read get_widget_data() */
  bool (*on_widget_event)(void* ctx, const char* widget_name, const char* event_json);
  bool (*on_tick)(void* ctx);

  /* Dialog result */
  void (*on_accepted)(void* ctx, const char* final_state_json);
  void (*on_rejected)(void* ctx);

  /* Config persistence — same ownership as get_widget_data */
  const char* (*save_config)(void* ctx);
  bool (*load_config)(void* ctx, const char* config_json);

  /* Error reporting — NULL if no error. Plugin-owned, valid until next call. */
  const char* (*get_last_error)(void* ctx);
} PJ_dialog_vtable_t;

/*
 * Every dialog plugin exports this symbol.
 * Returns a pointer to a static vtable. The pointer is valid for the process lifetime.
 *
 * Usage: const PJ_dialog_vtable_t* vt = PJ_get_dialog_vtable();
 */
typedef const PJ_dialog_vtable_t* (*PJ_get_dialog_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* PJ_DIALOG_PROTOCOL_H */
