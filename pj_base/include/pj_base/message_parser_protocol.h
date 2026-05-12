/**
 * @file message_parser_protocol.h
 * @brief C ABI protocol for MessageParser plugins (version 4).
 *
 * v4 summary of changes vs v3:
 *   - Every vtable slot is PJ_NOEXCEPT and carries a thread-class tag.
 *   - Parser write host (pj.parser_write.v1) no longer has
 *     append_arrow_ipc — see plugin_data_api.h. Parsers stay per-record;
 *     the host coalesces into Arrow batches internally.
 *
 * v4 appendable tail (no version bump — protocol stays at 4):
 *   - classify_schema, parse_scalars, parse_object: pure-functional API
 *     that returns typed values instead of writing to host views. Enables
 *     lazy materialization and removes the parser's coupling to push policy.
 *     See pj_base/canonical_object_abi.h for the wire format.
 *
 * The host obtains the plugin's vtable via `PJ_get_message_parser_vtable()`
 * and drives the plugin through: create -> bind(registry) ->
 * (bind_schema) -> (classify_schema) -> parse* / parseScalars / parseObject
 * -> destroy.
 */
#ifndef PJ_MESSAGE_PARSER_PROTOCOL_H
#define PJ_MESSAGE_PARSER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/canonical_object_abi.h"
#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_MESSAGE_PARSER_PROTOCOL_VERSION 4

/**
 * Minimum vtable size for v4.0 compatibility, pinned at v4.0 release.
 *
 * Loaders reject plugins whose `struct_size < PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE`.
 * MUST NOT GROW when new tail slots are appended. See PJ_ABI_VERSION comment
 * in plugin_data_api.h for the rationale.
 *
 * Last v4.0 slot is `get_plugin_extension` (promoted from v3 tail).
 */
#define PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE \
  (offsetof(PJ_message_parser_vtable_t, get_plugin_extension) + sizeof(const void* (*)(void*, PJ_string_view_t)))

#if defined(_WIN32)
#define PJ_MESSAGE_PARSER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_MESSAGE_PARSER_EXPORT __attribute__((visibility("default")))
#else
#define PJ_MESSAGE_PARSER_EXPORT
#endif

/**
 * MessageParser plugin vtable (v4).
 *
 * Fallible slots take a `PJ_error_t* out_error`; callers may pass NULL
 * to discard error detail. Every slot is PJ_NOEXCEPT.
 */
typedef struct PJ_message_parser_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_MESSAGE_PARSER_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_message_parser_vtable_t). */

  /** [main-thread] Allocate a new parser instance. */
  void* (*create)(void)PJ_NOEXCEPT;
  /** [main-thread] Destroy an instance previously created by create(). */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;

  /**
   * Static JSON manifest. Compile-time constant.
   *
   * Required keys:
   *   "id"       — stable plugin identifier (string). Used by the host catalog
   *                and the marketplace; must be unique per plugin.
   *   "name"     — human-readable plugin name (string).
   *   "version"  — semver version string (string).
   *   "encoding" — encoding this parser handles (string). The host uses
   *                this to match binding requests to parsers.
   */
  const char* manifest_json;

  /**
   * [main-thread] Bind host services. The host registers at least
   * "pj.parser_write.v1". Plugins that need extra services can query
   * additional names.
   */
  bool (*bind)(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Bind a message schema. Optional — parsers that don't
   * require schema (e.g. JSON) may accept and ignore this.
   */
  bool (*bind_schema)(void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [main-thread] Serialize parser configuration to JSON. */
  bool (*save_config)(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) PJ_NOEXCEPT;
  /** [main-thread] Restore parser configuration from JSON. */
  bool (*load_config)(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [stream-thread] Parse one raw message into writes via the bound
   * write host. @p timestamp_ns is nanoseconds since the Unix epoch.
   * Called on the thread that drives the host's parser dispatcher.
   */
  bool (*parse)(void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_error_t* out_error) PJ_NOEXCEPT;

  /** [thread-safe] Query a plugin-exposed extension by reverse-DNS id.
   *  See PJ_data_source_vtable_t::get_plugin_extension for the full
   *  contract and ID-versioning convention. */
  const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id)PJ_NOEXCEPT;

  /* ====================================================================
   * Tail slots beyond here are OPTIONAL. Host reads MUST check both
   * struct_size and slot-nullability via PJ_HAS_TAIL_SLOT.
   * ==================================================================== */

  /**
   * [thread-safe] A priori classification of the bound schema. Cheap; no
   * payload required. Host invokes this after bind_schema(). Returns
   * @p out_classification by value (POD).
   *
   * NULL or absent (struct_size too small) → host treats as
   * PJ_CANONICAL_OBJECT_KIND_NONE.
   *
   * Pure-functional contract: no host side-effects.
   */
  bool (*classify_schema)(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_schema_classification_t* out_classification,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [stream-thread] Pure-functional alternative to parse(): returns the
   * scalar fields by value (out parameter) instead of writing them to the
   * parser write host. The host invokes this in preference to parse() when
   * available; legacy plugins keep using parse().
   *
   * The plugin owns @p out_fields.fields buffer; @p out_fields.release is
   * called by the host when done. release MAY be NULL.
   */
  bool (*parse_scalars)(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_named_field_value_buffer_t* out_fields,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [stream-thread] Pure-functional production of a canonical object from
   * the payload. Fills @p out_blob with the serialized object (see layout
   * in canonical_object_abi.h). Only meaningful when classify_schema()
   * returned a non-zero kind.
   *
   * Pure-functional contract: no writes to the object write host. The
   * caller (DataSource / app) decides whether to push the blob eagerly,
   * capture it inside a lazy lambda, or hand it directly to a consumer.
   */
  bool (*parse_object)(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_canonical_object_blob_t* out_blob,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_message_parser_vtable_t;
/* The vtable above is ABI-APPENDABLE: new slots may be added at the tail;
 * host reads guard with PJ_HAS_TAIL_SLOT. See PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE. */

/** Signature of the exported entry point: `PJ_get_message_parser_vtable`. */
typedef const PJ_message_parser_vtable_t* (*PJ_get_message_parser_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_MESSAGE_PARSER_PROTOCOL_H
