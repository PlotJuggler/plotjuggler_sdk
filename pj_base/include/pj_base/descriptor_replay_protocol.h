/**
 * @file descriptor_replay_protocol.h
 * @brief Descriptor replay v1 — a FAMILY-NEUTRAL plugin extension + host
 * service pair for replaying a persisted "source descriptor" (an opaque,
 * provider-defined JSON document, typically stored in a layout file) back
 * into a loaded dataset, and for adopting the provider's materialized
 * artifact as a stock file-backed source.
 *
 *  - Plugin side: "pj.descriptor_replay.v1" (PJ_descriptor_replay_provider_v1_t),
 *    returned from ANY plugin family's get_plugin_extension hook (see
 *    PJ_data_source_vtable_t::get_plugin_extension for the hook contract).
 *    plugin_ctx is the originating plugin-family instance context — the same
 *    ctx get_plugin_extension was called with, never the extension-table
 *    pointer.
 *  - Host side: "pj.materialized_source.v1" (PJ_materialized_source_host_t),
 *    an optional service acquired from the bind() registry. Absence means the
 *    host has no adoption support. The host registers it PER PLUGIN INSTANCE
 *    and derives the provider's manifest id from that binding itself — the
 *    plugin never supplies its own identity, so it cannot be spoofed.
 *
 * Thread tags used below, beyond plugin_data_api.h's [main-thread] /
 * [thread-safe] set:
 *   [job-callback-thread, serialized]  A started replay job's callback
 *                                      thread; serialized, not necessarily main.
 *   [blocking, not-callback-thread]    May block; never call from a job or
 *                                      host callback.
 *   [host-callback-thread]             The host's adopt() result-callback
 *                                      thread; serialized per request.
 *   [thread-safe, asynchronous]        Any thread; the call returns before
 *                                      the operation completes.
 *
 * Growth contract (every struct below, both directions): the struct's owner
 * zero-initializes its complete allocation, then sets struct_size; the other
 * side reads/writes only fields wholly covered by that size. Field-appends to
 * v1 structs are therefore absent-as-zero on older peers. Semantic changes
 * (as opposed to additions) get a side-by-side ".v2" id instead.
 *
 * Encoding, lifetime and threading rules:
 *  - All text and JSON is UTF-8. Paths are absolute filesystem paths encoded
 *    as UTF-8; on Windows the host converts them to native wide paths.
 *  - out_error may be NULL on every call in this file (matching
 *    plugin_data_api.h convention) — callees must tolerate it, e.g. via a
 *    fillError()-style helper that no-ops on NULL.
 *  - query_descriptor inputs live for the call; views in out_result live
 *    until the NEXT query_descriptor call on the same plugin instance — the
 *    caller copies immediately.
 *  - start_replay copies the request contents and the callback function
 *    pointers before returning; no job callback may occur before it returns
 *    true.
 *  - Job callbacks are serialized but may arrive off the main thread; the
 *    host marshals. on_dataset is zero-or-one and precedes the dataset's
 *    progress_start, first publication, adoption, and on_terminal.
 *    on_terminal is exactly-once and last.
 *  - join returns only after the terminal callback has returned. destroy
 *    cancels and joins when necessary. Never call join/destroy from a job
 *    callback. The plugin instance and its DSO must stay alive until every
 *    job obtained from it has been destroyed.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_DESCRIPTOR_REPLAY_PROTOCOL_H
#define PJ_DESCRIPTOR_REPLAY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Plugin extension: "pj.descriptor_replay.v1"
 *
 * Returned from any plugin family's get_plugin_extension() hook. Lets a
 * plugin advertise descriptor-query and replay-start support without a
 * family protocol bump. See the file doc-block above for the full contract.
 * ========================================================================== */

#define PJ_DESCRIPTOR_REPLAY_EXTENSION_V1 "pj.descriptor_replay.v1"
#define PJ_MATERIALIZED_SOURCE_HOST_SERVICE_V1 "pj.materialized_source.v1"

/** Unknown/future trust values fail closed: treat as REFUSED. */
typedef enum PJ_descriptor_trust_t {
  PJ_DESCRIPTOR_TRUST_REFUSED = 0,
  PJ_DESCRIPTOR_TRUST_NEEDS_CONFIRMATION = 1,
  PJ_DESCRIPTOR_TRUST_TRUSTED = 2,
  /* Forces a stable 4-byte width across compilers. Not a real state. */
  PJ_DESCRIPTOR_TRUST_FORCE_INT32 = 0x7FFFFFFF
} PJ_descriptor_trust_t;

