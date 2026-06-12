/**
 * @file filter_registry_abi.h
 * @brief C ABI for the Filter Transform Registry host service (pj.filter_registry.v1).
 *
 * The host owns a single FilterTransformFactory exposed to plugins via this
 * service. Plugins register their transform classes during loaderInit (each
 * registration carries a per-DSO library_owner token so the host pins the
 * plugin DSO while any registered factory function is reachable), and resolve
 * transforms by id from the same registry — preview, layout restore, and the
 * streaming read path all go through the same instances.
 *
 * Every vtable slot is PJ_NOEXCEPT. Throws across the boundary terminate.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_FILTER_REGISTRY_ABI_H
#define PJ_FILTER_REGISTRY_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Service id for the filter registry. v1 == minimum protocol version. */
#define PJ_FILTER_REGISTRY_SERVICE_NAME "pj.filter_registry.v1"

/** Opaque handle to a FilterTransform instance. Lifetime is owned by the
 *  caller; release via the vtable's `destroy_transform` slot. */
typedef struct PJ_filter_transform PJ_filter_transform_t;

/** Factory function the plugin registers. The host invokes it (under the
 *  plugin's pinned DSO via the library_owner token) to create instances on
 *  demand. Must not throw across the boundary; return NULL on failure. */
typedef PJ_filter_transform_t* (*PJ_filter_transform_factory_fn)(void* user_ctx) PJ_NOEXCEPT;

/** Host-owned destructor for an instance created by the factory above. The
 *  host calls into this when releasing the instance — keeps the delete on
 *  the same side that did the new (mirrors `unique_ptr<…, deleter>`). */
typedef void (*PJ_filter_transform_deleter_fn)(PJ_filter_transform_t*) PJ_NOEXCEPT;

/** Plugin's registration payload. The host stores a copy. */
typedef struct PJ_filter_transform_registration_t {
  /** Stable id (e.g. "moving_average"). Must outlive the registration. */
  PJ_string_view_t id;
  /** Constructor invoked by host to materialise instances. */
  PJ_filter_transform_factory_fn factory;
  /** Destructor for instances produced by `factory`. */
  PJ_filter_transform_deleter_fn deleter;
  /** Opaque context passed to `factory` (typically the plugin's own state
   *  or nullptr; the host does not interpret it). */
  void* factory_ctx;
} PJ_filter_transform_registration_t;

/* ABI-APPENDABLE: new slots may be added at the tail; struct_size gates read. */
typedef struct PJ_filter_registry_vtable_t {
  uint32_t protocol_version;
  uint32_t struct_size;

  /**
   * Register a transform under @p reg.id. The library_owner token (opaque
   * to the vtable but typically the plugin DSO handle) pins the plugin so
   * its factory/deleter code stays reachable until every produced instance
   * is destroyed. Replaces any previous registration under the same id.
   *
   * Thread-class: [thread-safe]
   */
  bool (*register_transform)(
      void* ctx, PJ_filter_transform_registration_t reg, void* library_owner,
      PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * Drop the registration under @p id. Existing instances stay alive
   * (their deleter is still callable through the cached library_owner ref).
   *
   * Thread-class: [thread-safe]
   */
  bool (*unregister_transform)(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * Resolve a transform id and create an instance. The caller owns the
   * returned handle and must release it via the same registration's
   * deleter (reachable through `lookup_deleter` below).
   *
   * Thread-class: [thread-safe]
   */
  PJ_filter_transform_t* (*create_transform)(
      void* ctx, PJ_string_view_t id, PJ_error_t* out_error) PJ_NOEXCEPT;

  /**
   * Look up the deleter for instances created under @p id. Lets the caller
   * destroy an instance even after the registration entry has been
   * replaced (the deleter pointer + library_owner ref are captured at
   * `create_transform` time and remain valid until the instance dies).
   *
   * Thread-class: [thread-safe]
   */
  PJ_filter_transform_deleter_fn (*lookup_deleter)(void* ctx, PJ_string_view_t id) PJ_NOEXCEPT;

  /**
   * Iterate all registered ids in registration order. The host fills @p
   * out_ids up to @p capacity entries and writes the actual count to
   * @p out_count. Pass capacity==0 to size the buffer first.
   *
   * Thread-class: [thread-safe]
   */
  void (*list_ids)(
      void* ctx, PJ_string_view_t* out_ids, size_t capacity, size_t* out_count) PJ_NOEXCEPT;
} PJ_filter_registry_vtable_t;

/** Pinned minimum vtable size for v1.0; never grows when tail slots are
 *  appended. Loaders reject services whose `struct_size < this`. */
#define PJ_FILTER_REGISTRY_MIN_VTABLE_SIZE \
  (offsetof(PJ_filter_registry_vtable_t, list_ids) + sizeof(void (*)(void*, PJ_string_view_t*, size_t, size_t*)))

/** Fat pointer for the filter registry service. */
typedef struct PJ_filter_registry_t {
  void* ctx;
  const PJ_filter_registry_vtable_t* vtable;
} PJ_filter_registry_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PJ_FILTER_REGISTRY_ABI_H
