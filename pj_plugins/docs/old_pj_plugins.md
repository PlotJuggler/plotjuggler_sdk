# Legacy Source Plugin Review

## Purpose

This document reviews the legacy PlotJuggler source-side plugins from the point of
view of the new `DataSource` family.

Its goal is not to preserve legacy APIs. Its goal is to capture the actual
behaviors that users rely on so the new `DataSource` runtime can support the same
outcomes.

The review covers only legacy plugins that map naturally to the new source-side
scope:

- `DataLoadCSV`
- `DataLoadMCAP`
- `DataLoadParquet`
- `DataLoadULog`
- `DataLoadZcm`
- `DataStreamSample`
- `DataStreamUDP`
- `DataStreamWebsocket`
- `DataStreamMQTT`
- `DataStreamZMQ`
- `DataStreamZcm`
- `DataStreamPlotJugglerBridge`
- `DataStreamFoxgloveBridge`

## Capability Matrix

| Plugin | Mode | Parse path | Topic model | Runtime discovery | Dialog | Parser UI in same dialog | Saved config | Progress / cancel | Runtime controls |
|---|---|---|---|---|---|---|---|---|---|
| `DataLoadCSV` | Finite | Direct | Single file, many columns -> many fields | No | Yes | No | Yes | Yes | No |
| `DataLoadParquet` | Finite | Direct | Single file, many columns -> many fields | No | Yes | No | Yes | Yes | No |
| `DataLoadULog` | Finite | Direct | Many groups / fields from file | No | No | No | Trivial | No | No |
| `DataLoadZcm` | Finite | Direct | Selected channels -> topics / fields | No | Yes | No | Yes | Implicit | No |
| `DataLoadMCAP` | Finite | Delegated | Many channels / topics | No | Yes | Yes | Yes | Yes | No |
| `DataStreamSample` | Continuous | Direct | Fixed synthetic topics / fields | No | No | No | Trivial | No | Start / stop only |
| `DataStreamUDP` | Continuous | Delegated | Single logical stream | No | Yes | Yes | Via settings | No | Start / stop |
| `DataStreamWebsocket` | Continuous | Delegated | Single logical stream | No | Yes | Yes | Via settings | No | Start / stop |
| `DataStreamMQTT` | Continuous | Delegated | Many topics, parser per MQTT topic | Topic list refresh before start | Yes | Yes | Via settings | No | Start / stop, notifications |
| `DataStreamZMQ` | Continuous | Delegated | Single stream or many filtered topics | New parser per topic at runtime | Yes | Yes | Via settings | No | Start / stop |
| `DataStreamZcm` | Continuous | Direct | Many channels / fields | No | Yes | No | Yes | No | Start / stop |
| `DataStreamPlotJugglerBridge` | Continuous | Delegated | Remote topic discovery, parser per topic | Yes | Yes | Yes | Yes | No | Start / stop / pause / resume / reopen settings |
| `DataStreamFoxgloveBridge` | Continuous | Delegated | Remote channel discovery, parser per subscription | Yes | Yes | Yes | Yes | No | Start / stop / pause / resume |

## Per-Plugin Summary

### `DataLoadCSV`

- Direct file import with a substantial configuration dialog.
- User selects delimiter, time column or synthetic index, custom datetime parsing,
  and combined-column rules.
- Import shows progress and supports cancellation.
- Import warns about duplicate names and non-monotonic timestamps.
- Output is direct decoded data, including both numeric and string series.

What the new system must provide:

- Direct write path without any parser dependency.
- Integrated dialog protocol with persisted state.
- Host-side progress and cancel support for long-running finite imports.
- Logical string and numeric field writes.

Assessment:

- Covered by the new requirements.
- Best first direct-import migration candidate.

### `DataLoadParquet`

- Direct file import backed by Arrow / Parquet APIs.
- User selects timestamp column or row index and optional datetime parsing.
- Import is batch-oriented and naturally aligned with Arrow IPC.
- State is saved and restored.

What the new system must provide:

- Direct write path with efficient Arrow IPC ingest.
- Integrated dialog protocol with persisted state.
- Progress reporting for large files.

Assessment:

- Covered by the new requirements.
- Best proof that the new bulk write path is useful for real plugins.

### `DataLoadULog`

- Direct file import that expands one log into multiple groups, fields, and a
  synthetic parameter area.
- After import it opens an auxiliary parameters window.
- It has almost no meaningful saved configuration.

What the new system must provide:

- Direct multi-topic writes.
- Ability to create fields lazily while parsing.

Assessment:

- The ingestion side is covered.
- The post-load auxiliary window is not a clean fit for the new `DataSource`
  scope and should not be treated as a v1 requirement unless promoted into a
  separate host UI or Toolbox workflow.

### `DataLoadZcm`

- Direct file import of a ZCM log.
- Requires user configuration of type libraries plus selected channels.
- Produces both numeric and string fields.
- Saves selected channels and library path.

What the new system must provide:

- Direct multi-topic writes.
- Integrated dialog protocol for source-specific configuration.
- Persisted configuration independent of ambient `QSettings`.