/** Unknown/future outcome values fail closed: treat as FAILED. */
typedef enum PJ_descriptor_replay_outcome_t {
  PJ_DESCRIPTOR_REPLAY_FAILED = 0,
  PJ_DESCRIPTOR_REPLAY_CANCELLED = 1,
  /* Replay produced a usable eager dataset but no adoptable artifact. */
  PJ_DESCRIPTOR_REPLAY_SUCCEEDED_UNMATERIALIZED = 2,
  PJ_DESCRIPTOR_REPLAY_SUCCEEDED_MATERIALIZED = 3,
  /* Forces a stable 4-byte width across compilers. Not a real state. */
  PJ_DESCRIPTOR_REPLAY_OUTCOME_FORCE_INT32 = 0x7FFFFFFF
} PJ_descriptor_replay_outcome_t;

typedef uint64_t PJ_descriptor_replay_start_flags_t;

/* V1 defines no optional modes. Added flag bits require an SDK MINOR. */
#define PJ_DESCRIPTOR_REPLAY_START_FLAG_NONE UINT64_C(0)
#define PJ_DESCRIPTOR_REPLAY_START_FLAGS_V1_MASK UINT64_C(0)

/*
 * Caller zero-initializes its complete available capacity, then sets
 * struct_size. The provider writes only fields wholly covered by that size.
 *
 * On success source_identity and local_path_utf8 are always present, whether
 * is_materialized is zero or one. Returned views remain valid until the next
 * query_descriptor call on the same plugin instance.
 */
typedef struct PJ_descriptor_query_result_v1_t {
  uint32_t struct_size;
  uint32_t reserved0; /* must be zero */

  PJ_descriptor_trust_t trust;
  uint32_t is_materialized; /* exactly 0 or 1 */

  PJ_string_view_t source_identity;
  PJ_string_view_t local_path_utf8;
  PJ_string_view_t message; /* optional refusal/confirmation explanation */

  /*
   * Best estimate of provider payload bytes that replay would transfer,
   * measured consistently with max_transfer_bytes. Zero means unknown.
   * Derived from the descriptor or local metadata — never the network.
   */
  uint64_t estimated_bytes;
} PJ_descriptor_query_result_v1_t;

/*
 * Caller-owned and caller-sized. The caller zero-initializes the complete
 * allocation, then sets struct_size.
 *
 * descriptor_json is valid for the call; start_replay copies it before
 * returning successfully. Runtime options live HERE, never in the descriptor
 * (the descriptor is the persisted identity artifact).
 */
typedef struct PJ_descriptor_replay_start_request_v1_t {
  uint32_t struct_size;
  uint32_t reserved0; /* must be zero */

  PJ_string_view_t descriptor_json;
  PJ_descriptor_replay_start_flags_t flags;

  /*
   * Maximum provider payload bytes that may be transferred by this replay.
   * Zero means no additional caller-imposed ceiling; provider-configured
   * hard resource limits still apply.
   */
  uint64_t max_transfer_bytes;
} PJ_descriptor_replay_start_request_v1_t;

/*
 * General-purpose cancel/join/destroy shape — not descriptor-replay-specific.
 * Other future async plugin APIs may reuse it as-is; it lives in this header
 * only because descriptor replay v1 is its first consumer. Its layout is
 * frozen regardless of what else comes to depend on it.
 */
typedef struct PJ_joinable_job_vtable_t {
  uint32_t struct_size;
  uint32_t reserved0; /* must be zero */

  /** [thread-safe] Non-blocking, idempotent, best-effort cancellation. */
  void (*cancel)(void* ctx) PJ_NOEXCEPT;

  /**
   * [blocking, not-callback-thread] Idempotent. Returns after all callbacks,
   * including on_terminal, have returned.
   */
  void (*join)(void* ctx) PJ_NOEXCEPT;

  /**
   * [blocking, not-callback-thread] Invalidates ctx. Cancels and joins first
   * when necessary.
   */
  void (*destroy)(void* ctx) PJ_NOEXCEPT;
} PJ_joinable_job_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct PJ_joinable_job_t {
  void* ctx;
  const PJ_joinable_job_vtable_t* vtable;
} PJ_joinable_job_t;

typedef struct PJ_descriptor_replay_callbacks_v1_t {
  uint32_t struct_size;
  uint32_t reserved0; /* must be zero */

  /**
   * [job-callback-thread, serialized] Zero or one call. Must precede the
   * dataset's progress_start, first publication, adoption, and on_terminal.
   */
  void (*on_dataset)(void* callback_ctx, PJ_data_source_handle_t dataset) PJ_NOEXCEPT;

  /**
   * [job-callback-thread, serialized] Exactly once and last. message is valid
   * only for the duration of the callback.
   */
  void (*on_terminal)(void* callback_ctx, PJ_descriptor_replay_outcome_t outcome, PJ_string_view_t message) PJ_NOEXCEPT;
} PJ_descriptor_replay_callbacks_v1_t;

