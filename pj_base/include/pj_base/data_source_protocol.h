/**
 * @file data_source_protocol.h
 * @brief C ABI protocol for DataSource plugins (version 4).
 *
 * v4 summary of changes vs v3:
 *   - Arrow C Data Interface at the write boundary: bulk loaders use
 *     SourceWriteHost::append_arrow_stream instead of per-row appends.
 *     See pj_base/plugin_data_api.h. append_arrow_ipc is removed.
 *   - Every vtable slot is PJ_NOEXCEPT. Trampolines that drop exceptions
 *     through the ABI boundary are now a compile-time error in C++.
 *   - Every slot carries a thread-class tag (// [main-thread], etc.).
 *
 * The host obtains the plugin's vtable via `PJ_get_data_source_vtable()`
 * and drives the plugin through: create -> bind(registry) -> load_config
 * -> start -> poll -> stop -> destroy.
 *
 * String ownership convention: plugin-returned `const char*` and
 * `PJ_string_view_t` pointers remain valid until the next call to the
 * same function on the same context. Hosts copy if they need to retain.
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
#define PJ_DATA_SOURCE_PROTOCOL_VERSION 4

/**
 * Minimum vtable size for v4.0 compatibility, pinned at v4.0 release.
 *
 * Loaders reject plugins whose `struct_size < PJ_DATA_SOURCE_MIN_VTABLE_SIZE`.
 * This constant MUST NOT GROW as new tail slots are appended in later
 * releases — bumping it rejects plugins compiled against older headers
 * (which legitimately report a smaller struct_size). Tail-slot additions
 * grow `sizeof(PJ_data_source_vtable_t)` but leave this floor alone.
 *
 * Reads of any slot added after v4.0 must be gated with PJ_HAS_TAIL_SLOT.
 *
 * Computed as `offsetof(last v4.0 slot) + sizeof(its function pointer)`.
 * Last v4.0 slot is `get_plugin_extension` (promoted from v3.1 tail).
 */
#define PJ_DATA_SOURCE_MIN_VTABLE_SIZE \
  (offsetof(PJ_data_source_vtable_t, get_plugin_extension) + sizeof(const void* (*)(void*, PJ_string_view_t)))

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
 * DataSource runtime host vtable — control-plane callbacks provided by the
 * host and delivered to the plugin via the service registry under the name
 * `"pj.runtime.v1"`.
 *
 * The plugin calls these to report progress, send diagnostic messages,
 * notify state changes, and (for delegated ingest) bind parsers and push
 * raw message payloads. All calls are made on the thread that called
 * start().
 *
 * Fallible calls take a `PJ_error_t* out_error` which the callee populates
 * on failure. Callers may pass NULL if they don't need the detail.
 * Informational calls (report_message, notify_state, etc.) are void and
 * cannot fail in a way the plugin can act on.
 */
