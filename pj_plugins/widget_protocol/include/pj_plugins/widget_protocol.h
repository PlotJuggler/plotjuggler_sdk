/**
 * @file widget_protocol.h
 * @brief C ABI protocol for Widget plugins (version 1).
 *
 * A Widget plugin contributes dockable panels whose content is a toolkit
 * widget object created by the plugin itself. Only the opaque pointer to that
 * object crosses the ABI (PJ_qwidget_t); the SDK never dereferences it and
 * never includes the toolkit's headers. Data (inputs, values at time t,
 * history), settings and lifecycle travel through the C structs below.
 *
 * Rules that shape the contract:
 *   - Every slot, host and plugin side, is called on the GUI thread only.
 *   - One plugin library may expose several widget types; `create(type)` makes
 *     one independent instance per open panel.
 *   - The plugin never sees topics, datasets or the data store: the host wires
 *     each input slot to a series and pushes the value at the tracker time.
 *   - A Widget plugin links the GUI toolkit; the SDK does not. The host
 *     compares the toolkit version recorded in build_info() against its own
 *     before calling create_qwidget().
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_WIDGET_PROTOCOL_H
#define PJ_WIDGET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"
#include "pj_base/toolbox_protocol.h" /* PJ_toolbox_message_level_t */

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version of PJ_widget_vtable_t. Host and plugin must agree. */
#define PJ_WIDGET_PLUGIN_PROTOCOL_VERSION 1

/** Protocol version of the "pj.widget_host.v1" service vtable. */
#define PJ_WIDGET_HOST_PROTOCOL_VERSION 1

/**
 * Minimum vtable size for v1.0 compatibility, pinned at the first release.
 *
 * Loaders reject plugins whose `struct_size < PJ_WIDGET_MIN_VTABLE_SIZE`.
 * MUST NOT GROW when new tail slots are appended. Last v1.0 slot is
 * `get_plugin_extension`.
 */
#define PJ_WIDGET_MIN_VTABLE_SIZE \
  (offsetof(PJ_widget_vtable_t, get_plugin_extension) + sizeof(const void* (*)(void*, PJ_string_view_t)))

#if defined(_WIN32)
#define PJ_WIDGET_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_WIDGET_EXPORT __attribute__((visibility("default")))
#else
#define PJ_WIDGET_EXPORT
#endif

/**
 * Opaque pointer to the plugin-created widget object (a Qt widget). The host
 * casts it to its own widget type; the SDK and the plugin ABI never
 * dereference it. NULL means "no widget".
 */
typedef void* PJ_qwidget_t;

/** What a value delivered to an input slot looks like. */
typedef enum {
  PJ_WIDGET_INPUT_NUMBER = 0,               /* scalar series, delivered as `number` */
  PJ_WIDGET_INPUT_TEXT = 1,                 /* string-valued series (states such as "ARMED") */
  PJ_WIDGET_INPUT_OBJECT = 2,               /* object topic, delivered as encoded `bytes` + `type_name` */
  PJ_WIDGET_INPUT_FORCE_INT32 = 0x7FFFFFFF, /* pin 4-byte width; not a real kind */
} PJ_widget_input_kind_t;

/**
 * One named input of a widget, as returned by PJ_widget_vtable_t::inputs.
 *
 * ABI-APPENDABLE: the reader honors `struct_size`; fields past it are absent.
 * String views point into plugin-owned storage with the lifetime stated on
 * `inputs`. `name` is the stable identity of the input: the host keeps a
 * wiring across `load_config`/`inputs_changed` by matching names, never by
 * position.
 */
typedef struct {
  uint32_t struct_size;   /* sizeof(PJ_widget_input_t) as compiled by the plugin */
  PJ_string_view_t name;  /* stable, non-empty, unique within the widget */
  PJ_string_view_t label; /* display text; empty = use `name` */
  PJ_string_view_t unit;  /* display unit, may be empty */
  PJ_widget_input_kind_t kind;
  bool multiple; /* false: one series, a new drop replaces it. true: any number, one slot each */
} PJ_widget_input_t;

