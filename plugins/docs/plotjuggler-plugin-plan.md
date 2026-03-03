# PlotJuggler Portable Plugin System — Architecture Plan (v3)

---

# Part I — Goals and Direction

## 1.1 Core Goals

- **G1 — Clean data boundary** between plugins and host. Plugins never touch `PlotDataMapRef`. They produce data in Arrow; the host converts.
- **G2 — Decoupled plugin families** (DataSource vs Parser vs Transform). A new parser automatically works with all existing data sources.
- **G3 — Unified source model** (replace DataLoader/DataStreamer split). File loading and live streaming share one interface.
- **G4 — Portable compute plugins** via C ABI + Arrow. Parsers and transforms compile independently of Qt, compiler, or C++ standard library — including as WASM modules.
- **G5 — Accept Qt for DataSources.** DataSource plugins legitimately need interactive dialogs. Pin Qt 6.8 LTS for ABI stability. The trade-off is explicit: DataSources are Qt-coupled; Parsers and Transforms are not.
- **G6 — Engine-first ingestion.** The canonical storage target is the `data/` engine (`DataEngine`, chunked storage, typed writer/reader). Legacy structures are transitional compatibility views.

## 1.2 Non-Goals (Early Phases)

- Mandatory `.ui + JSON` dialog protocol for all DataSources.
- Full WASM rollout before native C ABI path is stable.
- All plugin families migrated in one technical phase.

## 1.3 What Changes, What Stays

| Plugin type | Qt dependency | Interface | Distributable without recompiling PJ? |
|---|---|---|---|
| **Parser** | None | C ABI + Arrow | Yes (native `.so`, `.wasm`, any language) |
| **Transform** | None | C ABI + Arrow | Yes |
| **DataSource** | Qt 6.8 LTS (minimal) | C++ virtual methods + Arrow + dialog protocol | Only within same Qt 6.8.x series |
| **StatePublisher** | Qt 6.8 LTS | (unchanged for now) | Same as above |
| **ToolboxPlugin** | Qt 6.8 LTS | (unchanged for now) | Same as above |

---

# Part II — Current Architecture (Problems)

## 2.1 Plugin Types

From the `plotjuggler_base` source, there are 6 plugin categories:

| Plugin Type | Base Class | Purpose |
|---|---|---|
| **DataLoader** | `PJ::DataLoader` | Load data from files (CSV, ULog, MCAP, Parquet) |
| **DataStreamer** | `PJ::DataStreamer` | Live data streaming (MQTT, ZMQ, UDP, WebSocket) |
| **MessageParser** | `PJ::ParserFactoryPlugin` | Decode message formats (JSON, Protobuf, ROS, DataTamer) |
| **StatePublisher** | `PJ::StatePublisher` | Publish data at time tracker position |
| **TransformFunction** | `PJ::TransformFunction` | Data transforms (derivative, moving average) |
| **ToolboxPlugin** | `PJ::ToolboxPlugin` | UI-heavy tools (FFT, Lua editor, Quaternion) |

All inherit from `PJ::PlotJugglerPlugin -> QObject`.

## 2.2 Core Data Structure

The central container `PlotDataMapRef` is a set of unordered maps (`numeric`, `strings`, `scatter_xy`, `user_defined`, `groups`) where each `PlotData` is a `std::deque<Point>` with `Point = { double x, double y }`.

## 2.3 Summary of Challenges

| Challenge | Severity | Solved by |
|---|---|---|
| ABI fragility (Qt, C++, STL) | High | C ABI for Parsers/Transforms; Qt 6.8 LTS for DataSources |
| Data sources coupled to parsers | High | Host dispatches parsers; delegated mode |
| Artificial DataLoader/DataStreamer split | Medium | Unified DataSource |
| Shared mutable PlotDataMapRef | High | Arrow at boundary; host owns conversion |
| Qt signals for async events | Medium | Polling model with `pollEvents()` |
| `optionsWidget()` and dialog coupling | Medium | Dialog protocol (see Part VIII) |
| Parser widget embedding in streamer dialogs | Medium | Host injects parser selector for delegated sources |
| Mid-operation error dialogs | Low | Error policy config + post-load summary |
| Layout file save/restore | Medium | JSON config state replaces `xmlSaveState()` |