typedef struct PJ_data_source_runtime_host_vtable_t {
  uint32_t protocol_version; /**< = 1 for the v4-era runtime host. */
  uint32_t struct_size;      /**< sizeof(PJ_data_source_runtime_host_vtable_t). */

  /** [thread-safe] Send a diagnostic message to the host (shown in UI log). */
  void (*report_message)(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t message) PJ_NOEXCEPT;

  /** [stream-thread] Begin a progress sequence. Returns false + error if the
   *  host cannot show progress. */
  bool (*progress_start)(
      void* ctx, PJ_string_view_t label, uint64_t total_steps, bool cancellable, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [stream-thread] Advance progress. Returns false to signal user
   * cancellation (when the sequence was started with cancellable=true).
   * This is NOT an error; no PJ_error_t is produced.
   */
  bool (*progress_update)(void* ctx, uint64_t current_step) PJ_NOEXCEPT;

  /** [stream-thread] End the current progress sequence. */
  void (*progress_finish)(void* ctx) PJ_NOEXCEPT;

  /** [thread-safe] Returns true if the host has requested the plugin to stop. */
  bool (*is_stop_requested)(void* ctx) PJ_NOEXCEPT;

  /** [thread-safe] Inform the host that the plugin has transitioned to @p state. */
  void (*notify_state)(void* ctx, PJ_data_source_state_t state) PJ_NOEXCEPT;

  /**
   * [thread-safe] Plugin-initiated stop. The plugin asks the host to
   * terminate it, specifying a terminal state (stopped or failed) and a
   * reason string.
   */
  void (*request_stop)(void* ctx, PJ_data_source_state_t terminal_state, PJ_string_view_t reason) PJ_NOEXCEPT;

  /**
   * [stream-thread] Bind (or look up) a parser for a topic. On success,
   * writes the handle to *out_handle and returns true. On failure, returns
   * false and (if out_error != NULL) populates it. Used for delegated
   * ingest mode.
   */
  bool (*ensure_parser_binding)(
      void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out_handle,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [stream-thread] Push a raw message payload for host-side parsing.
   * @p handle must have been obtained from ensure_parser_binding.
   * @p host_timestamp_ns is nanoseconds since the Unix epoch
   * (1970-01-01T00:00:00Z). Returns false + error on failure.
   */
  bool (*push_raw_message)(
      void* ctx, PJ_parser_binding_handle_t handle, int64_t host_timestamp_ns, PJ_bytes_view_t payload,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Display a modal message box to the user and wait for
   * their response. BLOCKS until the user closes the dialog. The host is
   * responsible for showing the dialog on the UI thread in a thread-safe
   * manner; the plugin may call from any thread and the host will marshal.
   *
   * @return The button that was clicked (a single PJ_message_box_buttons_t
   *         value), or -1 if the host does not support modal dialogs
   *         (e.g. headless mode).
   */
  int (*show_message_box)(
      void* ctx, PJ_message_box_type_t type, PJ_string_view_t title, PJ_string_view_t message, int buttons) PJ_NOEXCEPT;

  /**
   * [main-thread] List all available parser encodings.
   *
   * @return JSON array string of encoding names, e.g.
   *         ["json","cbor","protobuf"]. Host-owned string, valid until
   *         the next call to this function. Returns NULL if no parsers
   *         are loaded.
   */
  const char* (*list_available_encodings)(void* ctx)PJ_NOEXCEPT;
} PJ_data_source_runtime_host_vtable_t;

/** Fat pointer pairing a runtime host context with its vtable. */
typedef struct {
  void* ctx;
  const PJ_data_source_runtime_host_vtable_t* vtable;
} PJ_data_source_runtime_host_t;

/**
 * DataSource plugin vtable — the interface a plugin shared library exports.
 *
 * The host obtains this via the exported `PJ_get_data_source_vtable()`
 * symbol. Typical lifecycle (v4):
 *
 *   create  -> bind(registry) -> load_config (optional)
 *           -> start -> poll* -> stop -> destroy
 *
 * Fallible slots take a PJ_error_t* out-param which the callee populates
 * on failure. Callers may pass NULL to discard error detail. Every slot
 * is PJ_NOEXCEPT; exceptions from the implementation must be caught
 * inside the plugin and translated to the error return.
 */
typedef struct PJ_data_source_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_DATA_SOURCE_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_data_source_vtable_t). */

  /** [main-thread] Allocate a new plugin instance. Returns opaque context pointer. */
  void* (*create)(void)PJ_NOEXCEPT;
  /** [main-thread] Destroy an instance previously created by create(). */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;

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
  /** [main-thread] Return capability bitmask (PJ_DATA_SOURCE_CAPABILITY_* flags). */
  uint64_t (*capabilities)(void* ctx) PJ_NOEXCEPT;

  /**
   * [main-thread] Bind host-provided services.
   *
   * The plugin acquires whatever services it needs from @p registry
   * (write host, runtime host, optional services). The host must have
   * registered at least "pj.source_write.v1" and "pj.runtime.v1" before
   * calling bind on a DataSource plugin.
   *
   * Returns true on success. On failure, populates @p out_error (if
   * non-NULL) and returns false; the host should treat the plugin as
   * unusable and destroy it.
   *
   * Called exactly once between create() and the first lifecycle call.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Serialize plugin configuration to JSON.
   *
   * On success, returns true and writes to @p out_json a view over a
   * plugin-owned string that remains valid until the next call to this
   * function on the same ctx.
   */
  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [main-thread] Restore plugin configuration from JSON. */
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [main-thread] Begin data acquisition. May spawn stream threads internally. */
  bool (*start)(void* ctx, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [main-thread] Stop data acquisition. Must be idempotent. Failures are not reportable. */
  void (*stop)(void* ctx) PJ_NOEXCEPT;
  /** [main-thread] Pause a running source. Returns false + error if unsupported. */
  bool (*pause)(void* ctx, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [main-thread] Resume a paused source. Returns false + error if unsupported. */
  bool (*resume)(void* ctx, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [stream-thread] Called periodically by the host while running. */
  bool (*poll)(void* ctx, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [thread-safe] Return the plugin's current lifecycle state. */
  PJ_data_source_state_t (*current_state)(void* ctx) PJ_NOEXCEPT;

  /**
   * [main-thread] Return a typed borrowed reference to this source's
   * embedded dialog. The host must NOT call the dialog vtable's create()
   * or destroy() on a borrowed handle. Returns {NULL, NULL} if this
   * source has no dialog.
   */
  PJ_borrowed_dialog_t (*get_dialog)(void* ctx) PJ_NOEXCEPT;

  /**
   * [thread-safe] Query a plugin-exposed extension by reverse-DNS id.
   *
   * Returns a pointer to a static, plugin-owned POD (typically a tiny
   * vtable-like struct) valid for the lifetime of the plugin instance,
   * or NULL if the id is unknown. Hosts cast the pointer based on the
   * id they requested.
   *
   * Mirrors CLAP's `get_extension`. Lets plugins advertise extra
   * capabilities to hosts without bumping the family protocol version.
   *
   * Extension-ID convention: "pj.<name>.v<N>" for stable, or
   * "pj.experimental.<name>/draft-<N>" for unstable. A plugin may offer
   * multiple versions of the same capability (e.g. "pj.params.v1" and
   * "pj.params.v2") side by side.
   */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id)PJ_NOEXCEPT;

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */
} PJ_data_source_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_DATA_SOURCE_MIN_VTABLE_SIZE. */

/** Signature of the exported entry point: `PJ_get_data_source_vtable`. */
typedef const PJ_data_source_vtable_t* (*PJ_get_data_source_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_DATA_SOURCE_PROTOCOL_H
