#ifndef PJ_PLUGIN_DATA_API_H
#define PJ_PLUGIN_DATA_API_H
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_PLUGIN_DATA_API_VERSION 1

/*
 * PJ_NOEXCEPT: applied to every function-pointer type in a vtable. In C++ this
 * is part of the function type (since C++17) and is enforced at compile time;
 * in C it is a no-op. Plugin-side trampolines that implement these slots MUST
 * be declared noexcept — a throw across the ABI boundary calls std::terminate.
 */
#ifdef __cplusplus
#define PJ_NOEXCEPT noexcept
#else
#define PJ_NOEXCEPT
#endif

/**
 * Boot-level ABI version, exported by every plugin .so as a separate C symbol
 * independent of any vtable. Loaders dlsym this BEFORE fetching the family
 * vtable, because struct_size — the next level of compatibility check — lives
 * INSIDE the struct being validated, creating a bootstrap problem. An
 * incompatible or missing `pj_plugin_abi_version` is a fail-fast rejection
 * with a specific error.
 *
 * The integer value is PJ_ABI_VERSION below. The symbol name the loader looks
 * up is `pj_plugin_abi_version` (a regular C identifier, not a preprocessor
 * token).
 *
 * Contract for plugin authors: the symbol is emitted automatically at file
 * scope by `pj_base/plugin_abi_export.hpp`, which is transitively included by
 * every family SDK header (data_source_plugin_base.hpp, dialog_plugin_base.hpp,
 * etc.). Weak linkage lets multiple TUs in the same DSO each emit a definition
 * and have the linker fold them into one COMDAT entry — so co-resident DSOs
 * (e.g. DataSource + Dialog in one .so) work without any extra ceremony.
 * Do not redefine it manually.
 *
 * v5 plugins advertise version 5. Relative to v4, this bump rejects
 * pre-v5 parser DSOs because the C++ MessageParserPluginBase
 * pure-functional contract changed parseScalars()/parseObject() return
 * types to ScalarRecord/ObjectRecord.
 *
 * The C data-plane layout remains the v4 layout:
 *   - Arrow C Data Interface replaces Arrow IPC bytes at the boundary
 *     (append_arrow_stream + read_series_arrow).
 *   - append_arrow_ipc removed from all write hosts.
 *   - read_series (PJ_materialized_series_t) removed from toolbox host.
 *   - Every vtable slot is PJ_NOEXCEPT.
 *   - Every slot carries a thread-class tag (// [main-thread], etc.).
 */
#define PJ_ABI_VERSION 5

/**
 * Convention for plugin-loaders:
 *
 *   1. `dlsym("pj_plugin_abi_version")` — reject if missing or not equal to PJ_ABI_VERSION.
 *   2. `dlsym("PJ_get_<family>_vtable")` — reject if missing.
 *   3. Check `vtable->protocol_version == PJ_<FAMILY>_PROTOCOL_VERSION`.
 *   4. Check `vtable->struct_size >= PJ_<FAMILY>_MIN_VTABLE_SIZE`
 *      (NOT `sizeof(...)` — that grows per host release and would reject
 *       plugins compiled against older headers).
 *   5. Every tail slot read must be guarded by
 *      `PJ_HAS_TAIL_SLOT(vtable_type, vtable_ptr, field)` below, which
 *      checks both struct_size and field non-null.
 */
#define PJ_HAS_TAIL_SLOT(vtable_type, vtable_ptr, field)                                        \
  ((vtable_ptr)->struct_size >= (offsetof(vtable_type, field) + sizeof((vtable_ptr)->field)) && \
   (vtable_ptr)->field != NULL)