---

# Part III — Data Engine Integration

This section binds plugin architecture to the current `data/` engine state.

## 3.1 Stable Engine Capabilities

- Chunked topic storage with shared timestamp column.
- Typed write/read flow (`DataWriter`, `DataReader`).
- Time-domain IDs and display offsets.
- Range query and latest-at query APIs.
- Additive schema evolution validation in `TypeRegistry`.

## 3.2 Capabilities Implemented Since Initial Plan

The following were listed as constraints in prior versions but have since been fully implemented:

- **Derived DAG engine** — `DerivedEngine` supports SISO and MIMO transforms with topological scheduling, incremental and batch recompute. See `data/engine/include/PJ/engine/derived_engine.hpp`.
- **Variable-length array expansion** — `DataWriter::expand_array()` dynamically adds element columns. See `data/engine/include/PJ/engine/writer.hpp`.
- **Dynamic column addition** — `DataWriter::ensure_column()` adds columns to topics on the fly, supporting both typed and schemaless topics.

## 3.3 Remaining Constraints

- Writer API is row-builder based (`begin_row/set_*/finish_row`) plus bulk `append_columns()`. No `MessageView` append API.
- Commit path is synchronous (`flush_all()` -> `commit_chunks()`); staged multi-thread queue model is deferred.
- Timestamps are `int64_t` nanoseconds since epoch (see `PJ::Timestamp` in `data/base/include/PJ/base/types.hpp`).

## 3.4 Integration Implications

- The plugin pipeline targets row-builder ingestion first, with `append_columns()` as the columnar fast path.
- Transform scheduling is handled by `DerivedEngine` — plugin transforms register as SISO/MIMO nodes.
- Time ordering semantics must be explicit at the ingestion boundary.

---

# Part IV — Plugin Architecture

## 4.1 Plugin Families

1. **Parser plugin**: C ABI + Arrow output. Pure decoder: raw bytes in, Arrow RecordBatch out.
2. **Transform plugin**: C ABI + Arrow I/O. Pure computation: Arrow in, Arrow out.
3. **DataSource plugin**: Qt/C++ plugin with `Direct/Delegated/Both` modes. Replaces both `DataLoader` and `DataStreamer`.
4. **StatePublisher/Toolbox**: Out of early migration scope.

## 4.2 Data Flow (Target)

```
DataSource (Direct or Delegated)
        |
        v
Host dispatch (parser selection, lifecycle, error policy)
        |
        v
Arrow batch normalization (wide/tall/scatter + metadata)
        |
        v
Arrow -> DataWriter adapter (row appends or append_columns)
        |
        v
TopicChunkBuilder -> seal -> commit_chunks -> DataReader queries
        |
        v
DerivedEngine scheduling (SISO/MIMO transforms)
```

## 4.3 Transitional Compatibility

- Host may maintain a compatibility adapter for legacy consumers.
- Source of truth for newly migrated paths is the `data/` engine.
- Compatibility outputs are generated from engine reads, not parallel writes.

## 4.4 DataSource Mode Summary

| DataSource | Mode | Rationale |
|---|---|---|
| CSV reader | Direct | CSV *is* the data format |
| Parquet reader | Direct | Already columnar, wraps as Arrow |
| ULog reader | Direct | Self-contained format |
| Dummy streamer | Direct | Generates synthetic data |
| MCAP reader | Delegated | Container — messages can be ROS1, ROS2, Protobuf, etc. |
| MQTT streamer | Delegated | Transport — payloads can be any encoding |
| ZMQ / UDP streamer | Delegated | Raw bytes, encoding depends on publisher |
| WebSocket streamer | Delegated | Same as above |
| ROS2 subscriber | Delegated | Receives serialized ROS2 messages |

