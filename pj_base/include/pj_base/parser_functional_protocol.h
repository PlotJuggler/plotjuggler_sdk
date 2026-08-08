/**
 * @file parser_functional_protocol.h
 * @brief Additive C ABI extension for pure-functional MessageParser results.
 *
 * A handler-based MessageParser exposes the v1 and v2 tables from
 * get_plugin_extension after at least one SchemaHandler is registered. They
 * replace direct host calls on MessageParserPluginBase with synchronous,
 * caller-owned C sinks; v2 adds an optional single-field object splice. No
 * C++ object, STL container, exception, or plugin allocation survives an
 * extension call.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_PARSER_FUNCTIONAL_PROTOCOL_H
#define PJ_PARSER_FUNCTIONAL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/builtin_object_abi.h"
#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_PARSER_FUNCTIONAL_EXTENSION_V1 "pj.parser_functional.v1"

/** Caller-owned synchronous sink for one scalar record.
 *
 * `fields`, their names, and string values are borrowed from the plugin and
 * valid only until accept_record returns. The callback must copy anything it
 * retains. The provider calls accept_record exactly once on successful parse
 * and never after parse_scalars returns.
 */
typedef bool (*PJ_parser_accept_scalar_record_fn_t)(
    void* ctx, bool has_timestamp, int64_t timestamp_ns, const PJ_named_field_value_t* fields, uint64_t field_count,
    PJ_error_t* out_error) PJ_NOEXCEPT;

typedef struct PJ_parser_scalar_sink_v1_t {
  /** Initialize to sizeof(PJ_parser_scalar_sink_v1_t). Future revisions may
   * append fields; providers must accept every compatible prefix.
   */
  uint32_t struct_size;
  void* ctx;
  PJ_parser_accept_scalar_record_fn_t accept_record;
} PJ_parser_scalar_sink_v1_t;

#define PJ_PARSER_SCALAR_SINK_V1_MIN_SIZE \
  (offsetof(PJ_parser_scalar_sink_v1_t, accept_record) + sizeof(PJ_parser_accept_scalar_record_fn_t))

/** Caller-owned synchronous sink for one canonical builtin object.
 *
 * `canonical_wire` uses the stable `PJ.*` protobuf-wire contract associated
 * with `object_type`. Its bytes are borrowed and valid only until
 * accept_object returns. The callback must decode or copy them synchronously.
 */
typedef bool (*PJ_parser_accept_object_fn_t)(
    void* ctx, bool has_timestamp, int64_t timestamp_ns, uint16_t object_type, PJ_bytes_view_t canonical_wire,
    PJ_error_t* out_error) PJ_NOEXCEPT;

typedef struct PJ_parser_object_sink_v1_t {
  /** Initialize to sizeof(PJ_parser_object_sink_v1_t). Future revisions may
   * append fields; providers must accept every compatible prefix.
   */
  uint32_t struct_size;
  void* ctx;
  PJ_parser_accept_object_fn_t accept_object;
} PJ_parser_object_sink_v1_t;

#define PJ_PARSER_OBJECT_SINK_V1_MIN_SIZE \
  (offsetof(PJ_parser_object_sink_v1_t, accept_object) + sizeof(PJ_parser_accept_object_fn_t))

/** Pure-functional parser extension v1.
 *
 * Calls are [stream-thread] and synchronous. A provider validates the sink
 * table before invoking plugin code. On success it calls the supplied sink
 * exactly once; on parser failure it does not call the sink. Sink failure is
 * propagated as call failure. Every function is noexcept at the C boundary.
 */
typedef bool (*PJ_parser_parse_scalars_fn_t)(
    void* plugin_ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, const PJ_parser_scalar_sink_v1_t* sink,
    PJ_error_t* out_error) PJ_NOEXCEPT;

/** Object parsing takes ownership of exactly one `payload.anchor` reference at
 * function entry and releases it exactly once on every success/failure path.
 * A null ctx + null release is a borrowed call-duration-only payload. The
 * provider may propagate an owning anchor inside plugin-side C++ values while
 * parsing, but all such values are destroyed before this C call returns.
 */