/** Kind of a PJ_widget_value_t; selects which of its payload fields is valid. */
typedef enum {
  PJ_WIDGET_VALUE_NONE = 0,   /* no sample at or before t, or input not wired: no payload */
  PJ_WIDGET_VALUE_NUMBER = 1, /* `number` */
  PJ_WIDGET_VALUE_TEXT = 2,   /* `text` */
  PJ_WIDGET_VALUE_OBJECT = 3, /* `bytes` + `type_name` */
  PJ_WIDGET_VALUE_FORCE_INT32 = 0x7FFFFFFF,
} PJ_widget_value_kind_t;

/**
 * Value of one wire (input + slot) at a time.
 *
 * ABI-APPENDABLE. Every pointer inside (text, bytes, type_name) is valid only
 * for the duration of the call that delivers it (on_update, or the history
 * sink); a widget keeps what it needs by copying.
 */
typedef struct {
  uint32_t struct_size;        /* sizeof(PJ_widget_value_t) as compiled by the host */
  uint32_t input;              /* index into the array last returned by inputs() */
  uint32_t slot;               /* 0 for a non-`multiple` input; stable per wire for `multiple` ones */
  PJ_widget_value_kind_t kind; /* selects the payload below */
  double time;                 /* time of the sample, in the host's display time axis, seconds */
  double number;               /* valid when kind == NUMBER */
  PJ_string_view_t text;       /* valid when kind == TEXT */
  PJ_bytes_view_t bytes;       /* valid when kind == OBJECT: the object record payload */
  PJ_string_view_t type_name;  /* valid when kind == OBJECT: type name to pick the codec */
  PJ_string_view_t label;      /* host's display name of the wired series ("topic/field"); set for every
                                  kind, NONE included, so a widget can list its wires; may be empty */
} PJ_widget_value_t;

/** Bit values for PJ_widget_build_info_t::flags. */
enum {
  PJ_WIDGET_BUILD_DEBUG = 1u << 0, /* the plugin was built in a debug configuration */
};

/**
 * Toolkit build identity of the plugin, readable before any instance exists.
 *
 * The host compares the version (same major.minor; patch ignored) and, where
 * debug and release runtimes are not interchangeable, the debug flag and the
 * compiler family, and refuses to create the widget on mismatch.
 *
 * ABI-APPENDABLE. The returned object and its strings have static storage.
 */
typedef struct {
  uint32_t struct_size; /* sizeof(PJ_widget_build_info_t) as compiled by the plugin */
  uint32_t qt_major;    /* toolkit version the plugin was compiled against */
  uint32_t qt_minor;
  uint32_t qt_patch;
  uint32_t flags;                  /* PJ_WIDGET_BUILD_* bits */
  PJ_string_view_t compiler;       /* "gcc" | "clang" | "msvc" */
  PJ_string_view_t qt_version_str; /* full version string, for messages only */
} PJ_widget_build_info_t;

/** Sink invoked once per sample inside PJ_widget_host_vtable_t::query_history. */
typedef void (*PJ_widget_value_sink_fn)(void* user, const PJ_widget_value_t* value) PJ_NOEXCEPT;

/**
 * Widget host vtable, delivered as the "pj.widget_host.v1" service. One
 * service instance exists per open panel, so `ctx` identifies the panel.
 * Every slot is [main-thread].
 */
typedef struct PJ_widget_host_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /**
   * [main-thread] Read the history of one wire. Not pushed: the widget asks
   * for it from on_update (never from a paint handler) and keeps a copy.
   *
   * The host calls `sink` once per sample, in ascending time, synchronously
   * and before returning; `value` and everything it points to are valid only
   * inside that call. `from`/`to` are display-time seconds, inclusive. At most
   * `max_samples` samples are delivered (0 = host default cap); when the range
   * holds more, which ones are omitted is host policy. Returns false with
   * `out_error` filled when the input/slot is not wired or the call is not
   * allowed; the sink was not called in that case.
   */
  bool (*query_history)(
      void* ctx, uint32_t input, uint32_t slot, double from, double to, uint32_t max_samples,
      PJ_widget_value_sink_fn sink, void* user, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] The widget's inputs() changed (typically after a settings
   * change). The host re-reads inputs() and reconciles by name: the wiring of
   * a surviving name is kept, a vanished one is dropped, a new one starts
   * empty. Never call it from inside on_update, nor from inputs() itself.
   */
  void (*notify_inputs_changed)(void* ctx) PJ_NOEXCEPT;

  /** [main-thread] Diagnostic text for the host's message list; copied before return. */
  void (*report_widget_message)(void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t message) PJ_NOEXCEPT;
} PJ_widget_host_vtable_t;
/* ABI-APPENDABLE: new slots go at the tail; plugin-side reads guard with PJ_HAS_TAIL_SLOT. */