---

# Part V — C ABI Specification (Handle-Based, Versioned)

## 5.1 Versioning Model

Each ABI family has independent versioning:

- `pj_parser`: `abi_major`, `abi_minor`
- `pj_transform`: `abi_major`, `abi_minor`

Compatibility rules:

1. Host requires exact `abi_major` match.
2. Host accepts plugin `abi_minor <= host_abi_minor`.
3. Additive optional functionality is feature-probed (symbol lookup).

## 5.2 Parser ABI (Required Symbols)

```c
typedef struct pj_parser_instance* pj_parser_handle;

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    const char* plugin_name;
    const char* plugin_version;
    const char* encoding;          // "ros2", "json", "protobuf", etc.
} pj_parser_info;

int32_t pj_parser_get_info(pj_parser_info* out_info);
int32_t pj_parser_create(const char* init_json, uint32_t init_len,
                         pj_parser_handle* out);
void    pj_parser_destroy(pj_parser_handle h);
void    pj_parser_free(void* ptr);

int32_t pj_parser_configure(pj_parser_handle h,
                            const char* topic_name,
                            const char* type_name,
                            const uint8_t* schema_data,
                            uint32_t schema_len,
                            const char* config_json,
                            uint32_t config_len);

int32_t pj_parser_parse_native(pj_parser_handle h,
                               const uint8_t* msg_data,
                               uint32_t msg_len,
                               double host_timestamp,
                               struct ArrowArray* out_array,
                               struct ArrowSchema* out_schema);
```

## 5.3 Parser ABI (Optional Symbols)

```c
int32_t pj_parser_parse_ipc(pj_parser_handle h,
                            const uint8_t* msg_data,
                            uint32_t msg_len,
                            double host_timestamp,
                            uint8_t** out_ipc,
                            uint32_t* out_ipc_len);

const char* pj_parser_describe_parameters(pj_parser_handle h);
```

## 5.4 Transform ABI

Same handle-based pattern:

- `pj_transform_get_info/create/destroy/free/configure/calculate/reset`
- Handle-based, no global state assumptions.

Transform-specific signatures:

```c
// Input/output schema declaration
const char* pj_transform_input_schema();   // JSON array of {name, type}
const char* pj_transform_output_schema();  // JSON array of {name, type}

// Configure with bound names (host maps actual series names)
int32_t pj_transform_configure(pj_transform_handle h,
                               const char* config_json, uint32_t config_len);

// Native path: Arrow batch in -> Arrow batch out
int32_t pj_transform_calculate_native(pj_transform_handle h,
                                      const struct ArrowArray* input_array,
                                      const struct ArrowSchema* input_schema,
                                      struct ArrowArray* out_array,
                                      struct ArrowSchema* out_schema);

void pj_transform_reset(pj_transform_handle h);
```

**SISO example** (derivative): `input_schema = [{"name":"value","type":"float64"}]`, `output_schema = [{"name":"value","type":"float64"}]`.

**MIMO example** (quaternion -> RPY): 4 input columns (qx, qy, qz, qw), 3 output columns (roll, pitch, yaw). The host maps PlotJuggler series to input/output columns.

## 5.5 Memory Ownership Rules

| What | Allocated by | Freed by | Mechanism |
|---|---|---|---|
| `ArrowArray` / `ArrowSchema` | Plugin | Host | `array.release(&array)` (Arrow C Data Interface spec) |
| `out_ipc` byte buffers | Plugin | Host | `pj_parser_free(ptr)` |
| Static name/version strings | Plugin (static) | Nobody | Valid for plugin lifetime |
| `config_json` input | Host | Host | Plugin copies if needed |

Arrow objects follow the standard C Data Interface contract: the producer embeds a `release` callback, the consumer calls it after processing. After `release()`, the struct is zeroed (`release = NULL`).

