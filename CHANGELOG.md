# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [0.18.0]

### Feature: typed table sort keys — numeric columns sort numerically (MINOR)

A table cell crossed the dialog protocol as text only, so the host could compare
nothing but the rendered string and every numeric column sorted
lexicographically (`720` before `7` before `65`). Sort keys now travel beside the
display text. Backward-compatible JSON additions — no C ABI change,
`PJ_DIALOG_PROTOCOL_VERSION` unchanged, `abi/baseline.abi` unchanged:

- `PJ::TableItem { std::string text; std::optional<NumericValue> value; }` — the
  display string and the ordering truth. Constructors cover a text cell, a
  numeric cell (`std::to_string` rendering), and a numeric cell with plugin-owned
  rendering (`TableItem(v, "5.60536e+08")`), which also expresses a *hidden* key:
  display a date, sort on `int64` nanoseconds. `NumericValue` keeps the native
  width, so `int64`/`uint64` values past 2⁵³ round-trip exactly rather than
  degrading through a `double`.
- `WidgetData::setTableRows(name, vector<vector<TableItem>>)` — an overload beside
  the string one. Emits display text as the usual `rows` plus a **sparse**
  `column_values` map (`{"<col>": [v0, v1, …]}`): only columns where some cell has
  a value, with valueless cells as `null`, and the key omitted entirely when no
  cell has one. Old hosts never look for it and read `rows` exactly as before.
  Deriving both keys from one `TableItem` matrix means display and value cannot
  desync.
- `WidgetData::setTableSortIndicator(name, column, ascending)` → `sort_indicator`
  — draws the header arrow for a table that sorts itself via `onHeaderClicked`,
  which Qt otherwise leaves unpainted because its own sorting is off. Cosmetic
  only.
- `WidgetDataView::tableColumnValues(name)` / `tableSortIndicator(name)` for host
  implementations. `tableColumnValues` drops a column whose value count disagrees
  with `rows` or whose key is not a non-negative integer, rather than sorting some
  rows by number and the rest by text.
- `WidgetDataView::tableRows()` hardening: a non-string cell now yields an empty
  string instead of being skipped. Skipping shifted every later cell one column
  left and mis-aligned the row against its headers.
- The plain-string `setTableRows` overload now erases any `column_values` a
  previous typed delivery left on the same table, so alternating overloads can
  never pair fresh rows with stale sort keys.

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

### Feature: QDateTimeEdit event surface (MINOR)

The dialog protocol's QDateTimeEdit setters (`setDateTime` / `setDateTimeRange`,
shipped earlier) gain their missing event direction, so the widget becomes an
input, not just a display (backward-compatible JSON addition; no C ABI change,
`PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetEvent` key: `datetime_iso` (string — the edited datetime, wall-clock
  local ISO-8601; fractional seconds only for ms-precision editors), with
  `WidgetEventBuilder::dateTimeChanged()` on the host side,
  `WidgetEvent::dateTimeChanged()` on the plugin side, and typed dispatch via
  `DialogPluginTyped::onDateTimeChanged()`.
- Docs: `dialog-sdk-reference.md` gains the previously missing QDateTimeEdit
  section; the setter contract is clarified (empty/unparsable strings are
  ignored — the widget keeps its current value; `QDateEdit`/`QTimeEdit`
  subclasses share the binding).

### Fix: PJ_DIALOG_PLUGIN two-arg form broke under the MSVC legacy preprocessor (PATCH-level)

The overload-by-arg-count dispatch behind `PJ_DIALOG_PLUGIN(Class, kManifest)`
mis-expanded under MSVC's traditional preprocessor (the default without
`/Zc:preprocessor`): `__VA_ARGS__` was forwarded into the selector as a single
glued argument, so the two-arg call silently picked the one-arg legacy branch
with `"Class, kManifest"` as the class name (C2064/C2912 at the call site —
found via pj-official-plugins#230's Windows CI).

- `PJ_DIALOG_PLUGIN_EXPAND` rescan added to the dispatch — the macro now
  expands identically under both MSVC preprocessors and GCC/Clang. No ABI or
  call-site change; purely a header fix.
- `pj_dialog_sdk` no longer injects `INTERFACE /Zc:preprocessor` into
  consumers — the flag is unnecessary now, and it never reached Conan
  consumers anyway (CMakeDeps regenerates targets from `package_info()` and
  drops upstream `INTERFACE_COMPILE_OPTIONS`, which is how the breakage
  shipped unnoticed). Consumers that want the conformant preprocessor set it
  themselves.
- New compile-only regression target `mock_dialog_legacy_pp_plugin` builds the
  mock dialog with `/Zc:preprocessor-` on MSVC, so Windows CI now exercises
  the legacy-preprocessor path the SDK's conformant-only build never covered.

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
