# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [Unreleased]

### 0.15.0 — Lazy per-topic subscription for DataSources (MINOR, additive only)

Streaming sources can now connect without subscribing, advertise their full topic
set to the host, and subscribe/unsubscribe individual topics on host demand (driven
by which topics actually have consumers — plotted curves, scene layers, transform
inputs). Three additive pieces; every existing plugin keeps working with no
recompile (`abidiff` additions only):

- **New capability bit** `PJ_DATA_SOURCE_CAPABILITY_LAZY_SUBSCRIPTION` (1<<6, C++
  mirror `kCapabilityLazySubscription`) declaring that a source supports the flow.
- **New runtime-host tail slot** `set_advertised_topics` on
  `PJ_data_source_runtime_host_vtable_t`: declarative full-set topic advertise with
  parser metadata (reuses `PJ_parser_binding_request_t`), so advertised topics can
  appear in the host catalog with zero data. C++ wrapper
  `DataSourceRuntimeHostView::setAdvertisedTopics` returns the distinct error
  "host does not support lazy subscription" on old hosts so plugins can fall back
  to eager mode.
- **New plugin extension** `"pj.topic_subscription.v1"`
  (`pj_base/data_source_topic_subscription.h`): `set_active_topics` — the host's
  declarative desired-active set; the plugin reconciles subscriptions. Invoked on
  the poll thread, strictly serialized with poll/start/stop.
  `DataSourcePluginBase` wires it automatically to the new
  `onActiveTopicsChanged()` virtual when the capability is declared;
  `DataSourceHandle::topicSubscriptionExtension()` / `setActiveTopics()` on the
  host side.
- **New parser tail slot** `describe_schema_columns` on
  `PJ_message_parser_vtable_t`: the flattened scalar-column manifest for a bound
  schema (JSON `[{"path","type"}]`, parse()-emission order), letting hosts
  pre-create catalog columns for topics with no samples yet. C++ helpers:
  `MessageParserPluginBase::describeSchemaColumns` returning
  `std::vector<sdk::ColumnSpec>` (base serializes the wire JSON),
  `MessageParserHandle::describeSchemaColumns` host-side.

## [0.14.0 and earlier] — previously on branch `feature/plot-markers`, not yet publicly tagged

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