Raw byte buffers (IPC output, `describe_parameters` return) are allocated by the plugin, freed by the host via `pj_*_free()`.

---

# Part VI — Data Contract

## 6.1 Timestamp Contract (Engine-Aligned)

1. Parser output timestamp column is authoritative.
2. Host-provided `host_timestamp` is fallback input only.
3. Canonical ingestion type is **`int64_t` nanoseconds** (`PJ::Timestamp`).
4. If parser emits float timestamp, host performs one boundary conversion to `int64_t` ns before writer append.

## 6.2 Supported Arrow Shapes

### Wide timeseries (preferred for fixed schemas)

```
columns:  _timestamp (float64)  |  field_a (float64)  |  field_b (utf8)  | ...
rows:     one row per data point
```

### Tall timeseries (for dynamic/unknown schemas)

```
columns:  _series_name (utf8)  |  _timestamp (float64)  |  _value_numeric (float64, nullable)  |  _value_string (utf8, nullable)
rows:     one row per (series, timestamp, value) triple
```

### Scatter XY

- Required: `_x`, `_y`
- Optional: `_series_name`
- No synthetic timestamp required.

### Metadata sidecar

- Stored in Arrow schema metadata key: `pj:metadata`.

The host auto-detects shape by checking for `_series_name` and `_x`/`_y` columns.

## 6.3 Ordering Contract

The plugin boundary must define one of these host policies (configurable):

1. Reject out-of-order rows per topic.
2. Accept and preserve append order (document query semantics impact).
3. Buffer-sort before commit (bounded latency/memory).

The default policy for initial migration should be explicit and tested.

---

# Part VII — DataSource Strategy

## 7.1 Modes

- **Direct**: DataSource emits Arrow. Used by self-contained formats (CSV, Parquet, ULog).
- **Delegated**: DataSource emits raw envelopes; host invokes parser. Used by containers/transports (MCAP, MQTT, ZMQ).
- **Both**: Supports both paths.

## 7.2 Base Class (Pseudocode)

```cpp
class DataSource : public QObject {
    Q_OBJECT
public:
    enum Mode { Direct = 1, Delegated = 2, Both = 3 };
    virtual Mode mode() const = 0;
    virtual QStringList fileExtensions() const { return {}; }

    // Lifecycle
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() { return false; }
    virtual bool resume() { return false; }

    enum State { Idle, Running, Paused, Error };
    virtual State state() const = 0;

    // Direct mode: plugin produces decoded Arrow tables.
    struct ArrowTable { QString topic_name; ArrowArray array; ArrowSchema schema; };
    virtual std::vector<ArrowTable> pollDirect(bool& has_more) {
        has_more = false; return {};
    }

    // Delegated mode: raw messages for host to parse.
    struct RawMessage {
        QString topic_name, encoding, type_name;
        QByteArray schema, payload;
        uint64_t schema_hash;
        double timestamp;
    };
    virtual std::vector<RawMessage> pollDelegated(bool& has_more) {
        has_more = false; return {};
    }

    // Events (replacing Qt signals)
    enum EventType { ClearBuffers, RemoveGroup, Closed, Notification, Warning };
    struct Event { EventType type; QString payload; int int_value; };
    virtual std::vector<Event> pollEvents() { return {}; }

    // Config persistence
    virtual QByteArray saveConfigJson() const { return {}; }
    virtual bool loadConfigJson(const QByteArray& config_json) { return false; }
};
```

## 7.3 Lifecycle and State Machine

```
State machine:
  IDLE --start()--> RUNNING --stop()--> IDLE
                      |  ^
                pause()|  |resume()
                      v  |
                    PAUSED

Threading contract:
  All host calls happen on the GUI thread.
  Plugin may spawn background I/O threads internally.
  Plugin must synchronize internal threads with poll*() calls.
```