typedef enum {
  PJ_PRIMITIVE_TYPE_FLOAT32 = 0,
  PJ_PRIMITIVE_TYPE_FLOAT64 = 1,
  PJ_PRIMITIVE_TYPE_INT8 = 2,
  PJ_PRIMITIVE_TYPE_INT16 = 3,
  PJ_PRIMITIVE_TYPE_INT32 = 4,
  PJ_PRIMITIVE_TYPE_INT64 = 5,
  PJ_PRIMITIVE_TYPE_UINT8 = 6,
  PJ_PRIMITIVE_TYPE_UINT16 = 7,
  PJ_PRIMITIVE_TYPE_UINT32 = 8,
  PJ_PRIMITIVE_TYPE_UINT64 = 9,
  PJ_PRIMITIVE_TYPE_BOOL = 10,
  PJ_PRIMITIVE_TYPE_STRING = 11,
  /** Sentinel: null value with no type information. Used when is_null=true
   *  and the plugin provides no type hint (untyped kNull). */
  PJ_PRIMITIVE_TYPE_UNSPECIFIED = 0xFF,
} PJ_primitive_type_t;

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  const char* data;
  size_t size;
} PJ_string_view_t;

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  const uint8_t* data;
  size_t size;
} PJ_bytes_view_t;

/* ==========================================================================
 * Apache Arrow C Data Interface.
 *
 * These are the exact POD struct layouts from the Arrow specification at
 * https://arrow.apache.org/docs/format/CDataInterface.html, inlined verbatim
 * so the plugin ABI surface has zero Arrow-library dependency. Plugins that
 * want helpers link nanoarrow themselves.
 *
 * Frozen by upstream Arrow ("once this specification is supported in an
 * official Arrow release, the C ABI is frozen"). The ARROW_C_DATA_INTERFACE
 * guard is the official spec guard — if nanoarrow or arrow-cpp is already
 * included, these declarations are elided and the existing definitions win.
 *
 * Ownership: every struct carries its own `release` callback plus private
 * data. The producer of the struct is responsible for setting `release`; the
 * consumer that takes ownership is responsible for calling it when done.
 * ========================================================================== */

#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
  const char* format;
  const char* name;
  const char* metadata;
  int64_t flags;
  int64_t n_children;
  struct ArrowSchema** children;
  struct ArrowSchema* dictionary;
  void (*release)(struct ArrowSchema*);
  void* private_data;
};

struct ArrowArray {
  int64_t length;
  int64_t null_count;
  int64_t offset;
  int64_t n_buffers;
  int64_t n_children;
  const void** buffers;
  struct ArrowArray** children;
  struct ArrowArray* dictionary;
  void (*release)(struct ArrowArray*);
  void* private_data;
};

struct ArrowArrayStream {
  int (*get_schema)(struct ArrowArrayStream*, struct ArrowSchema* out);
  int (*get_next)(struct ArrowArrayStream*, struct ArrowArray* out);
  const char* (*get_last_error)(struct ArrowArrayStream*);
  void (*release)(struct ArrowArrayStream*);
  void* private_data;
};

#endif /* ARROW_C_DATA_INTERFACE */

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  uint32_t id;
} PJ_data_source_handle_t;

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  uint32_t id;
} PJ_topic_handle_t;

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  PJ_topic_handle_t topic;
  uint32_t id;
} PJ_field_handle_t;

/* ==========================================================================
 * Protocol v4 core types
 *
 * PJ_error_t carries its message/domain INLINE (fixed-size null-terminated
 * buffers) so callers can copy it freely and its lifetime is trivial.
 * There is no dangling view into plugin-owned storage.
 * ========================================================================== */

#define PJ_ERROR_DOMAIN_MAX 32
#define PJ_ERROR_MESSAGE_MAX 224
#define PJ_ERROR_KIND_MAX 32

/*
 * ABI-FROZEN (with growth escape hatch).
 *
 * The inline layout is permanent for v4.x — existing fields never move or
 * change type. The `extended` + `extended_kind` slots are the designated
 * growth path for richer payloads (cause chains, stack traces, structured
 * field lists); never add further top-level fields.
 *
 * Lifetime of `extended`: valid until the next ABI call through the same
 * plugin instance's vtable. Callers that want to retain the payload past
 * that window must deep-copy. `extended_kind` is a reverse-DNS ID of the
 * payload type (e.g. "pj.error.cause.v1"); when `extended_kind[0]=='\0'`
 * the `extended` pointer is ignored regardless of its value.
 *
 * Every populator (see sdk::fillError) MUST clear both new slots when
 * writing to avoid stale pointers in reused error structs.
 */
