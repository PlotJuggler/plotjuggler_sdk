# Plugin System Requirements

## 1. Purpose

PlotJuggler loads functionality at runtime from shared libraries (`.so` /
`.dylib` / `.dll`). The plugin system defines a stable, portable boundary
between the host application and these libraries, plus convenience layers that
make plugin development practical.

Five plugin families exist:

- **DataSource** — acquires data from files, network streams, or hardware.
  Write-only; one instance per application data source.
- **MessageParser** — decodes raw byte payloads (JSON, Protobuf, ROS, etc.)
  into named fields. Headless; one instance per (data source, topic) pair.
- **Dialog** — drives a Qt Designer `.ui` form through a stable C ABI.
  The plugin describes UI as XML, exchanges state as JSON, and handles events
  via typed callbacks — without linking Qt itself.
- **Toolbox** — stateful interactive tools with full read+write access to host
  data. Long-lived, UI-driven; may create new data sources or transform data.
- **Widget** — dockable panels whose content is a widget the plugin creates
  with the GUI toolkit itself. Consumes values pushed by the host; never reads
  or writes the data store. One library offers several widget types.

## 2. Design Principles

1. **Stable C ABI boundary.** Binary compatibility does not depend on compiler,
   C++ standard library, or Qt ABI. No C++ classes, STL containers, Qt objects,
   or exceptions cross the boundary.

2. **C++ SDK convenience layer.** Plugin authors subclass a base class, override
   virtuals, and export with a macro. The SDK generates C ABI trampolines with
   full exception safety.

3. **No Qt dependency in the plugin SDK.** Installed plugins link the
   `plotjuggler_sdk::plugin_sdk` umbrella, which supplies `pj_base` plus the
   parser/dialog/widget authoring headers without linking Qt. Qt is a host-side
   concern. **Exception:** a Widget plugin links Qt itself, because it creates
   a Qt widget; only the opaque pointer to that widget crosses the ABI, and no
   SDK header or library includes or links Qt. A plugin of any other family
   must not depend on Qt (the Widget plugin is the only place the exception
   applies, and it is why Widget code lives in a separate library even when the
   same product also ships a plugin of another family).

4. **Version negotiation.** Each protocol carries `protocol_version` and
   `struct_size` for forward/backward compatibility.

5. **Instance isolation.** Multiple instances of the same plugin type run
   concurrently with instance-local state.

### 2.1 Compatibility vocabulary

Plugin compatibility uses separate values. They are not aliases and a
manifest must not substitute one for another:

| Value | Meaning | Enforcement |
|---|---|---|
| `min_sdk_required` | Minimum PlotJuggler SDK contract whose features the plugin actually requires (the functioning floor) | Hard host-side load gate before plugin instantiation |
| `suggested_sdk_version` | Full-feature floor: the SDK release at which every host surface the plugin uses is available. Declared together with a `floor_test` whenever it is above `min_sdk_required` | Informational; never an admission criterion |
| `min_plotjuggler_version` | Deprecated application-release expectation | Decoded for back-compat and never gates |
| ABI/dialog protocol version | Binary C ABI and dialog C/wire contract compatibility | Strict vtable, `struct_size`, and protocol validation |

`tools/feature_floors/feature_floor_check.py`, installed with
`feature_floors.json` in `share/plotjuggler_sdk/`, validates a manifest's floors
for plugin repositories: `min_sdk_required` must cover every non-negotiated host
surface the plugin's sources use, and when a used surface was introduced after
it, the manifest declares `suggested_sdk_version` (the newest introducing
release) plus the `floor_test` gtest that proves the plugin works against a
floor-level host.

`min_sdk_required` is not automatically the SDK version used to compile the
plugin. A plugin compiled with a newer SDK declares the oldest SDK contract
that provides every feature it actually uses.

Manifest rules for `min_sdk_required`:

- The key is optional. Missing or an empty string means no SDK floor was
  declared and remains loadable.
- A present value must be a JSON string containing one concrete, three-part
  SemVer 2.0.0 version: `MAJOR.MINOR.PATCH`, optionally followed by a
  pre-release and/or build suffix. Ranges and wildcards are not supported
  because the field already expresses a minimum.
- A present non-string or malformed version invalidates the manifest. A valid
  value newer than the host SDK is hard-rejected by the host before plugin
  instantiation.