For file-based sources, the host tight-loops calling poll until `has_more == false`. For streaming, the host polls at ~50 Hz.

## 7.4 Native Dialogs First

- Keep native `QWidget*` dialogs in early phases.
- Host injects parser selector/config area for delegated sources.
- Host-rendered dialog protocol (Part VIII) remains optional later-phase work.

## 7.5 Config Precedence

1. Layout-restored config.
2. Global preferences.
3. Plugin defaults.

---

# Part VIII — Dialog Protocol

The dialog protocol is **fully implemented** at `plugins/dialog_protocol/`. It provides a C ABI vtable for plugins to describe UI layout (`.ui` XML) and communicate with the host through JSON data binding.

## 8.1 Implementation Files

| Path | Purpose |
|---|---|
| `plugins/dialog_protocol/include/PJ/dialog_protocol.h` | C ABI vtable (`PJ_dialog_vtable_t`) |
| `plugins/dialog_protocol/include/PJ/host/` | Host-side C++ API (`PJ::host` namespace) |
| `plugins/dialog_protocol/include/PJ/host_qt/` | Qt dialog engine (`PJ::host_qt` namespace) |
| `plugins/dialog_protocol/include/PJ/sdk/` | Plugin-side SDK helpers |
| `plugins/dialog_protocol/src/dialog_engine.cpp` | Qt dialog engine implementation |
| `plugins/dialog_protocol/src/widget_binding.cpp` | Widget binding implementation |
| `plugins/dialog_protocol/tests/` | Protocol and integration tests |
| `plugins/dialog_protocol/examples/` | Example plugin using the protocol |

## 8.2 Protocol Summary

The vtable exposes: `create/destroy`, `get_manifest/get_ui_content`, `get_widget_data`, `on_widget_event/on_tick`, `on_accepted/on_rejected`, `save_config/load_config`, and `get_last_error`. String ownership follows a "plugin-owned, valid until next call to same function on same context" convention.

See the header files under `plugins/dialog_protocol/include/PJ/` for the full API surface.

---

# Part IX — Host Architecture

## 9.1 Parser Dispatch Model

- Cache parser instances per topic (or topic + schema key).
- Reconfigure parser on schema hash changes.
- User-selected parser acts as default; explicit per-message encoding overrides.

```
Host receives RawMessage{topic, encoding, type_name, schema, schema_hash, payload, timestamp}
    |
    v
parser_cache.find(topic)
    |
    +-- Found, schema_hash matches -> use cached parser instance
    |
    +-- Found, schema_hash differs -> schema evolved: pj_parser_configure() + update hash
    |
    +-- Not found -> parser_registry.find(encoding) -> create + configure + cache
    |
    v
pj_parser_parse_native(payload, timestamp) -> Arrow batch
    |
    v
Arrow -> DataWriter adapter
```

## 9.2 Arrow-to-Engine Adapter

Adapter responsibilities:

1. Validate required columns by shape (wide/tall/scatter).
2. Normalize timestamp to `int64_t` ns.
3. Resolve/register schema and topic descriptors via `DataWriter`.
4. Write rows via `begin_row/set_*/finish_row` or bulk `append_columns()`.
5. Flush/commit chunks, notify `DerivedEngine::on_source_committed()`.

The primary ingestion path targets `DataWriter` directly. Legacy `PlotDataMapRef` is populated from engine reads via a compatibility bridge, not parallel writes.

## 9.3 Adapter Layer for C ABI Plugins

Native plugins return Arrow via C Data Interface (zero-copy pointers). WASM plugins return Arrow IPC bytes. Internally, the host uses a single wrapper that holds either representation and lazily converts, so all downstream code has one path.

```
+---------------------------------------------------------+
|  Host Application                                       |
|                                                         |
|  NativeParserAdapter       WasmParserAdapter            |
|  (wraps dlopen,            (wraps wasmtime,             |
|   ArrowArray by ptr)        Arrow IPC bytes)            |
|         |                          |                    |
|         +----------+---------------+                    |
|                    v                                    |
|          Parser dispatch table                          |
|          (encoding, type) -> adapter instance           |
|                    |                                    |
|                    v                                    |
|          Arrow -> DataWriter adapter                    |
+---------------------------------------------------------+
```

