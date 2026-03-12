/**
 * @file message_parser_protocol.h
 * @brief C ABI protocol for MessageParser plugins (version 1).
 *
 * Defines the vtable contract that a MessageParser shared library must export.
 * The host loads the library, calls PJ_get_message_parser_vtable() to obtain a
 * vtable, then drives the plugin instance through create/bind/parse/destroy.
 *
 * The write host (PJ_parser_write_host_t, from plugin_data_api.h) is the
 * data-plane binding — the parser writes decoded fields through it.
 *
 * String ownership convention: plugin-returned `const char*` pointers remain
 * valid until the next call to the same function on the same context.
 */
#ifndef PJ_MESSAGE_PARSER_PROTOCOL_H
#define PJ_MESSAGE_PARSER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol version. Host and plugin must agree on the same major version. */
#define PJ_MESSAGE_PARSER_PROTOCOL_VERSION 1

#if defined(_WIN32)
#define PJ_MESSAGE_PARSER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PJ_MESSAGE_PARSER_EXPORT __attribute__((visibility("default")))
#else
#define PJ_MESSAGE_PARSER_EXPORT
#endif

/**
 * MessageParser plugin vtable — the interface a plugin shared library exports.
 *
 * The host obtains this via the exported PJ_get_message_parser_vtable() symbol.
 * Typical lifecycle: create -> bind_write_host -> (bind_schema) -> parse* -> destroy.
 */
typedef struct PJ_message_parser_vtable_t {
  uint32_t protocol_version; /**< Must equal PJ_MESSAGE_PARSER_PROTOCOL_VERSION. */
  uint32_t struct_size;      /**< sizeof(PJ_message_parser_vtable_t). */

  /** Allocate a new plugin instance. Returns opaque context pointer. */
  void* (*create)(void);
  /** Destroy an instance previously created by create(). */
  void (*destroy)(void* ctx);

  /**
   * Static JSON manifest. Compile-time constant string literal.
   *
   * Required keys:
   *   "name"     — human-readable plugin name (string).
   *   "version"  — semver version string (string).
   *   "encoding" — encoding this parser handles, e.g. "json", "protobuf" (string).
   *                The host uses this to match binding requests to parsers.
   */
  const char* manifest_json;

  /** Bind the data-plane write host. Must be called before parse(). */
  bool (*bind_write_host)(void* ctx, PJ_parser_write_host_t write_host);

  /**
   * Bind a message schema. Optional; called after create(), before parse().
   * Parsers that don't require schema (e.g. JSON) may accept and ignore this.
   * @p type_name is the encoding-specific message type name.
   * @p schema is the raw schema bytes (e.g. protobuf FileDescriptorSet).
   */
  bool (*bind_schema)(void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema);

  /** Serialize plugin configuration to JSON. Plugin-owned string. */
  const char* (*save_config)(void* ctx);
  /** Restore plugin configuration from JSON. */
  bool (*load_config)(void* ctx, const char* config_json);

  /**
   * Parse one raw message into writes via the bound write host.
   * @p timestamp_ns is nanoseconds since the Unix epoch (1970-01-01T00:00:00Z).
   * @p payload is the raw message bytes.
   */
  bool (*parse)(void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload);

  /** Return the last error message, or NULL if none. Plugin-owned string. */
  const char* (*get_last_error)(void* ctx);
} PJ_message_parser_vtable_t;

/** Signature of the exported entry point: `PJ_get_message_parser_vtable`. */
typedef const PJ_message_parser_vtable_t* (*PJ_get_message_parser_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif  // PJ_MESSAGE_PARSER_PROTOCOL_H
