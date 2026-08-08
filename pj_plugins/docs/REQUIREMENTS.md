# Plugin System Requirements

## 1. Purpose

PlotJuggler loads functionality at runtime from shared libraries (`.so` /
`.dylib` / `.dll`). The plugin system defines a stable, portable boundary
between the host application and these libraries, plus convenience layers that
make plugin development practical.

Four plugin families exist:

- **DataSource** — acquires data from files, network streams, or hardware.
  Write-only; one instance per application data source.
- **MessageParser** — decodes raw byte payloads (JSON, Protobuf, ROS, etc.)
  into named fields. Headless; one instance per (data source, topic) pair.
- **Dialog** — drives a Qt Designer `.ui` form through a stable C ABI.
  The plugin describes UI as XML, exchanges state as JSON, and handles events
  via typed callbacks — without linking Qt itself.
- **Toolbox** — stateful interactive tools with full read+write access to host
  data. Long-lived, UI-driven; may create new data sources or transform data.

## 2. Design Principles

1. **Stable C ABI boundary.** Binary compatibility does not depend on compiler,
   C++ standard library, or Qt ABI. No C++ classes, STL containers, Qt objects,
   or exceptions cross the boundary.

2. **C++ SDK convenience layer.** Plugin authors subclass a base class, override
   virtuals, and export with a macro. The SDK generates C ABI trampolines with
   full exception safety.

3. **No Qt dependency in the plugin SDK.** Plugins link only `pj_base` (and
   optionally `pj_dialog_sdk` for dialog UI). Qt is a host-side concern.

4. **Version negotiation.** Each protocol carries `protocol_version` and
   `struct_size` for forward/backward compatibility.

5. **Instance isolation.** Multiple instances of the same plugin type run
   concurrently with instance-local state.

### 2.1 Compatibility vocabulary

Plugin compatibility uses three separate values. They are not aliases and a
manifest must not substitute one for another:

| Value | Meaning | Enforcement |
|---|---|---|
| `min_sdk_required` | Minimum PlotJuggler SDK contract whose features the plugin actually requires | Hard host-side load gate before plugin instantiation |
| `min_plotjuggler_version` | Minimum PlotJuggler application release expected by the plugin | Existing application-level advisory policy |
| ABI/dialog protocol version | Binary C ABI and dialog C/wire contract compatibility | Existing strict vtable, `struct_size`, and protocol validation |

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
Qt-free `PJ::SemVer` parser/comparator in `pj_base/sdk/semver.hpp`.

## 3. Plugin Families and Their Roles

### DataSource

Unified replacement for legacy `DataLoader` and `DataStreamer`. Handles both
finite imports (CSV, Parquet, MCAP) and continuous streams (MQTT, ZMQ, UDP).

- Write-only — no read access to existing host data.
- May decode data directly (direct ingest) or hand raw payloads to a
  MessageParser (delegated ingest).
- Topics may appear lazily at runtime.
- Lifecycle: idle → configuring → starting → running → stopping → stopped.
  Optional pause/resume.

### MessageParser

Independent decoder, typically driven by a DataSource via the host.

- Write-only, topic-scoped — fields are namespaced under the assigned topic.
- Stateless across messages (but may cache schema-derived lookup tables).
- May export a dialog vtable from the same `.so` for configuration UI
  (e.g. schema selection, include paths). The dialog is an independent owned
  instance — the host creates it and bridges config to parser instances via
  JSON.
- Lifecycle: create → bind → parse* → destroy.
- A parser that registers `SchemaHandler` entries exposes
  `pj.parser_functional.v1` after schema binding. Functional scalar and object
  results cross only through synchronous caller-owned C sinks; the host must
  never cast the opaque context to `MessageParserPluginBase` for such a parser.
- Scalar views are callback-duration-only. Canonical objects cross as a stable
  numeric builtin tag plus canonical wire bytes and are decoded into host-owned
  storage before the extension call returns. No plugin allocator, destructor,
  RTTI object, `std::any`, `std::string`, or `BufferAnchor` crosses back.
- The object route consumes exactly one `PJ_payload_t` anchor reference and
  releases it exactly once on success and every failure path. This permits
  zero-copy input when the host already owns an anchored payload; output still
  pays the explicit canonical serialization boundary.
- Extension presence is truthful, not merely SDK-version-based: a 0.21-built
  parser that still overrides only legacy `parse()` must not advertise the
  functional extension. Hosts query after `bind_schema` and do not cache an
  earlier absence.
- During the 0.21 migration only, a host may use an isolated deprecated
  direct-C++ bridge for a pre-0.21 parser whose extension is absent. The bridge
  is removed, together with its class-layout constraint, in SDK 1.0.

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
- `readSeriesArrow(field)` — read the full time series for a field as a
  host-owned `ArrowSchema` + `ArrowArray` pair (timestamp column +
  value column). Plugins wrap the out-params in
  `PJ::sdk::ArrowSchemaHolder` / `ArrowArrayHolder` for scope-bound
  release.

Materialization/decompression is acceptable when reading actual sample data.

### 4.3 Runtime Services

DataSource plugins receive a runtime host providing:

- Message reporting (info/warning/error to host UI log).
- Progress bar (start/update/finish with optional cancellation).
- State notifications and plugin-initiated stop.
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

All plugin callbacks are called on the host's thread. The host guarantees
single-threaded access per instance. Plugins needing internal threads must
synchronize and call host methods only from callback context.

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
| `kCapabilityPerTopicPause` | Host may pause/resume individual topics on a live connection (advertise via `notifyAvailableTopics` + `pj.topic_subscription.v1`) — see `docs/ARCHITECTURE.md` → "Per-topic pause" and `docs/data-source-guide.md` |

### Toolbox Capabilities

| Flag | Purpose |
|---|---|
| `kToolboxCapabilityHasDialog` | Plugin provides a persistent UI panel |

Read, write, create-source, and destructive-update capabilities are intrinsic
to all Toolbox plugins.

## 7. Configuration Contract

- All families support `saveConfig()` / `loadConfig()` with JSON.
- The plugin decides what state it persists.
- The host restores state deterministically from `loadConfig()` alone —
  hidden ambient sources like `QSettings` are not required.
- For delegated sources, the host wraps source and parser config in a
  versioned `ConfigEnvelope`:

```json
{"version": 1, "source_config": "...", "parser_binding": "..."}
```

The source never sees `parser_binding`; the host manages it.

## 8. External SDK Consumption

- The SDK is consumable from an independent repository via Conan (primary)
  or git submodule (secondary).
- A plugin author builds against `pj_base` without depending on
  `pj_datastore`.
- The SDK is componentized: headless families don't require the dialog SDK.

## 9. Permission Summary

| Family | Read | Write | Create source | Destructive | UI |
|---|---|---|---|---|---|
| DataSource | No | Yes | No (bound to one) | No | Optional |
| MessageParser | No | Yes | No | No | Optional |
| Dialog | No | No | No | No | Yes |
| Toolbox | Yes | Yes | Yes | Yes | Yes |

## 10. Non-Goals / Deferred

- Extension installation automation and host-specific catalog policy.
- Hot-reload or cross-process plugins.
- Exact ABI versioning scheme beyond protocol_version + struct_size.
- `StatePublisher` or native `Transform` plugin ABI.
- Backpressure and overflow policy.
- Detailed data model semantics (belong in `pj_datastore` docs).