## 9.4 Error and Warning Semantics

- Plugins do not show blocking host-independent dialogs in the hot path.
- Warnings are surfaced as structured `Warning` events via `pollEvents()`.
- Host applies policy: continue/skip/abort and presents summary UX (e.g., "Loaded 15,230 rows, skipped 23 with errors").

## 9.5 Legacy Bridge

- Temporary bridge projects engine data into old containers (`PlotDataMapRef`) for unaffected UI/tooling modules.
- Bridge is explicitly transitional and removed after parity sign-off.

---

# Part X — Plugin SDK

## 10.1 SDK Contents

- `pj_plugin_api.h` — C ABI declarations.
- nanoarrow headers — Arrow C Data Interface + utilities.
- Optional C++ convenience wrappers/macros.
- Example parser/transform plugins.
- Contract test harness (symbol/lifecycle/ownership checks).

## 10.2 Directory Structure

```
plotjuggler-plugin-sdk/
+-- include/
|   +-- nanoarrow/
|   |   +-- nanoarrow.h           # Arrow C Data Interface
|   |   +-- nanoarrow_ipc.h       # IPC read/write (WASM plugins)
|   +-- pj_plugin_api.h           # C function declarations
|   +-- pj_plugin_helpers.hpp     # Optional C++ convenience wrappers
+-- examples/
|   +-- parser_json/
|   +-- transform_derivative/
|   +-- CMakeLists.txt
+-- CMakeLists.txt
```

## 10.3 C++ Convenience Layer

Plugin authors get header-only helpers hiding Arrow boilerplate:

```cpp
class MyParser : public PJ::PluginParser {
public:
    const char* name() override { return "my_json_parser"; }
    const char* encoding() override { return "json"; }

    void parse(const uint8_t* msg, uint32_t len, double timestamp,
               PJ::OutputBatch& output) override {
        output.add_numeric("temperature", timestamp, 23.5);
        output.add_string("status", timestamp, "ok");
    }
};
PJ_EXPORT_PARSER(MyParser);
```

`PJ_EXPORT_PARSER` generates all `extern "C"` functions and Arrow plumbing automatically.

## 10.4 Reference Examples

- Minimal parser (wide output).
- Dynamic parser (tall output).
- Scatter parser.
- SISO transform (derivative).
- MIMO transform (quaternion -> RPY).

---

# Part XI — Discovery, Loading, Packaging

## 11.1 Manifest

Each C ABI plugin ships `plugin.json`:

```json
{
  "name": "my_parser",
  "type": "parser",
  "abi_family": "pj_parser",
  "abi_major": 1,
  "abi_minor": 0,
  "plugin_version": "1.2.0",
  "encoding": "protobuf",
  "format": "native",
  "library": "libmy_parser.so",
  "min_host_version": "4.0.0"
}
```

## 11.2 Search Paths

1. User marketplace cache.
2. User local plugin directory.
3. System install plugin directory.
4. Environment path override.

## 11.3 Loader Policy

1. Load with `RTLD_LOCAL`.
2. Resolve required symbols first; fail closed if missing.
3. Resolve optional symbols opportunistically.
4. Isolate instance state per plugin handle.

---

# Part XII — Migration Inventory

## 12.1 Wave Grouping

### First Wave (low risk, high leverage)

- DataStreamSample (direct).
- DataLoadParquet (direct — already uses Arrow internally).
- ParserDataTamer (pure computation, ideal C ABI candidate).
- ParserIDL (same as DataTamer).
- ParserLineInflux (dynamic fields — tests tall format).

### Second Wave

