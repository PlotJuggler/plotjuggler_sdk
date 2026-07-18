# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [0.18.0]

### Feature: batch table deltas for large QTableWidgets (MINOR)

Mutate a table without resending the whole `rows` array (backward-compatible
JSON addition; no C ABI change, `PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetData::appendTableRows` / `updateTableCells` / `removeTableRows` write
  a per-widget `table_delta` object (`seq`, `append`, `update_cells`,
  `remove_rows`). `seq` is plugin-owned; hosts apply a delta only when its seq
  differs from the last one applied to that widget, in the order update_cells
  → remove_rows → append, with all indexes addressing the pre-delta table.
  New `TableCellUpdate` struct.
- `WidgetDataView::tableDelta()` returns the decoded `TableDeltaView` for host
  implementations (strict: a malformed op rejects the whole delta;
  `remove_rows` arrives descending and duplicate-free), with
  `tableDeltaSeq()` as the cheap staleness pre-check.
- `dialog-plugin-guide.md` gains a "Large tables" section documenting the
  omit-unchanged-fields pattern (with measured costs) and the delta ops.
- SDK-side only: hosts apply `table_delta` from the companion PlotJuggler
  change onward; older hosts ignore the key (harmless no-op).

## [0.17.0]

### Feature: dialog-protocol additions for deletable lists and chart/list placeholders (MINOR)

Backward-compatible JSON protocol additions (unknown keys are ignored by old
hosts/plugins; no C ABI change, `PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetData` per-widget keys via `setListItemsDeletable` / `setListPlaceholder` /
  `setChartPlaceholder`: `list_deletable` (bool — rows grow a delete affordance), `list_placeholder` / `chart_placeholder` (string — centered
  hint shown while the list/chart is empty).
- `WidgetEvent` key: `item_delete_index` (int — row whose delete affordance
  was clicked), with `WidgetEventBuilder::itemDeleteRequested()` on the host
  side, `WidgetEvent::itemDeleteRequestedIndex()` on the plugin side, and
  typed dispatch via `DialogPluginTyped::onItemDeleteRequested()`.
- `WidgetDataView` accessors (`listDeletable` / `listPlaceholder` /
  `chartPlaceholder`) for host implementations.

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
