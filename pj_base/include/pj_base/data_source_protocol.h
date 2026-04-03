/**
 * @file data_source_protocol.h
 * @brief C ABI protocol for DataSource plugins (version 2).
 *
 * Defines the vtable contracts that a DataSource shared library must export.
 * The host loads the library, calls PJ_get_data_source_vtable() to obtain a
 * vtable, then drives the plugin instance through create/bind/start/poll/stop.
 *
 * Two host bindings exist:
 *  - **Write host** (PJ_source_write_host_t, from plugin_data_api.h): data-plane
 *    callbacks for writing records into the host's storage engine.
 *  - **Runtime host** (PJ_data_source_runtime_host_t, below): control-plane
 *    callbacks for progress, messages, state notifications, and parser delegation.
 *
 * String ownership convention: plugin-returned `const char*` pointers remain
 * valid until the next call to the same function on the same context.
 */
#ifndef PJ_DATA_SOURCE_PROTOCOL_H
#define PJ_DATA_SOURCE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_DATA_SOURCE_PROTOCOL_VERSION 2

#if defined(_WIN32)
#define PJ_DATA_SOURCE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_DATA_SOURCE_EXPORT __attribute__((visibility("default")))
#else
#define PJ_DATA_SOURCE_EXPORT
#endif

/**
 * Plugin lifecycle state machine.
 *
 * Valid transitions:
 *   idle -> configuring -> starting -> running -> stopping -> stopped
 *   running -> paused -> running  (if SUPPORTS_PAUSE capability set)
 *   any -> failed  (terminal)
 *   stopped is terminal — create a new instance to restart.
 */
typedef enum {
  PJ_DATA_SOURCE_STATE_IDLE = 0,
  PJ_DATA_SOURCE_STATE_CONFIGURING = 1,
  PJ_DATA_SOURCE_STATE_STARTING = 2,
  PJ_DATA_SOURCE_STATE_RUNNING = 3,
  PJ_DATA_SOURCE_STATE_PAUSED = 4,
  PJ_DATA_SOURCE_STATE_STOPPING = 5,
  PJ_DATA_SOURCE_STATE_STOPPED = 6, /**< Terminal. */
  PJ_DATA_SOURCE_STATE_FAILED = 7,  /**< Terminal. */
} PJ_data_source_state_t;

/** Severity level for plugin-to-host diagnostic messages. */
typedef enum {
  PJ_DATA_SOURCE_MESSAGE_INFO = 0,
  PJ_DATA_SOURCE_MESSAGE_WARNING = 1,
  PJ_DATA_SOURCE_MESSAGE_ERROR = 2,
} PJ_data_source_message_level_t;

/** Type of message box to display. Determines the icon shown. */
typedef enum {
  PJ_MESSAGE_BOX_INFO = 0,     /**< Information icon (i). */
  PJ_MESSAGE_BOX_WARNING = 1,  /**< Warning icon (!). */
  PJ_MESSAGE_BOX_ERROR = 2,    /**< Error/critical icon (X). */
  PJ_MESSAGE_BOX_QUESTION = 3, /**< Question icon (?). */
} PJ_message_box_type_t;

/**
 * Standard buttons for message boxes.
 * Combine with bitwise OR: PJ_MSG_BTN_OK | PJ_MSG_BTN_CANCEL
 */
typedef enum {
  PJ_MSG_BTN_OK = 0x01,
  PJ_MSG_BTN_CANCEL = 0x02,
  PJ_MSG_BTN_YES = 0x04,
  PJ_MSG_BTN_NO = 0x08,
  PJ_MSG_BTN_CONTINUE = 0x10,
  PJ_MSG_BTN_ABORT = 0x20,
  PJ_MSG_BTN_RETRY = 0x40,
  PJ_MSG_BTN_IGNORE = 0x80,
} PJ_message_box_buttons_t;

/**
 * Capability flags returned by the plugin's capabilities() function.
 * Combine with bitwise OR. The host uses these to decide which features to
 * enable (e.g. showing a pause button, calling poll(), offering parser UI).
 */