Assessment:

- Covered by the new requirements.
- Good direct-import case for dynamic field creation and mixed primitive types.

### `DataLoadMCAP`

- Finite container import that delegates parsing per channel.
- Reads file summary first, discovers available topics, lets the user select a
  subset, and chooses parsers by encoding and schema.
- Creates one parser per selected channel.
- Applies parser settings such as embedded timestamp and large-array policy.
- Shows progress during import and supports cancellation.

What the new system must provide:

- Delegated parsing as a first-class DataSource behavior.
- Host-owned parser selection and parser configuration embedded in the same
  dialog as source-specific controls.
- Parser binding per `(data_source, topic)`.
- Progress and cancellation for finite delegated imports.

Assessment:

- Covered only if the DataSource runtime provides delegated parser binding and
  integrated host-owned parser UI.
- Best first delegated-import migration candidate.

### `DataStreamSample`

- Direct continuous source that generates synthetic numeric and string data.
- No meaningful setup dialog.
- Produces notifications only to exercise the old notification UI.

What the new system must provide:

- Direct continuous write path.
- Start / stop lifecycle.
- Optional status or notification reporting.

Assessment:

- Covered by the new requirements.
- Best first continuous-source migration candidate.

### `DataStreamUDP`

- Continuous delegated source with a simple network dialog.
- User selects address, port, parser encoding, and parser-specific options in the
  same dialog.
- Uses one parser instance for the stream.
- Stops itself on parsing failure and asks the host to close the source.

What the new system must provide:

- Integrated source + parser dialog.
- Delegated runtime path from raw payload to host-managed parser instance.
- Start / stop lifecycle and source-initiated close or failure reporting.

Assessment:

- Covered if the runtime defines source lifecycle and delegated parser binding.

### `DataStreamWebsocket`

- Continuous delegated source similar to UDP, but server-oriented and handling
  multiple client connections.
- Same parser-selection pattern as UDP.
- Stops on parser failure or connection-level problems.

What the new system must provide:

- Same delegated-source runtime features as UDP.
- Runtime status / error reporting.

Assessment:

- Covered if the UDP requirements above are implemented.

### `DataStreamMQTT`

- Continuous delegated source with broker connection dialog and a dynamic topic
  list before start.
- Parser is selected once, but parser instances are created lazily per MQTT topic.
- Parsing failures are tracked and surfaced as notifications.

What the new system must provide:

- Integrated source + parser dialog.
- Runtime topic discovery before start.
- Delegated parser binding per topic.
- Status / notification reporting that replaces the old notification button logic.

Assessment:

- Covered if the runtime supports per-topic parser bindings and a message/status
  surface in addition to raw data ingest.

### `DataStreamZMQ`

- Continuous delegated source with connect/bind mode, address, port, and topic
  filters.
- Can operate with or without explicit topic frames.
- Creates parsers for configured topics and can create additional ones at runtime.
- Accepts payload timestamps from the transport when present.

What the new system must provide:

- Integrated source + parser dialog.
- Delegated per-topic parser creation during execution.
- Lazy topic creation at runtime.
- Ability for the source to pass explicit transport timestamps.

Assessment:

- Covered if lazy topic creation and runtime parser binding are implemented.

### `DataStreamZcm`

- Continuous direct source with its own type database and subscribe filter.
- Decodes payloads directly and writes numeric and string fields.
- Saves library path, subscribe regex, and transport URL.

What the new system must provide:

- Direct continuous write path.
- Integrated dialog protocol and save/load.
- Multi-topic direct writes with dynamic fields.

Assessment:

- Covered by the new requirements.

### `DataStreamPlotJugglerBridge`

- Continuous delegated source that speaks a remote bridge protocol.
- Connects first, discovers topics dynamically, lets the user choose a subset,
  then creates one parser per selected topic.
- Supports pause / resume while running.
- Keeps additional state such as heartbeats and connection statistics.
- Reuses parser settings such as embedded timestamp and large-array policy.

What the new system must provide:

- Dialog protocol support for asynchronous discovery workflows driven by `on_tick`.
- Integrated host-owned parser selection / configuration in the same dialog.
- Delegated parser binding per discovered topic.
- Runtime pause / resume support.
- Runtime status / error reporting separate from the initial dialog.

Assessment:

- Covered only if the runtime includes a real source control plane, not just the
  new data write API.

### `DataStreamFoxgloveBridge`

- Continuous delegated source similar to the PlotJuggler bridge, but channel-based
  and specialized for ROS 2 CDR over Foxglove.
- Discovers channels at runtime, filters them for parser compatibility, creates
  parsers per subscription, and coalesces data notifications.
- Supports pause / resume while running.
- Surfaces warnings and connection errors to the user.

What the new system must provide:

- Same control-plane features as the PlotJuggler bridge.
- Delegated parser creation keyed by discovered channel metadata.
- Runtime status / error reporting.
- Optional coalescing of host refresh notifications as an implementation detail.

Assessment:

- Covered only if the DataSource runtime includes delegated parser binding,
  runtime state transitions, and status reporting.

## Dialog Ownership Patterns

The legacy plugins use three distinct patterns for dialog–source interaction.
Understanding these patterns motivated the new DataSource-owned dialog design.

### Ephemeral (UDP, WebSocket)

The dialog is created locally inside `start()`, shown with `exec()`, and
destroyed after it returns. State flows from the dialog to the source via
local variables or direct member reads between `exec()` and the dialog's
deletion.

```
start() {
  MyDialog d;
  d.loadSettings(QSettings);
  if (d.exec() != Accepted) return false;
  host_ = d.host();           // read before dialog dies
  port_ = d.port();
  d.saveSettings(QSettings);  // persist for next time
  // ... open socket with host_:port_ ...
}
```

Traits:
- Dialog lifetime is a subset of `start()`.
- No shared state after `exec()` returns.
- State persisted via `QSettings`, not the DataSource config system.

### Persistent member (MQTT, CSV, PJ Bridge)

The dialog is a class member of the source, reused across `start()` calls.
The source reads dialog state directly via public accessors or friend access.

```
class MySource {
  MyDialog dialog_;           // member, created once

  start() {
    dialog_.loadSettings(QSettings);
    if (dialog_.exec() != Accepted) return false;
    connect(dialog_.host(), dialog_.port());  // read state directly
  }
};
```

Traits:
- Dialog survives across start/stop cycles.
- Source reads dialog state without serialization.
- Shared state is implicit (same object lifetime as the source).
- State persisted via `QSettings`.

### Shared business object (MQTT)

The dialog holds a `shared_ptr` to a business-logic object (e.g., an MQTT
client) and manipulates it directly — calling `connect()`, `subscribe()`, etc.
from event handlers.

```
class MyDialog {
  shared_ptr<MqttClient> client_;

  onConnectClicked() {
    client_->connect(host_, port_);    // direct business logic
  }
  onTopicSelected(topics) {
    client_->subscribe(topics);
  }
};

class MySource {
  shared_ptr<MqttClient> client_;
  MyDialog dialog_{client_};           // dialog shares the client
};
```

Traits:
- Dialog has write access to business logic, not just UI state.
- Tight coupling between dialog and source internals.
- Hardest pattern to test in isolation.

### All legacy plugins use QSettings for persistence

Every legacy plugin persists its dialog state via `QSettings` — not through
the DataSource config system. The new design replaces this with
`saveConfig()` / `loadConfig()` on the dialog, with the source delegating
config persistence to its owned dialog member. This removes the ambient
`QSettings` dependency and makes config fully serializable and testable.

### New design choice

The new DataSource-owned dialog design follows the **persistent member**
pattern: the dialog is a member of the source, they share state directly, and
the dialog exposes read-only accessors for the source to consume. This avoids
the tight coupling of the shared business object pattern while preserving the
direct state access that makes the persistent member pattern efficient.

See `pj_plugins/examples/mock_source_with_dialog.cpp` for the reference
implementation.

## Compatibility Conclusions

### What the new requirements already cover well

- One unified `DataSource` family is enough for all reviewed legacy loaders and
  streamers.
- The new shared write contract is sufficient for all reviewed ingestion styles:
  - direct decoded writes
  - delegated parser writes
  - multi-topic sources
  - lazy field creation
  - lazy topic creation
- The dialog protocol is expressive enough for both finite-import dialogs and
  asynchronous discovery dialogs.

### What the new DataSource runtime still must define explicitly

- A real source control plane in addition to the new data plane:
  - start
  - stop
  - running state
  - optional pause / resume
  - failure / closed reporting
- Integrated source dialogs with host-owned parser selection and parser
  configuration for delegated sources.
- A delegated-ingestion host surface where the DataSource can bind a parser to a
  topic and then feed raw payloads to that binding.
- Runtime status, warning, error, and progress reporting without `QMessageBox`,
  Qt signals, or shared-container mutation.
- Reconfiguration from saved state without depending on `QSettings`.

### Behaviors that should not drive v1 DataSource design

- Direct access to `PlotDataMapRef` or direct clearing of shared buffers.
- Parser-owned widgets embedded in source dialogs.
- The ULog post-load auxiliary dialog as a required DataSource feature.
- Legacy notification-button UI semantics as a required one-to-one behavior.

## Migration Order Implication

The legacy inventory suggests the following order:

1. `DataLoadCSV` or `DataLoadParquet` for the first direct finite source.
2. `DataLoadMCAP` for the first delegated finite source.
3. `DataStreamSample` for the first continuous direct source.
4. `DataStreamUDP` or `DataStreamWebsocket` for the first simple delegated
   continuous source.
5. `DataStreamMQTT` or `DataStreamZMQ` for delegated multi-topic runtime behavior.
6. `DataStreamPlotJugglerBridge` and `DataStreamFoxgloveBridge` after the generic
   delegated runtime is stable.
7. The ZCM pair after the generic direct and delegated paths are proven.