typedef bool (*PJ_parser_parse_object_fn_t)(
    void* plugin_ctx, int64_t timestamp_ns, PJ_payload_t payload, const PJ_parser_object_sink_v1_t* sink,
    PJ_error_t* out_error) PJ_NOEXCEPT;

typedef struct PJ_parser_functional_v1_t {
  /** sizeof(PJ_parser_functional_v1_t) for this append-only table revision. */
  uint32_t struct_size;
  PJ_parser_parse_scalars_fn_t parse_scalars;
  PJ_parser_parse_object_fn_t parse_object;
} PJ_parser_functional_v1_t;

#define PJ_PARSER_FUNCTIONAL_V1_MIN_SIZE \
  (offsetof(PJ_parser_functional_v1_t, parse_object) + sizeof(PJ_parser_parse_object_fn_t))

#define PJ_PARSER_FUNCTIONAL_EXTENSION_V2 "pj.parser_functional.v2"
#define PJ_PARSER_ERROR_KIND_DATA_ERROR "pj.parser.data_error"
#define PJ_PARSER_ERROR_KIND_SINK_REJECTED "pj.parser.sink_rejected"
#define PJ_PARSER_ERROR_KIND_CONTRACT_VIOLATION "pj.parser.contract_violation"

/** Caller-owned synchronous sink for one canonical object with one bulk
 * field represented as a reference into the parse payload.
 *
 * `partial_wire` is canonical `PJ.*` wire with the eligible bulk field
 * elided. `splice_field_number` identifies that field in the frozen table in
 * builtin_object_abi.h. `input_offset` and `input_length` address bytes in
 * the exact payload passed to parse_object. All views are borrowed for the
 * callback duration. At most one splice may be emitted for a message.
 */
typedef bool (*PJ_parser_accept_object_spliced_fn_t)(
    void* ctx, bool has_timestamp, int64_t timestamp_ns, uint16_t object_type, PJ_bytes_view_t partial_wire,
    uint32_t splice_field_number, uint64_t input_offset, uint64_t input_length, PJ_error_t* out_error) PJ_NOEXCEPT;

typedef struct PJ_parser_object_sink_v2_t {
  /** Initialize to sizeof(PJ_parser_object_sink_v2_t). Future revisions may
   * append fields; providers must accept every compatible prefix.
   */
  uint32_t struct_size;
  void* ctx;
  PJ_parser_accept_object_fn_t accept_object;
  PJ_parser_accept_object_spliced_fn_t accept_object_spliced;
} PJ_parser_object_sink_v2_t;

#define PJ_PARSER_OBJECT_SINK_V2_MIN_SIZE \
  (offsetof(PJ_parser_object_sink_v2_t, accept_object_spliced) + sizeof(PJ_parser_accept_object_spliced_fn_t))

/** Pure-functional parser extension v2.
 *
 * Calls are [stream-thread] and synchronous. Scalar parsing is unchanged
 * from v1. Object parsing consumes exactly one payload anchor reference using
 * the v1 ownership contract and may deliver either a complete canonical wire
 * object or one splice-eligible object. Failures set `extended_kind` to one
 * of the frozen PJ_PARSER_ERROR_KIND_* values and leave `extended` null.
 */
typedef struct PJ_parser_functional_v2_t {
  /** sizeof(PJ_parser_functional_v2_t) for this append-only table revision. */
  uint32_t struct_size;
  PJ_parser_parse_scalars_fn_t parse_scalars;
  bool (*parse_object)(
      void* plugin_ctx, int64_t timestamp_ns, PJ_payload_t payload, const PJ_parser_object_sink_v2_t* sink,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_parser_functional_v2_t;

#define PJ_PARSER_FUNCTIONAL_V2_MIN_SIZE               \
  (offsetof(PJ_parser_functional_v2_t, parse_object) + \
   sizeof(bool (*)(void*, int64_t, PJ_payload_t, const PJ_parser_object_sink_v2_t*, PJ_error_t*) PJ_NOEXCEPT))

#ifdef __cplusplus
}
#endif

#endif  // PJ_PARSER_FUNCTIONAL_PROTOCOL_H
