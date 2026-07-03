# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [Unreleased] — on branch `feature/plot-markers`, not yet publicly tagged

### DataSource: per-topic pause/resume — advertise-without-subscribe + demand-driven control (ADDITIVE)

Lets a streaming source expose *all* its topics cheaply while only transmitting data
for topics the host is actually displaying. Strictly additive (tail slot + plugin
extension, both `struct_size`/`PJ_HAS_TAIL_SLOT`-gated); every existing plugin keeps
working with no recompile, and a new plugin degrades gracefully on an old host.

- **New capability** `PJ_DATA_SOURCE_CAPABILITY_PER_TOPIC_PAUSE = 1 << 6` (C++ mirror
  `kCapabilityPerTopicPause`).
- **Plugin → host advertise**: new runtime-host tail slot
  `notify_available_topics(topics, count)` carrying `PJ_available_topic_t`
  (`topic_name`/`parser_encoding`/`type_name`/`schema`, mirroring
  `PJ_parser_binding_request_t`), so the host can list and a-priori classify topics
  (via `classify_schema`) before any data flows. C++ helper
  `DataSourceRuntimeHostView::notifyAvailableTopics(Span<const AvailableTopic>)`.
- **Host → plugin control**: new `get_plugin_extension("pj.topic_subscription.v1")`
  extension `PJ_topic_subscription_v1_t::set_active_topics(names, count)` — declarative
  full active-set; the plugin diffs and subscribes/unsubscribes. Host wrapper
  `DataSourceHandle::setActiveTopics(...)` (no-op when the extension is absent).
- Runtime-host vtable size grows 96 → 104 (`notify_available_topics` at offset 96);
  `PJ_ABI_VERSION` (5), `PJ_DATA_SOURCE_PROTOCOL_VERSION` (4), and
  `PJ_DATA_SOURCE_MIN_VTABLE_SIZE` (128) unchanged. `abi/baseline.abi` unchanged
  (additions only).

### Host service: markers + transforms unified into `pj.data_processors.v1` (UNRELEASED BREAK)

The two whole-series host-driven services were collapsed into ONE contract — Pablo's
`pj.data_processors.v1` — with a string `kind` discriminator, so a plugin chooses
`markers`/`transform` (and future engines) from the **same** `create` call:

- **Removed** `PJ_markers_host_vtable_t` / `MarkersHostService` (the old `pj.markers.v1`,
  which never left this branch) and the separate `PJ_generators_host_vtable_t` /
  `GeneratorsHostService` (`pj.generators.v1`) that briefly carried the markers backend.
- **Generalized** `pj.data_processors.v1` `create_data_processor` to the unified shape:
  added `kind` (`"transform"` → DerivedEngine timeseries; `"markers"` → ObjectStore
  PlotMarkers), `language`, a `flags` bitset with `PJ_DATA_PROCESSOR_FLAG_EPHEMERAL`,
  and an `out_topics` resolved-name return. The separate `create_data_processor_ephemeral`
  slot is **removed** — ephemeral preview is now the EPHEMERAL flag on `create`.
- **Generalized** `validate_data_processor_script` to take a `kind` argument.
- `DataProcessorsHostView` gains the unified `create(kind, …)` plus thin convenience
  shims `createTransform` / `createEphemeralTransform` / `createMarkers`; the
  `createTransform`/`createEphemeralTransform`/`remove`/`list`/`recipeOf` signatures are
  unchanged, so existing transform call-sites are unaffected.

**Migration for `pj.data_processors.v1` consumers (Pablo's transform-editor work).** The
ABI `create_data_processor` and `validate_data_processor_script` slot signatures changed,
so a host that IMPLEMENTS the service (e.g. `DataProcessorsRuntimeHost`) must update its
vtable fill; callers using `DataProcessorsHostView::createTransform` keep working via the
shim, while `validateScript` call-sites gain a leading `kind` argument (`"transform"`).

**Versioning note.** This is an ABI/API change to a service that merged to `main`. It
ships as `0.13.0` because no PUBLIC tag has carried `pj.data_processors.v1` yet, so no
released plugin is broken. **The first public release that carries the unified
`pj.data_processors.v1` must be tagged `1.0.0`** per the pre-1.0 break rule in `CLAUDE.md`.
