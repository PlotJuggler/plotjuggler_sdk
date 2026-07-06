/**
 * @file data_source_topic_subscription.h
 * @brief "pj.topic_subscription.v1" plugin extension — host-driven lazy
 *        per-topic subscription for DataSource plugins.
 *
 * A DataSource plugin that declares PJ_DATA_SOURCE_CAPABILITY_LAZY_SUBSCRIPTION
 * connects without subscribing to any topic. It advertises the available topic
 * set through the runtime host's `set_advertised_topics` tail slot (see
 * data_source_protocol.h) and lets the host drive which topics are actually
 * produced through this extension:
 *
 *   host: get_plugin_extension(ctx, "pj.topic_subscription.v1")
 *      -> PJ_topic_subscription_extension_t*
 *   host: ext->set_active_topics(ctx, names, count, &err)   // desired set
 *
 * The verb is DECLARATIVE: the host always sends the full desired-active set
 * and the plugin reconciles — subscribing topics newly present, unsubscribing
 * topics absent. Declarative reconcile stays correct across reconnects and
 * mid-stream advertise/unadvertise without any imperative pairing.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_DATA_SOURCE_TOPIC_SUBSCRIPTION_H
#define PJ_DATA_SOURCE_TOPIC_SUBSCRIPTION_H

#include <stdbool.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Extension id passed to PJ_data_source_vtable_t::get_plugin_extension. */
#define PJ_TOPIC_SUBSCRIPTION_EXTENSION_ID "pj.topic_subscription.v1"

/**
 * Returned by get_plugin_extension(ctx, PJ_TOPIC_SUBSCRIPTION_EXTENSION_ID).
 * Static plugin-owned POD, valid for the plugin-instance lifetime.
 */
typedef struct PJ_topic_subscription_extension_t {
  uint32_t struct_size; /**< sizeof(PJ_topic_subscription_extension_t) — future tail slots. */

  /**
   * [stream-thread] Set the host's desired-active topic set (full set, by
   * topic name as advertised). The plugin reconciles its actual
   * subscriptions: subscribe topics newly present, unsubscribe topics
   * absent. The plugin MAY additionally keep its own always-on
   * subscriptions (e.g. a config-selected eager floor); the effective
   * subscribed set is the union.
   *
   * Threading contract: the host invokes this on the SAME thread that calls
   * the plugin's poll(), strictly serialized with poll()/start()/stop() —
   * never concurrently with any of them. May be called between start()
   * returning and the first poll(); never before start() or after stop().
   * Unknown (not currently advertised) names are ignored, not errors — the
   * host and the source's live topic set converge asynchronously.
   *
   * @p ctx is the plugin instance context (the same pointer passed to every
   * PJ_data_source_vtable_t slot). Name views are valid only for the call.
   */
  bool (*set_active_topics)(void* ctx, const PJ_string_view_t* topic_names, uint64_t count, PJ_error_t* out_error)
      PJ_NOEXCEPT;
} PJ_topic_subscription_extension_t;

#ifdef __cplusplus
}
#endif

#endif  // PJ_DATA_SOURCE_TOPIC_SUBSCRIPTION_H