enum {
  PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT = 1ull << 0,     /**< One-shot file import. */
  PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM = 1ull << 1, /**< Long-lived streaming. */
  PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST = 1ull << 2,     /**< Plugin writes decoded data via write host. */
  PJ_DATA_SOURCE_CAPABILITY_DELEGATED_INGEST = 1ull << 3,  /**< Plugin pushes raw bytes for host-side parsing. */
  PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE = 1ull << 4,    /**< pause()/resume() are implemented. */
  PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG = 1ull << 5,        /**< Plugin provides a configuration dialog. */
};

/** Opaque handle returned by ensure_parser_binding, used with push_raw_message. */
typedef struct {
  uint32_t id;
} PJ_parser_binding_handle_t;

/**
 * Request to bind (or look up) a parser for a given topic.
 * All string views must remain valid for the duration of the call.
 */
typedef struct {
  PJ_string_view_t topic_name;         /**< Topic the parser will decode for. */
  PJ_string_view_t parser_encoding;    /**< Encoding name, e.g. "json", "protobuf". */
  PJ_string_view_t type_name;          /**< Message type name (encoding-specific). */
  PJ_bytes_view_t schema;              /**< Optional schema bytes (e.g. FileDescriptorSet). */
  PJ_string_view_t parser_config_json; /**< Optional JSON config for the parser. */
} PJ_parser_binding_request_t;

/**
 * Runtime host vtable — control-plane callbacks provided by the host.
 *
 * The plugin calls these to report progress, send diagnostic messages,
 * notify state changes, and (for delegated ingest) bind parsers and push
 * raw message payloads. All calls are made on the thread that called start().
 */
typedef struct PJ_data_source_runtime_host_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_DATA_SOURCE_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_data_source_runtime_host_vtable_t). */

  /** Returns the last host-side error message, or NULL if none. */
  const char* (*get_last_error)(void* ctx);

  /** Send a diagnostic message to the host (shown in UI log). */
  void (*report_message)(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t message);

  /** Begin a progress sequence. Returns false if the host cannot show progress. */
  bool (*progress_start)(void* ctx, PJ_string_view_t label, uint64_t total_steps, bool cancellable);

  /** Advance progress. Returns false if the user cancelled (when cancellable). */
  bool (*progress_update)(void* ctx, uint64_t current_step);

  /** End the current progress sequence. */
  void (*progress_finish)(void* ctx);

  /** Returns true if the host has requested the plugin to stop. */
  bool (*is_stop_requested)(void* ctx);

  /** Inform the host that the plugin has transitioned to @p state. */
  void (*notify_state)(void* ctx, PJ_data_source_state_t state);

  /**
   * Plugin-initiated stop. The plugin asks the host to terminate it,
   * specifying a terminal state (stopped or failed) and a reason string.
   */
  void (*request_stop)(void* ctx, PJ_data_source_state_t terminal_state, PJ_string_view_t reason);

  /**
   * Bind (or look up) a parser for a topic. On success, writes the handle
   * to *out_handle. Returns false on failure (check get_last_error).
   * Used for delegated ingest mode.
   */
  bool (*ensure_parser_binding)(
      void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out_handle);

  /**
   * Push a raw message payload for host-side parsing.
   * @p handle must have been obtained from ensure_parser_binding.
   * @p host_timestamp_ns is nanoseconds since the Unix epoch (1970-01-01T00:00:00Z).
   */
  bool (*push_raw_message)(
      void* ctx, PJ_parser_binding_handle_t handle, int64_t host_timestamp_ns, PJ_bytes_view_t payload);

  /**
   * Display a modal message box to the user and wait for their response.
   *
   * This function BLOCKS until the user closes the dialog. The host is
   * responsible for showing the dialog on the UI thread in a thread-safe manner.
   *
   * @param ctx      Host context.
   * @param type     Dialog type (determines icon): info, warning, error, question.
   * @param title    Window title for the dialog.
   * @param message  Message text to display (may contain newlines).
   * @param buttons  Bitmask of PJ_message_box_buttons_t values.
   *
   * @return The button that was clicked (a single PJ_message_box_buttons_t value),
   *         or -1 if the host does not support modal dialogs (e.g., headless mode).
   *
   * @note If buttons == 0, the host should use PJ_MSG_BTN_OK as default.
   * @note In headless mode, the host may return the "positive" button by default
   *       (OK, Yes, Continue) or -1.
   */
  int (*show_message_box)(
      void* ctx, PJ_message_box_type_t type, PJ_string_view_t title, PJ_string_view_t message, int buttons);

  /**
   * List all available parser encodings.
   *
   * @param ctx Host context.
   * @return JSON array string of encoding names, e.g. ["json","cbor","protobuf"].
   *         Host-owned string, valid until the next call to this function.
   *         Returns NULL if no parsers are loaded.
   *
   * @note Plugins can use this to dynamically populate encoding selection UI
   *       instead of hardcoding a static list.
   * @note Check struct_size >= offsetof(..., list_available_encodings) + sizeof(ptr)
   *       before calling, as older hosts may not have this field.
   */
  const char* (*list_available_encodings)(void* ctx);
} PJ_data_source_runtime_host_vtable_t;

