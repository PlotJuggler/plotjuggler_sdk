/**
 * @file parser_route_claims_protocol.h
 * @brief Additive C ABI extension for exact MessageParser route claims.
 *
 * A MessageParser built with a route-aware SDK exposes this table from
 * get_plugin_extension("pj.parser_route_claims.v1"). Classification reports
 * exact handler-table coverage only. The host owns wildcard claims and never
 * asks this extension to report them.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_PARSER_ROUTE_CLAIMS_PROTOCOL_H
#define PJ_PARSER_ROUTE_CLAIMS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/builtin_object_abi.h"
#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_PARSER_ROUTE_CLAIMS_EXTENSION_V1 "pj.parser_route_claims.v1"

#define PJ_PARSER_ROUTE_FLAG_SCALAR_V1 UINT16_C(1)
#define PJ_PARSER_ROUTE_FLAG_OBJECT_V1 UINT16_C(2)
#define PJ_PARSER_ROUTE_MATCH_EXACT_V1 UINT16_C(0)
#define PJ_PARSER_ROUTE_STATUS_CLAIMED_V1 UINT16_C(0)
#define PJ_PARSER_ROUTE_STATUS_DECLINED_V1 UINT16_C(1)

/** Exact route classification for one schema type.
 *
 * `route_flags` uses bit 0 for the scalar route and bit 1 for the object
 * route. `match` must be PJ_PARSER_ROUTE_MATCH_EXACT_V1; other values are
 * invalid because this extension never reports wildcard claims. `status` is
 * claimed or declined. Failures are returned by classify_routes itself and
 * are never encoded as a status. `object_type` is NONE unless the object
 * route is claimed.
 */
typedef struct PJ_route_classification_v1_t {
  uint16_t route_flags;
  uint16_t match;
  uint16_t status;
  uint16_t object_type;
} PJ_route_classification_v1_t;

/** Route-aware parser classification extension v1.
 *
 * classify_routes is [thread-safe], pure, and synchronous. It is called
 * after bind_schema on the same instance. A successful decline means there
 * is no exact handler-table claim for `type_name`; it says nothing about the
 * host-owned wildcard scalar claim. On classification failure the provider
 * returns false, populates `out_error`, and leaves `out` unspecified.
 */
typedef struct PJ_parser_route_claims_v1_t {
  /** sizeof(PJ_parser_route_claims_v1_t) for this append-only table revision. */
  uint32_t struct_size;
  bool (*classify_routes)(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_route_classification_v1_t* out,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_parser_route_claims_v1_t;

#define PJ_PARSER_ROUTE_CLAIMS_V1_MIN_SIZE                  \
  (offsetof(PJ_parser_route_claims_v1_t, classify_routes) + \
   sizeof(bool (*)(void*, PJ_string_view_t, PJ_bytes_view_t, PJ_route_classification_v1_t*, PJ_error_t*) PJ_NOEXCEPT))

#ifdef __cplusplus
}
#endif

#endif  // PJ_PARSER_ROUTE_CLAIMS_PROTOCOL_H