- Precedence follows SemVer 2.0.0. A pre-release sorts below its corresponding
  release (`0.21.0-pre < 0.21.0`); numeric pre-release identifiers compare
  numerically and sort below alphanumeric identifiers; a shorter identifier
  list sorts first when the shared prefix is equal. Build metadata never
  affects precedence (`0.21.0+linux == 0.21.0+windows`).

The SDK exposes its configured contract identity in
`pj_base/sdk/version.hpp` (`PJ_SDK_VERSION_*`,
`PJ_SDK_VERSION_AT_LEAST()`, and `PJ::sdkVersion()`) and provides the shared
Qt-free `PJ::SemVer` parser/comparator in `pj_base/sdk/semver.hpp`. For a
development pre-release (`0.37.0-dev.1`) the `PJ_SDK_VERSION_*` macros carry
only the numeric core `0.37.0` and `sdkVersion()` the full string, so a host
must compare `SemVer::parse(sdkVersion())` against `min_sdk_required` to
reject a plugin that requires `0.37.0`.

## 3. Plugin Families and Their Roles

### DataSource

Unified replacement for legacy `DataLoader` and `DataStreamer`. Handles both
finite imports (CSV, Parquet, MCAP) and continuous streams (MQTT, ZMQ, UDP).

- Write-only — no read access to existing host data.
- May decode data directly (direct ingest) or hand raw payloads to a
  MessageParser (delegated ingest).
- Topics may appear lazily at runtime.
- Lifecycle: create → bind → load_config → start → poll* → stop → destroy,
  signalled by the `start`/`poll` results, `request_stop`, `is_stop_requested`
  and `complete_ingest`. Reported states and pause/resume are advisory.

### MessageParser

Independent decoder, typically driven by a DataSource via the host.

- Write-only, topic-scoped — fields are namespaced under the assigned topic.
- Stateless across messages (but may cache schema-derived lookup tables).
- May export a dialog vtable from the same `.so` for configuration UI
  (e.g. schema selection, include paths). The dialog is an independent owned
  instance — the host creates it and bridges config to parser instances via
  JSON.
- Lifecycle: create → bind → parse* → destroy.
- `MessageParserPluginBase` always answers `pj.parser_route_claims.v1` (a type
  without a handler is declined) and answers `pj.parser_functional.v2` only when
  the bound type has a handler (before binding, when any handler is registered).
  Functional scalar and object results cross only through synchronous
  caller-owned C sinks; v2 may represent one eligible object bulk field as a
  payload-relative splice. New host code reads results through v2 rather than
  casting the opaque context to `MessageParserPluginBase`; the deprecated bridge
  below remains supported.
- Route classification reports exact handler-table claims only. The host owns
  each encoding's universal wildcard scalar claim and resolves scalar and object
  routes independently.
- Scalar views are callback-duration-only. Canonical objects cross as a stable
  numeric builtin tag plus canonical wire bytes and are decoded into host-owned
  storage before the extension call returns. No plugin allocator, destructor,
  RTTI object, `BuiltinObject`, `std::string`, or `BufferAnchor` crosses back.
- The object route consumes exactly one `PJ_payload_t` anchor reference and
  releases it exactly once on success and every failure path. This permits
  zero-copy input when the host already owns an anchored payload; output still
  pays the explicit canonical serialization boundary.
- Extension presence is truthful, not merely SDK-version-based: a rebuilt
  parser that still overrides only legacy `parse()` must not advertise the
  functional extension. Hosts query after `bind_schema` and do not cache an
  earlier absence.
- A host may read objects through the deprecated direct-C++ bridge (a cast of
  the parser context to `MessageParserPluginBase*`) instead of v2. The bridge
  is supported through 1.x. While any supported host uses it, the layouts it
  shares with the plugin are frozen: the `MessageParserPluginBase` member
  prefix and virtual order, `sdk::ObjectRecord`, `sdk::BuiltinObject`,
  `PJ::Expected` and every builtin object struct. A 1.x release lifts the
  freeze once the host reads objects through v2 only.

### Dialog

Host-rendered UI component. The plugin provides `.ui` XML and exchanges
widget state as JSON; the host renders the widgets and relays events.

- Reactive loop: host reads `widget_data()`, user interacts, host dispatches
  event, plugin returns `true` if state changed, host re-reads `widget_data()`.