/** Fat pointer pairing a runtime host context with its vtable. */
typedef struct {
  void* ctx;
  const PJ_data_source_runtime_host_vtable_t* vtable;
} PJ_data_source_runtime_host_t;

/**
 * DataSource plugin vtable — the interface a plugin shared library exports.
 *
 * The host obtains this via the exported PJ_get_data_source_vtable() symbol.
 * Typical lifecycle: create -> bind hosts -> load config -> start -> poll -> stop -> destroy.
 */
typedef struct PJ_data_source_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_DATA_SOURCE_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_data_source_vtable_t). */

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
   *   "description"     — short description of the plugin (string).
   *   "file_extensions" — array of file extensions this source handles,
   *                       e.g. [".csv", ".tsv"]. Plugins declaring
   *                       FINITE_IMPORT SHOULD include this so the host
   *                       can build file-dialog filters without
   *                       instantiating the plugin.
   */
  const char* manifest_json;
  /** Return capability bitmask (PJ_DATA_SOURCE_CAPABILITY_* flags). */
  uint64_t (*capabilities)(void* ctx);

  /** Bind the data-plane write host. Must be called before start(). */
  bool (*bind_write_host)(void* ctx, PJ_source_write_host_t write_host);
  /** Bind the control-plane runtime host. Must be called before start(). */
  bool (*bind_runtime_host)(void* ctx, PJ_data_source_runtime_host_t runtime_host);

  /** Serialize plugin configuration to JSON. Plugin-owned string. */
  const char* (*save_config)(void* ctx);
  /** Restore plugin configuration from JSON. */
  bool (*load_config)(void* ctx, const char* config_json);

  /** Begin data acquisition. Returns false on failure (check get_last_error). */
  bool (*start)(void* ctx);
  /** Stop data acquisition. Must be idempotent. */
  void (*stop)(void* ctx);
  /** Pause a running source. Returns false if unsupported. */
  bool (*pause)(void* ctx);
  /** Resume a paused source. Returns false if unsupported. */
  bool (*resume)(void* ctx);
  /** Called periodically by the host while running. Returns false on error. */
  bool (*poll)(void* ctx);

  /** Return the plugin's current lifecycle state. */
  PJ_data_source_state_t (*current_state)(void* ctx);

  /** Return the last error message, or NULL if none. Plugin-owned string. */
  const char* (*get_last_error)(void* ctx);

  /**
   * Returns a context pointer usable with the dialog protocol vtable.
   * The returned pointer is owned by the DataSource instance — the host
   * must NOT call the dialog vtable's create() or destroy() on it.
   * Returns NULL if this source has no dialog.
   */
  void* (*get_dialog_context)(void* ctx);
} PJ_data_source_vtable_t;

/** Signature of the exported entry point: `PJ_get_data_source_vtable`. */
typedef const PJ_data_source_vtable_t* (*PJ_get_data_source_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_DATA_SOURCE_PROTOCOL_H
