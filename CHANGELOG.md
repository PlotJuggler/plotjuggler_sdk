# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [0.17.0] — Unreleased, on branch `feature/playback-viewport-services`

> Version number is provisional: 0.16.0–0.16.2 were released upstream while this
> branch was in flight (without these services); renumber at release if needed.

### Added — host services `pj.playback.v1` and `pj.viewport.v1` (MINOR, additions only)

Two new optional host services so a plugin (first consumer: the Assistant Agent
toolbox) can drive the app like a user — transport and zoom — without any new
executable surface crossing the ABI:

- **`pj.playback.v1`** (`PJ_playback_host_vtable_t`, `sdk::PlaybackHostView`,
  `sdk::PlaybackHostService`): `play` / `pause` / `seek` / `set_playback_rate` /
  `get_state` (ABI-frozen `PJ_playback_state_t` snapshot) / `to_display_time`
  (absolute int64 ns → display-axis seconds, per-topic dataset offset;
  current-frame semantics). All times are display-axis seconds.
- **`pj.viewport.v1`** (`PJ_viewport_host_vtable_t`, `sdk::ViewportHostView`,
  `sdk::ViewportHostService`): `zoom_to_time_range` (every open time plot's X
  window; per-plot Y preserved; XY/empty plots untouched) and `zoom_reset`
  (fit all).

No existing struct or vtable slot was touched — every already-built plugin keeps
working with no recompile (`abidiff`: additions only).

### Fixed — `deserializePlotMarkers` accepts the empty buffer as an empty set

An empty buffer is the canonical proto encoding of an empty `PlotMarkers` set —
the "clear my markers" tombstone a producer publishes to a replace-only store —
but the decoder rejected `size == 0` as an error, making the tombstone
unrepresentable on the wire. It now decodes to an empty set; null-with-size,
truncated, and malformed payloads still error. (Logically part of the
plot-markers feature line; riding this branch until the PRs are re-sorted.)

## [0.16.2]

### Fix: 0.16.1's Apple `to_chars` guard tested a misspelled macro and never engaged (PATCH)

The 0.16.1 fallback guard checked `__ENVIRONMENT_MACOS_VERSION_MIN_REQUIRED__`,
which does not exist — the compiler defines
`__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__` (verified against clang's
`OSTargets.cpp`) — so `defined(...)` was always false and every Apple build
still compiled the floating-point `std::to_chars` path. The guard now uses the
correct macro and is fail-safe: any Apple target not provably macOS ≥ 13.3
(including non-macOS Apple platforms, which spell the macro differently) takes
the `snprintf` fallback. Guard behavior verified by preprocessing the header's
exact `#if` line under deployment targets 13.0 / 13.3 / 26.0 / undefined /
non-Apple.

## [0.16.1]

### Fix: double formatting in `plugin_data_api.hpp` on older Apple deployment targets (PATCH)

`SettingsStoreView::setValue(key, double)` — an inline method in an installed
public header — used the floating-point `std::to_chars` overload, which libc++
marks *unavailable* below a macOS 13.3 deployment target. Any macOS build with
an older target (Conan Center's build farm, or a consumer setting
`CMAKE_OSX_DEPLOYMENT_TARGET`) failed to compile. On such targets the method
now falls back to `snprintf("%.17g")`, which round-trips any IEEE double; all
other platforms keep the shortest-round-trip `std::to_chars` path. Behavioral
difference on the fallback path only: non-minimal digit strings (e.g. `0.1` →
`0.10000000000000001`) — values are preserved exactly.

Also seeds the Conan Center recipe at `0.16.1` (its `tool_requires
cmake/[>=3.22]` fix from `618b92e` plus this header fix are both required for
CCI's macOS builders).

## [0.16.0]

### SDK slimming: duplicate-resolution catalog moved to the host (HOST-FACING REMOVAL)

`PluginRuntimeCatalog` — the layer that resolved duplicate plugin ids across scan
folders by authoritative → compatibility → version → folder priority — is host
*policy*, not a plugin-facing mechanism, so it moved to the PlotJuggler app
(`pj_runtime`). The SDK stays a thin discovery + loader layer: `scanPluginDsos`,
`inspectPluginDso`, the RAII loaders, and the C ABI are unchanged. (#144)

- **Removed** `pj_plugins/host/plugin_runtime_catalog.hpp` and the installed
  `pj_plugin_runtime_catalog` library (dropped from the `pj_plugin_host`
  umbrella and the install set).

**Versioning note.** No plugin links `PluginRuntimeCatalog` (it was host-side
only) and `abi/baseline.abi` is unchanged, so by the plugin-impact rule this is
a MINOR, not a MAJOR. It does remove host-facing public API: a *host* built
against 0.15.0's catalog must adopt the app-side implementation (`pj_runtime`).

## [0.15.0]

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

## [0.14.0]

### Host service: markers + transforms unified into `pj.data_processors.v1`

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

**Versioning note.** This was an ABI/API break to a service that had merged to `main`.
It shipped as `0.14.0` rather than `1.0.0` because no public tag had ever carried
`pj.data_processors.v1`, so no released plugin was broken by the change.