- DataLoadULog (direct, simple dialog).
- UDP/WebSocket/ZMQ streamers (delegated — trivial once host handles parser selection).
- ParserROS variants (medium — schema handling for message definitions).

### Third Wave

- DataLoadMCAP (delegated — biggest beneficiary, all parser coupling removed).
- DataStreamMQTT (delegated, hardest dialog — multi-step + async discovery).
- DataLoadCSV (direct, complex dialog — interactive preview, error policy).
- ParserProtobuf (split: C ABI core + host-side schema management).
- ZCM loaders/streamers.

### Later

- Transform family migration.
- Optional dialog protocol modernization.
- WASM runtime and marketplace hardening.

## 12.2 Per-Plugin Assessment

### DataSource Plugins

| Plugin | Target Mode | Difficulty | Key Notes |
|---|---|---|---|
| **DataLoadCSV** | Direct | Medium | Interactive delimiter/preview dialog. Mid-load QMessageBox -> Warning events. |
| **DataLoadMCAP** | Delegated | Medium | All parser coupling code removed. Just returns raw messages. |
| **DataLoadParquet** | Direct | Easy | Already uses Arrow internally. Just stop converting to PlotDataMapRef. |
| **DataLoadULog** | Direct | Easy | Simple format, simple dialog. |
| **DataStreamMQTT** | Delegated | Hard | Multi-step dialog. Async topic discovery. TLS cert pickers. |
| **DataStreamSample** | Direct | Easy | No dialog. Good first PoC. |
| **DataStreamUDP** | Delegated | Medium | Host injects parser selector. Simple connection form. |
| **DataStreamWebsocket** | Delegated | Medium | Same pattern as UDP. |
| **DataStreamZMQ** | Delegated | Medium | Per-topic parser caching handled by host. |

### Parser Plugins

| Plugin | Difficulty | Key Notes |
|---|---|---|
| **ParserDataTamer** | Easy | Pure computation. No UI, no settings. |
| **ParserIDL** | Easy | Same as DataTamer. |
| **ParserLineInflux** | Easy | Dynamic field names -> tests tall format output. |
| **ParserROS** (ROS1+2) | Medium | Separate C ABI parsers. Schema handling. Recursive field traversal. |
| **ParserProtobuf** | Hard | Split: C ABI core (deserialize FileDescriptorSet) + host-side schema management. |

### Unchanged (for now)

| Plugin | Reason |
|---|---|
| **StatePublisherCSV/ZMQ** | Different pattern (reads data). Future phase. |
| **ToolboxFFT** | Deeply UI-integrated. Reads AND writes PlotDataMapRef. |
| **ToolboxLuaEditor** | Scripting environment. Most complex plugin. |
| **ToolboxQuaternion** | ToolboxPlugin stays unchanged. The QuaternionToRPY TransformFunction can become a C ABI MIMO Transform. |

---

# Part XIII — Phase Plan with Gates

## Phase 0 — Foundation Hardening (Immediate)

**Scope:**

1. Resolve toolchain/build compatibility for `data/` integration path.
2. Tighten writer lifecycle and bounds validation behavior.
3. Lock and test timestamp ordering policy.
4. Add explicit ingestion-path diagnostics (not silent no-ops).

**Exit gates:**

1. CI build matrix green on supported toolchains.
2. Negative-path tests for invalid column/topic/row lifecycle.
3. Documented and tested ordering policy.

## Phase 1 — Host Dispatch Decoupling

**Scope:**

1. Move parser selection and instantiation to host.
2. Keep current plugin interfaces initially.
3. Remove duplicated source-side parser UI logic.

**Exit gates:**

1. Functional parity for migrated built-ins.
2. No parser-selection UX regression.

## Phase 2 — Parser C ABI + Arrow-to-Engine Ingestion

**Scope:**

1. Introduce parser C ABI v1 (handle-based, versioned).
2. Implement discovery + loader enforcement.
3. Ship Arrow-to-engine adapter (`Arrow batch -> DataWriter`).
4. Port easiest parsers to validate full path.