typedef struct {
  int32_t code;                          /* 0 = success; otherwise domain-specific */
  char domain[PJ_ERROR_DOMAIN_MAX];      /* null-terminated; truncated if too long */
  char message[PJ_ERROR_MESSAGE_MAX];    /* null-terminated; truncated if too long */
  const void* extended;                  /* nullable typed payload */
  char extended_kind[PJ_ERROR_KIND_MAX]; /* reverse-DNS ID; "" if no payload */
} PJ_error_t;

/* ABI-FROZEN: fat pointer layout permanent. The `vtable` is const void* by
 * design — consumers cast to the appropriate typed service vtable based on
 * the service name they requested. */
typedef struct {
  void* ctx;
  const void* vtable;
} PJ_service_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read. */
typedef struct PJ_service_registry_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /* [thread-safe] Look up a host-provided service by reverse-DNS name. */
  bool (*get_service)(
      void* ctx, PJ_string_view_t name, uint32_t min_version, PJ_service_t* out_service,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_service_registry_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const PJ_service_registry_vtable_t* vtable;
} PJ_service_registry_t;

struct PJ_dialog_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const struct PJ_dialog_vtable_t* vtable;
} PJ_borrowed_dialog_t;

typedef union {
  float as_float32;
  double as_float64;
  int8_t as_int8;
  int16_t as_int16;
  int32_t as_int32;
  int64_t as_int64;
  uint8_t as_uint8;
  uint16_t as_uint16;
  uint32_t as_uint32;
  uint64_t as_uint64;
  uint8_t as_bool;
  PJ_string_view_t as_string;
} PJ_scalar_value_data_t;

typedef struct {
  PJ_primitive_type_t type;
  PJ_scalar_value_data_t data;
} PJ_scalar_value_t;