/*
 * Returned through any plugin family's get_plugin_extension() for
 * PJ_DESCRIPTOR_REPLAY_EXTENSION_V1. The plugin owns this struct; it must
 * stay valid for the plugin instance lifetime. plugin_ctx is the same
 * originating plugin-instance context passed to get_plugin_extension(); it is
 * never the extension-table pointer.
 */
typedef struct PJ_descriptor_replay_provider_v1_t {
  uint32_t struct_size;
  uint32_t reserved0; /* must be zero */

  /**
   * [main-thread, strictly bounded] No network, credential resolution,
   * blocking lock acquisition, or full-file scan. False means a
   * malformed/unsupported descriptor; out_result is then unspecified and
   * out_error carries the reason.
   */
  bool (*query_descriptor)(
      void* plugin_ctx, PJ_string_view_t descriptor_json, PJ_descriptor_query_result_v1_t* out_result,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * [main-thread] Copies the request and callback pointers before returning.
   * Unknown flag bits are rejected synchronously: false, out_error populated,
   * no callbacks, and out_job untouched. On true: out_job is valid and
   * on_terminal will occur exactly once.
   */
  bool (*start_replay)(
      void* plugin_ctx, const PJ_descriptor_replay_start_request_v1_t* request,
      const PJ_descriptor_replay_callbacks_v1_t* callbacks, void* callback_ctx, PJ_joinable_job_t* out_job,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_descriptor_replay_provider_v1_t;

/* ==========================================================================
 * Host service: "pj.materialized_source.v1" (protocol_version 1)
 *
 * Acquired from the bind() service registry, bound per plugin instance.
 * Lets a descriptor-replay provider hand its materialized artifact to the
 * host to be adopted as a stock file-backed source. Absence means the host
 * has no adoption support. See the file doc-block above for the full
 * contract.
 * ========================================================================== */

/*
 * Adoption request. All views are valid for the duration of the adopt() call;
 * the host copies before returning. loader_plugin_id + loader_config_json are
 * provider-supplied because a non-MCAP artifact needs its own companion
 * loader; dataset is the provisional dataset announced via on_dataset.
 */
typedef struct PJ_materialized_source_adopt_request_v1_t {
  uint32_t struct_size;
  PJ_data_source_handle_t dataset;

  PJ_string_view_t source_identity;
  PJ_string_view_t local_path_utf8;
  PJ_string_view_t loader_plugin_id;
  PJ_string_view_t loader_config_json;
  PJ_string_view_t descriptor_json;
} PJ_materialized_source_adopt_request_v1_t;

/**
 * [host-callback-thread] Runs exactly once after an ACCEPTED adopt() call
 * (i.e. adopt() returned true) — distinct from success: ok reports whether
 * the transaction itself succeeded. message is valid only for the duration
 * of the callback.
 */
typedef void (*PJ_materialized_source_adopt_result_fn)(void* callback_ctx, bool ok, PJ_string_view_t message)
    PJ_NOEXCEPT;

/**
 * Host adoption service ("pj.materialized_source.v1", protocol_version 1).
 * Bound per plugin instance — the service ctx identifies the provider.
 */
typedef struct PJ_materialized_source_host_vtable_t {
  uint32_t protocol_version; /* = 1 */
  uint32_t struct_size;

  /**
   * [thread-safe, asynchronous] Copies the complete request before returning
   * and marshals the stock loader transaction to the host thread.
   *
   * false: request rejected synchronously; result_cb will not run.
   * true: request accepted; result_cb will run exactly once. This does NOT
   * mean the dataset swap has happened yet — only that it has been queued.
   * result_cb MAY be invoked re-entrantly, before adopt() returns.
   *
   * By the time result_cb reports ok=true, the host has transactionally
   * replaced the named dataset, captured the loader's accepted configuration,
   * and attached {provider manifest id, source_identity, descriptor_json}.
   */
  bool (*adopt)(
      void* ctx, const PJ_materialized_source_adopt_request_v1_t* request,
      PJ_materialized_source_adopt_result_fn result_cb, void* callback_ctx, PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_materialized_source_host_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct PJ_materialized_source_host_t {
  void* ctx;
  const PJ_materialized_source_host_vtable_t* vtable;
} PJ_materialized_source_host_t;

#ifdef __cplusplus
}
#endif

#endif