**Exit gates:**

1. ABI contract tests pass.
2. Engine parity tests pass against baseline fixtures.
3. Performance thresholds met (throughput, memory, latency).

## Phase 3 — Unified DataSource API

**Scope:**

1. Introduce `Direct/Delegated/Both` DataSource base class.
2. Port simple sources, then complex interactive sources.
3. Keep native dialogs and host parser injection.

**Exit gates:**

1. Layout save/restore parity.
2. Output parity across migrated plugins.

## Phase 4 — Transform C ABI and Runtime Integration

**Scope:**

1. Introduce transform C ABI v1 (handle-based, versioned).
2. Port SISO then MIMO transforms.
3. Integrate with `DerivedEngine` scheduling (incremental + batch recompute).

**Exit gates:**

1. Numerical parity within tolerance.
2. No unacceptable live-latency regressions.

## Phase 5 — Optional Enhancements

**Scope:**

1. Host-rendered dialog protocol opt-in (`.ui + JSON` via `plugins/dialog_protocol/`).
2. WASM support (Arrow IPC adapter path).
3. Marketplace trust and distribution UX.

---

# Part XIV — Test and Benchmark Strategy

## 14.1 C ABI Contract Tests

- Required symbol presence.
- Optional symbol probing behavior.
- Multi-instance isolation.
- Configure-before-parse error behavior.
- Ownership correctness (`release`, `pj_parser_free`).

## 14.2 Parity Tests

- Golden input sets through old and new paths.
- Compare series identity, row counts, values, timestamps, null semantics.
- Include mixed-encoding and schema-evolution scenarios.

## 14.3 Engine-Specific Correctness

- Range query correctness on ingested plugin data.
- Latest-at correctness.
- Retention behavior with non-positive timestamps.
- Schema evolution additive-only correctness.

## 14.4 Performance Gates

For each phase with data-path changes, collect:

- Ingest throughput.
- Peak RSS.
- Poll-to-commit latency.
- Transform update latency on large datasets.

Use phase baselines, and fail CI for defined regression thresholds.

---

# Part XV — WASM Considerations

**Runtime**: Wasmtime C API (recommended) or WAMR for lighter weight.

**Memory protocol**: Host serializes to Arrow IPC -> calls `wasm_malloc()` in plugin -> copies bytes into linear memory -> calls plugin function. Output follows reverse path. The SDK provides `wasm_malloc`/`wasm_free` exports automatically.

**Transport**: WASM plugins use `pj_parser_parse_ipc()` / `pj_transform_calculate_ipc()`. Native plugins use the zero-copy C Data Interface path.

---

# Part XVI — Risks and Mitigations

| Risk | Mitigation |
|---|---|
| ABI instability | Strict major/minor governance; contract tests at every gate |
| Engine integration regressions | Adapter + parity tests at every phase gate |
| Ambiguous timestamp/order semantics | Explicit ordering policy and enforcement |
| Transform recompute cost at scale | Incremental strategy (DerivedEngine) with benchmark gates |
| Ecosystem migration friction | SDK, examples, and beta compatibility sprint for external maintainers |
| nanoarrow missing features | Full Arrow C++ on host side as fallback |
| QUiLoader limitations | Register custom widgets; fall back to standard equivalents |
| Dialog protocol too restrictive | Plugin can always fall back to native `QWidget*` escape hatch |
| Arrow conversion overhead | Benchmark early; `append_columns()` bulk path |
| ParserProtobuf split complexity | Start with simpler parsers; Protobuf is Wave 3 |

---

# Part XVII — Open Questions

1. Default ordering policy for out-of-order ingest at host boundary.
2. Batch sizing policy for delegated parse calls under live streaming.
3. Exact retention/eviction policy exposure in plugin-facing configuration.
4. Compatibility layer lifetime and cutover criteria.
5. WASM runtime target and security model for marketplace delivery.