- `onTick()` enables async background work (polling servers, discovery).
- Used standalone or embedded inside DataSource/Toolbox plugins.

### Toolbox

Stateful interactive tools with full data access.

- Read+write access — can read existing data, write new data, create new
  data sources, perform destructive updates.
- No state machine — either alive or destroyed.
- UI via the dialog protocol.

### Widget

Dockable panel whose content is a toolkit widget created by the plugin.

- The plugin library links Qt and builds the widget (typically from a Qt
  Designer `.ui` compiled with `uic`). The widget pointer crosses the ABI as
  `void*`; the SDK never dereferences it.
- A library declares `widgets: [{type, name, icon, settings}]` in its
  manifest. `create(type)` makes one independent instance per open panel. The
  panel id in layouts is `plugin_id/type`; `type` is permanent.
- Inputs are named slots (`inputs()`), possibly `multiple`; the user wires
  series to them in the host. The widget receives the value at the tracker time
  through `on_update` and reads history on demand through the host service
  `pj.widget_host.v1`. It never sees topics, datasets or the store, and creates
  no series.
- Settings are declared in the manifest per widget type; the host draws the
  form and pushes the JSON through `load_config`: every declared key (manifest
  defaults filled in by the host), with the widget's own `save_config` state
  laid over it. The manifest is the single source of defaults.
- `on_update` carries one value per wire and is also pushed after the widget is
  created, after every accepted configuration and after any wiring change, so
  the widget never shows stale state.
- Every call is on the GUI thread, except the constant `build_info` and
  `get_plugin_extension`. The library is never unloaded while the process runs;
  instance destruction is synchronous.
- Compatibility: the plugin records the Qt version it was built against
  (`build_info`); the host requires the same major.minor and refuses to create
  the widget otherwise. Qt private headers and QRhi are forbidden. Qt modules
  come from the canonical list in `widget_qt_modules.h`.

## 4. Shared Host Services

### 4.1 Write Services

DataSource, MessageParser, and Toolbox share the same datastore write
contract and backend, but bind family-specific ABI services so each plugin
only sees operations it is allowed to call. These services support:

- **Incremental writes** — `appendRecord()` / `appendBoundRecord()` with
  named or pre-resolved field values. Used by parsers and streaming
  sources where data arrives one message at a time.
- **Bulk Arrow writes** — `appendArrowStream()` hands an
  `ArrowArrayStream*` (Arrow C Data Interface) to the host, which pulls
  all batches and takes ownership on success. This is the canonical
  path for file-based sources, toolbox bulk imports, and parser-shaped
  payloads that naturally decode many rows in one `parse()` call. Parser
  batch writes are topic-scoped; the parser never names the topic.
- **Topic and field management** — `ensureTopic()`, `ensureField()`, where
  available for the family. Parsers are bound to one topic and only create
  fields within that topic.

The service names are `"pj.source_write.v1"`, `"pj.parser_write.v1"`, and
`"pj.toolbox_write.v1"`. Family-specific permissions differ: Toolbox can
create data sources, DataSource writes under its own source, and MessageParser
is bound to one topic.

### 4.2 Read Surface

Only Toolbox requires read access:

- `catalogSnapshot()` — enumerate available data sources, topics, and fields.
- `readSeries(field)` — read the full time series for a field as a
  `MaterializedSeriesView` (`rowCount()`, `timestamps()`, typed `valuesAs*()`
  pointers, `nullCount()` / `isNull(row)`). The value slot of a null sample is
  unspecified, so a caller skips rows where `isNull(row)` when `nullCount() != 0`.
- `readSeriesArrow(field)` — the same data as a host-owned `ArrowSchema` +
  `ArrowArray` pair (timestamp column + value column), which `readSeries` wraps.
  Plugins wrap the out-params in `PJ::sdk::ArrowSchemaHolder` /
  `ArrowArrayHolder` for scope-bound release.

Materialization/decompression is acceptable when reading actual sample data.

### 4.3 Runtime Services

DataSource plugins receive a runtime host providing:

- Message reporting (info/warning/error to host UI log).
- Progress bar (start/update/finish with optional cancellation).
- Advisory state notifications and plugin-initiated stop.
- Delegated parser binding and raw message dispatch.

Toolbox plugins receive a simpler runtime host:

- Message reporting.
- Data-change notification (triggers host UI refresh).
- Dataset-scoped ingest lifecycle (progress bar, cooperative stop, delegated
  parser binding) for a dataset the toolbox creates — `createDatasetIngest()` /
  `releaseDatasetIngest()`, the same underlying context DataSource delegated
  ingest uses.

### 4.4 UI Surface

All UI-capable families use the dialog protocol. UI state integrates with the
common save/load interface. The dialog protocol is part of the external SDK
for UI-capable families.

In delegated-ingest workflows, the host may embed parser configuration
controls inside a DataSource dialog via the `pj_parser_slot` placeholder.

## 5. Interaction Model

### Ownership and Lifecycle

- **DataSource dialog**: member of the source class. Host obtains a borrowed
  reference via `getDialog()` (using `PJ::borrowDialog(dialog_)` from the
  SDK). Dialog and source share state directly.
- **Parser dialog**: independent owned instance created by the host. Config
  flows via JSON — dialog and parser share a JSON schema contract but are
  otherwise decoupled.
- **Toolbox dialog**: member of the toolbox class, same borrowed pattern as
  DataSource.

### Threading

Each slot's thread rule is its tag; the per-family tables on
`PJ_data_source_vtable_t` and `PJ_toolbox_vtable_t` are normative. A DataSource
`start` may run on a worker and `stop` on the polling thread; per-topic
`set_active_topics` runs concurrently with `poll`. Plugins needing internal
threads must synchronize and call host methods only where those tables allow.

## 6. Capability System

### DataSource Capabilities

| Flag | Purpose |
|---|---|
| `kCapabilityFiniteImport` | One-shot file import |
| `kCapabilityContinuousStream` | Long-lived streaming |
| `kCapabilityDirectIngest` | Plugin writes decoded data via write host |
| `kCapabilityDelegatedIngest` | Plugin pushes raw bytes for host-side parsing |
| `kCapabilitySupportsPause` | pause()/resume() are implemented |
| `kCapabilityHasDialog` | Plugin provides a configuration dialog |
| `kCapabilityPerTopicPause` | Host may pause/resume individual topics on a live connection (advertise via `notifyAvailableTopics` + `pj.topic_subscription.v1`) — see [ARCHITECTURE.md](ARCHITECTURE.md#per-topic-pause-demand-driven-subscription) and [data-source-guide.md](data-source-guide.md) |

### Toolbox Capabilities

| Flag | Purpose |
|---|---|
| `kToolboxCapabilityHasDialog` | Plugin provides a persistent UI panel |
| `kToolboxCapabilityNonModalDialog` | The dialog is shown as a non-modal panel rather than a modal pop-up |

Read, write, create-source, and destructive-update capabilities are intrinsic
to all Toolbox plugins.

## 7. Configuration Contract

- All families support `saveConfig()` / `loadConfig()` with JSON.
- The plugin decides what state it persists.
- The host restores state deterministically from `loadConfig()` alone —
  hidden ambient sources like `QSettings` are not required.
- The host stores the JSON string returned by `saveConfig()` and passes it back to
  `loadConfig()`. When the source dialog embeds a parser dialog, the host adds a
  `_parser_config` key to that JSON, which `PJ::sdk::parserConfigOverride()`
  (`pj_plugins/sdk/streaming_source.hpp`) reads.

## 8. External SDK Consumption

- The SDK is consumable from an independent repository via Conan (primary)
  or git submodule (secondary).
- A plugin author builds against `pj_base` without depending on
  `pj_datastore`.
- The SDK is componentized: headless families don't require the dialog SDK
  or the widget SDK.

## 9. Permission Summary

| Family | Read | Write | Create source | Destructive | UI |
|---|---|---|---|---|---|
| DataSource | No | Yes | No (bound to one) | No | Optional |
| MessageParser | No | Yes | No | No | Optional |
| Dialog | No | No | No | No | Yes |
| Toolbox | Yes | Yes | Yes | Yes | Yes |
| Widget | No (values pushed by the host; history on demand) | No | No | No | Yes (its own widget) |

## 10. Non-Goals / Deferred

- Extension installation automation and host-specific catalog policy.
- Hot-reload or cross-process plugins.
- Exact ABI versioning scheme beyond protocol_version + struct_size.
- `StatePublisher` or native `Transform` plugin ABI.
- Backpressure and overflow policy.
- Detailed data model semantics (belong in `pj_datastore` docs).