typedef struct {
  PJ_string_view_t name;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_named_field_value_t;

typedef struct {
  PJ_field_handle_t field;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_bound_field_value_t;

typedef struct {
  PJ_data_source_handle_t handle;
  PJ_string_view_t name;
  uint32_t first_topic;
  uint32_t topic_count;
} PJ_data_source_info_t;

typedef struct {
  PJ_topic_handle_t handle;
  PJ_data_source_handle_t source;
  PJ_string_view_t name;
  uint32_t first_field;
  uint32_t field_count;
} PJ_topic_info_t;

typedef struct {
  PJ_field_handle_t handle;
  PJ_string_view_t name;
  PJ_primitive_type_t type;
} PJ_field_info_t;

typedef struct {
  const PJ_data_source_info_t* data_sources;
  size_t data_source_count;
  const PJ_topic_info_t* topics;
  size_t topic_count;
  const PJ_field_info_t* fields;
  size_t field_count;
  void* release_ctx;
  void (*release)(void* release_ctx);
} PJ_catalog_snapshot_t;

/* ==========================================================================
 * Three distinct write-host vtables (protocol v4).
 *
 * Each plugin family binds to its own type so the compiler enforces scope:
 * a DataSource plugin cannot accidentally call Toolbox-only ops, a Parser
 * plugin cannot name topics, etc. The host-side implementation can (and
 * does) share one backend across all three — but at the ABI layer the
 * types are distinct.
 *
 * All fallible slots take a PJ_error_t* out-parameter. Callers may pass
 * NULL to discard detail. Every slot is PJ_NOEXCEPT and carries a
 * thread-class tag in its leading comment.
 *
 * Arrow C Data Interface is the canonical bulk-ingest path
 * (append_arrow_stream). Per-record slots remain for streaming producers
 * and simple plugins where batching does not fit naturally. Thread tags:
 *   [main-thread]    GUI thread. Dialog callbacks, initial config.
 *   [stream-thread]  Host's background ingest thread. Most appends.
 *   [thread-safe]    Any thread.
 * ========================================================================== */

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Source write host: multi-topic writes bound to one data source.
 *
 * append_arrow_stream ownership:
 *   Producer (plugin) sets `stream->release`. On a successful call the host
 *   takes ownership of the stream, pulls all batches via get_next, and calls
 *   stream->release before returning. On failure (function returns false),
 *   ownership is NOT transferred — the plugin retains responsibility and
 *   must release the stream itself. */
typedef struct PJ_source_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [stream-thread] Ensure a topic exists under this data source. */
  bool (*ensure_topic)(void* ctx, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic, PJ_error_t* out_error)
      PJ_NOEXCEPT;

  /* [stream-thread] Ensure a field exists under a topic with the given type. */
  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Append a record by field name. Convenience path for
   * simple plugins; resolves field handles on every call. */
  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Append a record with pre-resolved field handles. Fast
   * path for streaming producers — skip the name lookup. */
  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] PRIMARY BATCH PATH. Plugin hands ownership of an Arrow
   * C Data Interface stream; host pulls all batches and releases the stream
   * before returning (success path). `timestamp_column` names the column
   * in the stream's schema whose int64 values are interpreted as nanoseconds
   * since Unix epoch; if empty, a synthetic monotonic timestamp is used. */
  bool (*append_arrow_stream)(
      void* ctx, PJ_topic_handle_t topic, struct ArrowArrayStream* stream, PJ_string_view_t timestamp_column,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_source_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_source_write_host_vtable_t* vtable;
} PJ_source_write_host_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Parser write host: single-topic writes. The bound topic is set at
 * service-creation time; the parser plugin never names it.
 *
 * append_arrow_stream is an optional tail slot for parser-shaped formats
 * that naturally decode a batch in one parse() call. Ownership matches
 * PJ_source_write_host_vtable_t::append_arrow_stream. Plugins must gate
 * this slot with PJ_HAS_TAIL_SLOT when calling through the C ABI directly. */
typedef struct PJ_parser_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [stream-thread] Ensure a field exists in the bound topic. */
  bool (*ensure_field)(
      void* ctx, PJ_string_view_t field_name, PJ_primitive_type_t type, PJ_field_handle_t* out_field,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Append a record by field name. */
  bool (*append_record)(
      void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Append a record with pre-resolved field handles. */
  bool (*append_bound_record)(
      void* ctx, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Optional batch path. Plugin hands ownership of an Arrow
   * C Data Interface stream for the bound topic. The timestamp column rule
   * and success/failure ownership contract are identical to
   * PJ_source_write_host_vtable_t::append_arrow_stream. */
  bool (*append_arrow_stream)(
      void* ctx, struct ArrowArrayStream* stream, PJ_string_view_t timestamp_column, PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_parser_write_host_vtable_t;

/*
 * Parser write-host v4.0 floor, before append_arrow_stream was added as an
 * optional tail slot. Hosts/plugins that care about the batch path must use
 * PJ_HAS_TAIL_SLOT(PJ_parser_write_host_vtable_t, vtable, append_arrow_stream).
 */
#define PJ_PARSER_WRITE_HOST_MIN_VTABLE_SIZE (offsetof(PJ_parser_write_host_vtable_t, append_arrow_stream))

typedef struct {
  void* ctx;
  const PJ_parser_write_host_vtable_t* vtable;
} PJ_parser_write_host_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Toolbox host: multi-source read+write.
 *
 * read_series_arrow: caller zero-initialises both out structs. Host fills
 * them (allocates buffers, sets release callbacks). On success the caller
 * MUST invoke out_schema->release and out_array->release when done. The
 * array has two columns: ["timestamp" (int64), <field_name> (typed)].
 * Validity bitmap populated per Arrow spec. */
typedef struct PJ_toolbox_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [main-thread] Create a new named data source, returning its handle. */
  bool (*create_data_source)(
      void* ctx, PJ_string_view_t name, PJ_data_source_handle_t* out_source, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Ensure a topic exists under a specified data source. */
  bool (*ensure_topic)(
      void* ctx, PJ_data_source_handle_t source, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Ensure a field exists under a topic. */
  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Append a record by field name. */
  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Append a record with pre-resolved field handles. */
  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Bulk-write via Arrow C Data Interface (same ownership rule
   * as PJ_source_write_host_vtable_t::append_arrow_stream). */
  bool (*append_arrow_stream)(
      void* ctx, PJ_topic_handle_t topic, struct ArrowArrayStream* stream, PJ_string_view_t timestamp_column,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Snapshot the current catalog of data sources, topics, and
   * fields. Caller releases via snapshot.release(snapshot.release_ctx). */
  bool (*acquire_catalog_snapshot)(void* ctx, PJ_catalog_snapshot_t* out_snapshot, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Materialise one field's time series into a host-owned
   * ArrowArray (two columns: timestamp + field). Caller must call
   * out_schema->release and out_array->release when done. */
  bool (*read_series_arrow)(
      void* ctx, PJ_field_handle_t field, struct ArrowSchema* out_schema, struct ArrowArray* out_array,
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_toolbox_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_toolbox_host_vtable_t* vtable;
} PJ_toolbox_host_t;

/* ==========================================================================
 * Object-store write host (protocol v4)
 *
 * Plugin-visible surface over `pj_datastore::ObjectStore` — a message-
 * oriented peer to DataEngine holding timestamped opaque payloads
 * (markers/annotations written eagerly via push_owned; images/point
 * clouds written lazily via push_lazy with a plugin-owned fetch closure).
 *
 * Separate from the scalar write surface by design: a plugin family
 * may want one, the other, or both. DataSource plugins that handle
 * media resolve `pj.source_object_write.v1` from the service registry.
 * Parser plugins receiving delegated ingest for a media topic resolve
 * `pj.parser_object_write.v1` in addition to `pj.parser_write.v1`.
 * ========================================================================== */

/* ABI-FROZEN: layout permanent; changes = ABI break. */
typedef struct {
  uint32_t id; /* 0 == invalid handle */
} PJ_object_topic_handle_t;

/* Lazy-fetch callback type. Invoked by the host on-demand when a consumer
 * reads an entry stored via push_lazy. On success the plugin populates
 * *out_data + *out_size with a pointer into memory owned by the plugin's
 * fetch context — valid at least until the NEXT call to the same fn or
 * until fetch_ctx_destroy runs. The host immediately copies the bytes; the
 * plugin may reuse or free the buffer on the following call. */
typedef bool (*PJ_lazy_fetch_fn_t)(void* fetch_ctx, const uint8_t** out_data, size_t* out_size) PJ_NOEXCEPT;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * push_lazy / fetch_ctx lifetime contract:
 *   The store retains fetch_ctx for the lifetime of the pushed entry (i.e.
 *   as long as the entry remains in the ObjectStore's deque — indefinite
 *   if no retention budget, bounded by the budget otherwise). When the
 *   entry is finally evicted (retention, explicit evictBefore, removeTopic,
 *   or clear), the store invokes fetch_ctx_destroy(fetch_ctx) exactly once.
 *   Typical use: pack a shared_ptr<FileReader> into fetch_ctx; the destroy
 *   callback `delete`s the owning box and drops the shared reference. */
typedef struct PJ_object_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [stream-thread] Register an object topic under this data source with
   * the given metadata JSON. `metadata_json` is opaque to the store and
   * retained verbatim; viewers and parsers read it to pick a renderer.
   * Returns false (with out_error populated) if a topic with this name
   * already exists for the data source. */
  bool (*register_topic)(
      void* ctx, PJ_string_view_t topic_name, PJ_string_view_t metadata_json, PJ_object_topic_handle_t* out_handle,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Push an eagerly-owned entry. The store copies the bytes
   * into its own storage; the plugin's buffer is free to be reused or freed
   * the moment this call returns. Appropriate for small structured messages
   * (markers, annotations, scene primitives) whose aggregate volume stays
   * comfortably in memory. */
  bool (*push_owned)(
      void* ctx, PJ_object_topic_handle_t topic, int64_t timestamp_ns, const uint8_t* data, size_t size,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Push a lazy entry — host stores the fetch closure, not
   * the bytes. Appropriate for large blobs (still images, point clouds)
   * where eager storage would inflate memory. See the lifetime contract
   * above for fetch_ctx / fetch_ctx_destroy. */
  bool (*push_lazy)(
      void* ctx, PJ_object_topic_handle_t topic, int64_t timestamp_ns, PJ_lazy_fetch_fn_t fetch_fn, void* fetch_ctx,
      void (*fetch_ctx_destroy)(void*), PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [stream-thread] Configure the per-topic retention budget. Either axis
   * may be zero to disable that axis; both zero disables automatic
   * eviction entirely. Infallible — bad handles are silently ignored.
   *
   * Typical plugin author usage: do NOT call this. The application owns
   * the retention policy; DataSource plugins should leave budgets alone. */
  void (*set_retention_budget)(
      void* ctx, PJ_object_topic_handle_t topic, int64_t time_window_ns, size_t max_memory_bytes) PJ_NOEXCEPT;
} PJ_object_write_host_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const PJ_object_write_host_vtable_t* vtable;
} PJ_object_write_host_t;

/* ==========================================================================
 * Parser-scoped object write host (protocol v4)
 *
 * The MessageParser analogue of PJ_parser_write_host_vtable_t for the object
 * path. Topic is bound once by the host at service-creation time (just like
 * the scalar parser write host); the parser never names topics. Delivered
 * only when the host has an object-capable target for the parser's binding —
 * typically delegated ingest from a DataSource that registered an object
 * topic alongside the scalar topic.
 *
 * A media-capable parser resolves both "pj.parser_write.v1" and
 * "pj.parser_object_write.v1" at bind time and writes scalar portions to the
 * former + media payload to the latter from a single parse() call. No
 * protocol-version bump needed.
 * ========================================================================== */

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * Same lifetime contract for fetch_ctx / fetch_ctx_destroy as
 * PJ_object_write_host_vtable_t::push_lazy: the store retains the ctx until
 * the entry is evicted, then runs fetch_ctx_destroy exactly once. */
typedef struct PJ_parser_object_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [stream-thread] Eager push of serialized payload bytes into the bound
   * object topic. Store copies the bytes. */
  bool (*push_owned)(void* ctx, int64_t timestamp_ns, const uint8_t* data, size_t size, PJ_error_t* out_error)
      PJ_NOEXCEPT;

  /* [stream-thread] Lazy push. Rarely used from parsers (a delegated parser
   * is given already-available bytes by the host), but exposed for
   * transform-style parsers that produce fetch closures. */
  bool (*push_lazy)(
      void* ctx, int64_t timestamp_ns, PJ_lazy_fetch_fn_t fetch_fn, void* fetch_ctx, void (*fetch_ctx_destroy)(void*),
      PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_parser_object_write_host_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const PJ_parser_object_write_host_vtable_t* vtable;
} PJ_parser_object_write_host_t;

/* ==========================================================================
 * Object-store read host (protocol v4)
 *
 * Exposed to Toolbox plugins that want to read back ObjectStore entries —
 * typically transformer-style plugins that consume bytes and emit results.
 * Read access uses an opaque OWNING handle model: each successful
 * read_latest_at allocates a refcounted box that keeps the bytes alive
 * independent of the store's internal state (matches
 * `shared_ptr<const vector<uint8_t>>` in the C++ API).
 *
 * Lifetime contract: a handle remains valid until the consumer calls
 * release_bytes(handle). Eviction, concurrent writes, even topic removal
 * cannot invalidate a handle that is already held.
 * ========================================================================== */

/* Opaque handle. The host allocates one per successful read; the plugin
 * releases via vtable->release_bytes. The pointer value is never
 * dereferenced by the plugin. */
struct PJ_object_bytes_handle_s;
typedef struct PJ_object_bytes_handle_s* PJ_object_bytes_handle_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read.
 *
 * get_bytes / release_bytes take the handle directly (no ctx) because the
 * handle itself is a heap-allocated box the host owns; its internal state
 * is all the free/read operation needs. */
typedef struct PJ_object_read_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;

  /* [main-thread] Look up a topic by name. Returns {id=0} on miss. */
  PJ_object_topic_handle_t (*lookup_topic)(void* ctx, PJ_string_view_t topic_name) PJ_NOEXCEPT;

  /* [main-thread] Enumerate all object topics the toolbox can see. The
   * caller passes a buffer of capacity `buffer_capacity`; the host writes
   * up to that many handles and always sets *out_count to the TOTAL number
   * of topics (so the caller can detect truncation and resize). */
  bool (*list_topics)(
      void* ctx, PJ_object_topic_handle_t* out_buffer, size_t buffer_capacity, size_t* out_count,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Return the topic's metadata JSON — a pointer stable for
   * the topic's lifetime. Returns NULL on bad handle. */
  const char* (*topic_metadata)(void* ctx, PJ_object_topic_handle_t topic)PJ_NOEXCEPT;

  /* [main-thread] Fetch the entry at-or-before `timestamp_ns`. On success
   * allocates an owning handle; caller releases via release_bytes.
   * out_timestamp (optional) receives the entry's actual timestamp. On
   * miss returns false with *out_handle=NULL and out_error populated. */
  bool (*read_latest_at)(
      void* ctx, PJ_object_topic_handle_t topic, int64_t timestamp_ns, PJ_object_bytes_handle_t* out_handle,
      int64_t* out_timestamp, PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [thread-safe] Expose the bytes behind an owning handle. View is valid
   * until release_bytes(handle). Safe to call from decoder worker threads. */
  void (*get_bytes)(PJ_object_bytes_handle_t handle, const uint8_t** out_data, size_t* out_size) PJ_NOEXCEPT;

  /* [thread-safe] Release an owning handle. Idempotent on NULL. */
  void (*release_bytes)(PJ_object_bytes_handle_t handle) PJ_NOEXCEPT;

  /* [main-thread] Entry count for a topic. 0 on bad handle. */
  size_t (*entry_count)(void* ctx, PJ_object_topic_handle_t topic) PJ_NOEXCEPT;

  /* [main-thread] Time range [min, max] for a topic. Returns false if the
   * topic is unknown or empty. */
  bool (*time_range)(void* ctx, PJ_object_topic_handle_t topic, int64_t* out_min_ts, int64_t* out_max_ts) PJ_NOEXCEPT;
} PJ_object_read_host_vtable_t;

/* ABI-FROZEN: fat pointer layout permanent. */
typedef struct {
  void* ctx;
  const PJ_object_read_host_vtable_t* vtable;
} PJ_object_read_host_t;

/**
 * Colormap registry service (v4).
 *
 * Independent host-provided service for toolbox plugins that want to
 * publish named colormap callbacks.
 */
typedef struct PJ_colormap_registry_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /* [main-thread] Register a named colormap. eval_fn is invoked later from
   * the main GUI thread when rendering. */
  bool (*register_map)(
      void* ctx, PJ_string_view_t name, const char* (*eval_fn)(double value, void* user_ctx), void* user_ctx,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /* [main-thread] Unregister a previously registered colormap. */
  bool (*unregister_map)(void* ctx, PJ_string_view_t name, PJ_error_t* out_error) PJ_NOEXCEPT;
} PJ_colormap_registry_vtable_t;

typedef struct {
  void* ctx;
  const PJ_colormap_registry_vtable_t* vtable;
} PJ_colormap_registry_t;

#ifdef __cplusplus
}
#endif

#endif