/** Fat pointer to the widget host service. */
typedef struct {
  void* ctx;
  const PJ_widget_host_vtable_t* vtable;
} PJ_widget_host_t;

/**
 * Widget plugin vtable (v1).
 *
 * Lifecycle of one panel: create(type) -> bind(registry) -> [load_config]
 * -> inputs() -> create_qwidget(parent) -> { on_update, load_config, inputs()
 * }* -> save_config -> destroy. Every slot is PJ_NOEXCEPT and [main-thread].
 */
typedef struct PJ_widget_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /**
   * [main-thread] Allocate one instance of the widget type `type` (an entry of
   * the manifest's `widgets[].type`). Returns NULL for an unknown type.
   */
  void* (*create)(PJ_string_view_t type)PJ_NOEXCEPT;

  /**
   * [main-thread] Destroy an instance. Synchronous, and the point where the
   * plugin deletes the widget object it created if that object is still
   * alive: the host calls setParent(nullptr) on it immediately before, so no
   * parent frees it behind the plugin's back. Must tolerate the widget object
   * having been deleted already.
   */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;

  /**
   * Static JSON manifest (compile-time constant). Required keys: "id",
   * "name", "version", and a non-empty "widgets" array of
   * {"type","name","icon","settings"} objects; optional "qt_modules" array.
   */
  const char* manifest_json;

  /** [main-thread] Static toolkit build identity; never NULL for a valid plugin. */
  const PJ_widget_build_info_t* (*build_info)(void)PJ_NOEXCEPT;

  /**
   * [main-thread] Bind host services. The host registers "pj.widget_host.v1";
   * "pj.settings.v1" may also be present.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Current inputs. Writes the address of a plugin-owned array
   * to `*out_inputs` and returns its length. The array and the strings in it
   * stay valid until the next inputs(), load_config() or destroy() on this
   * instance. The list never changes in the middle of on_update().
   */
  uint32_t (*inputs)(void* ctx, const PJ_widget_input_t** out_inputs) PJ_NOEXCEPT;

  /**
   * [main-thread] Create the widget object as a child of `parent` (the
   * host's widget, possibly NULL). Called at most once per instance. The
   * plugin owns the object; see destroy(). Returns NULL with `out_error`
   * filled on failure.
   */
  PJ_qwidget_t (*create_qwidget)(void* ctx, PJ_qwidget_t parent, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] New values for the wired inputs at time `time` (display time
   * axis, seconds). `values[0..count)` holds one entry per wire (input, slot),
   * with kind NONE where there is no sample at or before `time`. Not called
   * when nothing changed. `values` is valid only during the call.
   */
  void (*on_update)(void* ctx, const PJ_widget_value_t* values, uint32_t count, double time) PJ_NOEXCEPT;

  /**
   * [main-thread] Serialize the widget's internal state to JSON. The returned
   * view is plugin-owned and valid until the next call on this instance.
   */
  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Apply a configuration JSON. The host passes the user's
   * settings (the keys declared in the manifest) as well as any state that
   * save_config produced; restoring a layout and editing a setting are the
   * same call. `config_json` is valid only during the call.
   */
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [main-thread] Plugin-exposed extension by reverse-DNS id, or NULL. */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id)PJ_NOEXCEPT;

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */
} PJ_widget_vtable_t;
/* ABI-APPENDABLE: see PJ_WIDGET_MIN_VTABLE_SIZE. */

typedef const PJ_widget_vtable_t* (*PJ_get_widget_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* PJ_WIDGET_PROTOCOL_H */
