# PlotJuggler Portable Plugin System — Architecture Plan (v2)

---

# Part I — Goals and Vision

## 1. What We Want to Achieve

PlotJuggler's plugin system should allow **anyone to write, distribute, and install plugins without recompiling PlotJuggler** — regardless of operating system, compiler, Qt version, or C++ standard library.

### 1.1 Core Goals

**G1 — Clean Data Boundary.** Plugins should never touch PlotJuggler's internal data structures (`PlotDataMapRef`). They produce data in a standard, self-describing columnar format (Arrow). The host converts it to its internal representation. This is the key to decoupling.

**G2 — Decoupled Plugin Families.** Data sources (file readers, network streamers) and message parsers (JSON, Protobuf, ROS) should be fully independent. A new parser should automatically work with all existing data sources, and vice versa. Today they are tightly coupled.

**G3 — Unified DataSource.** File loading and live streaming are the same thing at the interface level: a source produces data, the host consumes it. The difference is just pacing (drain fast vs. poll at 50 Hz). Two plugin categories should become one.

**G4 — Distributable Parsers and Transforms.** Parsers and transforms are pure computation — bytes in, Arrow out. They don't need Qt, dialogs, or PlotJuggler headers. They should be compilable and distributable independently, including as WASM modules. A C ABI for these specific plugin types enables cross-compiler, cross-language, and WASM support.

**G5 — Accept Qt for DataSources.** DataSource plugins legitimately need interactive dialogs (file preview, topic selection, connection setup). Forcing these into a C ABI creates artificial complexity. Pin Qt 6.8 LTS for ABI stability, and let DataSource plugins use Qt freely. The trade-off is explicit: DataSources are Qt-coupled; Parsers and Transforms are not.

**G6 — Host-Rendered Dialogs via .ui Files.** DataSource plugins provide their dialog layout as a standard Qt `.ui` file. The host loads and renders the dialog via `QUiLoader`, and the plugin communicates with it through a JSON data-binding protocol. The plugin never instantiates Qt widgets directly — it only describes what to show and reacts to events. This keeps the plugin's Qt coupling minimal (it only uses `QByteArray` and JSON) and opens the door to replacing `.ui` files with other UI descriptions in the future.

### 1.2 What Changes, What Stays

| Plugin type | Qt dependency | Interface | Distributable without recompiling PJ? |
|---|---|---|---|
| **Parser** | None | C ABI + Arrow | Yes (native `.so`, `.wasm`, any language) |
| **Transform** | None | C ABI + Arrow | Yes |
| **DataSource** | Qt 6.8 LTS (minimal) | C++ virtual methods + Arrow + .ui dialog | Only within same Qt 6.8.x series |
| **StatePublisher** | Qt 6.8 LTS | (unchanged for now) | Same as above |
| **ToolboxPlugin** | Qt 6.8 LTS | (unchanged for now) | Same as above |

This is a pragmatic split: pure computation is portable; anything with UI stays in Qt.

### 1.3 The Three Plugin Types

**Parser** (C ABI, no Qt) — A pure decoder: raw bytes in, Arrow RecordBatch out. Knows nothing about data sources, transport, or PlotJuggler internals. Loaded via `dlopen` / WASM runtime.

**Transform** (C ABI, no Qt) — A pure computation: Arrow RecordBatch in, Arrow RecordBatch out. Same loading mechanism as parsers.

**DataSource** (Qt C++ interface, uses Arrow for output) — A unified replacement for both `DataLoader` and `DataStreamer`. Provides a `.ui` file for its configuration dialog. Operates in one of two modes:

- *Direct mode*: Reads a self-contained format (CSV, Parquet, ULog) and produces Arrow tables directly. No separate parser needed.
- *Delegated mode*: Acts as a container or transport (MCAP, MQTT, ZMQ), providing raw messages + metadata. The host matches them to the correct parser. The host also injects parser selection UI into the dialog.

### 1.4 Why Apache Arrow

Arrow is a columnar in-memory data format that is a natural fit for PlotJuggler:

- PlotJuggler's core data is named columns of `(timestamp, double)` pairs — this is exactly what Arrow represents.
- Arrow defines a standard **C Data Interface** — two simple C structs (`ArrowArray`, `ArrowSchema`) that are ABI-stable by specification. This is the plugin boundary.
- Arrow defines an **IPC format** for serializing data to bytes — this is the WASM transport.
- The **nanoarrow** library is a single-header C implementation (~150 KB, Apache 2.0) with zero dependencies. It compiles to WASM trivially. This is the plugin SDK.
- Arrow is the lingua franca of the data ecosystem. A plugin written in Rust, C, or any Arrow-supporting language can produce data that PlotJuggler consumes with no custom serialization.
- **Parquet plugins already use Arrow internally** — the DataLoadParquet plugin produces Arrow tables and then converts them to PlotDataMapRef. The new architecture eliminates that last conversion step.

### 1.5 Data Flow Summary

Both DataSource modes converge at the same point: the host receives **(topic_name, Arrow RecordBatch)** pairs and converts them to `PlotDataMapRef`.

```
DIRECT MODE (CSV, Parquet, ULog)

  DataSource ──→ Arrow tables ──→ Host converts to PlotDataMapRef


DELEGATED MODE (MCAP, MQTT, ZMQ)

  DataSource ──→ raw messages ──→ Host finds Parser
                                      │
                                      ▼
                                  Parser ──→ Arrow tables ──→ Host converts to PlotDataMapRef


TRANSFORM

  Host converts PlotData → Arrow ──→ Transform ──→ Arrow ──→ Host converts back to PlotData
```

---

# Part II — Challenges with the Current Architecture

## 2. Current Plugin System

### 2.1 Plugin Types

From the `plotjuggler_base` source code, there are 6 plugin categories:

| Plugin Type | Base Class | Purpose |
|---|---|---|
| **DataLoader** | `PJ::DataLoader` | Load data from files (CSV, ULog, MCAP, Parquet) |
| **DataStreamer** | `PJ::DataStreamer` | Live data streaming (MQTT, ZMQ, UDP, WebSocket) |
| **MessageParser** | `PJ::ParserFactoryPlugin` | Decode message formats (JSON, Protobuf, ROS, DataTamer) |
| **StatePublisher** | `PJ::StatePublisher` | Publish data at time tracker position |
| **TransformFunction** | `PJ::TransformFunction` | Data transforms (derivative, moving average, etc.) |
| **ToolboxPlugin** | `PJ::ToolboxPlugin` | UI-heavy tools (FFT, Lua editor, Quaternion) |

All inherit from `PJ::PlotJugglerPlugin → QObject`.

### 2.2 Core Data Structure

The central container is `PlotDataMapRef`:

```
PlotDataMapRef
├── numeric:      unordered_map<string, PlotData>         # TimeseriesBase<double>
├── strings:      unordered_map<string, StringSeries>     # TimeseriesBase<StringRef>
├── scatter_xy:   unordered_map<string, PlotDataXY>       # PlotDataBase<double, double>
├── user_defined: unordered_map<string, PlotDataAny>      # TimeseriesBase<std::any>
└── groups:       unordered_map<string, PlotGroup::Ptr>
```

Each `PlotData` is a `std::deque<Point>` where `Point = { double x, double y }` (timestamp + value).

### 2.3 ABI Fragility

Every plugin boundary is riddled with ABI-sensitive types:

| What crosses the boundary | Why it's fragile |
|---|---|
| `QObject` inheritance | Ties plugin to a specific Qt major version |
| C++ virtual tables | Different compilers produce incompatible vtables |
| `std::string`, `std::vector`, `std::unordered_map` | Not ABI-stable across compilers or libstdc++ vs. libc++ |
| `PlotDataMapRef&` passed by reference | Plugin directly mutates host data structures |
| Qt signals (`dataReceived()`, `closed()`, etc.) | Requires `Q_OBJECT` macro, MOC-generated code, same Qt version |
| `QWidget*` from `optionsWidget()` | Deep GUI coupling |

The practical consequence: a plugin must be compiled with the exact same Qt version, compiler, and standard library as PlotJuggler itself. This makes pre-built binary distribution essentially impossible.

### 2.4 Tight Coupling Between Data Sources and Parsers

`DataLoader` and `DataStreamer` both hold a `ParserFactories*` pointer and internally instantiate parsers:

```cpp
class DataStreamer : public PlotJugglerPlugin {
    ParserFactories* _parser_factories;  // set by host
    // plugin calls parser->parseMessage() internally
};

class DataLoader : public PlotJugglerPlugin {
    ParserFactories* _parser_factories;  // set by host
    // plugin calls parser->parseMessage() internally
};
```

This means:
- A streaming plugin (MQTT) must know about the parser system (JSON, Protobuf).
- Adding a new message format requires updating or coordinating with every data source.
- The data source selects, instantiates, and invokes parsers — responsibility that should belong to the host.

From the plugin-by-plugin evaluation: **MQTT, UDP, WebSocket, and ZMQ** all duplicate the same pattern — enumerate `parserFactories()`, populate a `comboBoxProtocol`, embed each parser's `optionsWidget()`. This is exactly the coupling we need to eliminate.

### 2.5 Artificial Separation of DataLoader and DataStreamer

`DataLoader` reads files synchronously and returns data all at once. `DataStreamer` runs a background thread, shares a mutable `PlotDataMapRef` with the host, and uses signals to notify when data is available.

But conceptually they do the same thing: produce time series data. A file is just a fast, finite stream. A live source is just a slow, indefinite stream. The distinction creates two separate plugin interfaces, two loading paths in the host, and two sets of documentation — for what is fundamentally the same operation.

### 2.6 Qt Signals as Plugin-to-Host Communication

`DataStreamer` uses Qt signals for asynchronous notifications:

```cpp
signals:
    void clearBuffers();                        // discard all data
    void removeGroup(std::string group_name);   // remove a series group
    void dataReceived();                        // new data available
    void closed();                              // plugin shut down
    void notificationsChanged(int count);       // UI badge update
```

These require `Q_OBJECT`, MOC compilation, and the same Qt version. They also cross thread boundaries (plugin I/O thread → GUI thread), relying on Qt's queued connection mechanism.

`DataStreamer` also provides `notificationAction()` returning a `QAction*` — a Qt GUI object representing what happens when the user clicks the notification button.

### 2.7 Plugin Dialogs: Interactive, Data-Dependent UI

Many plugins require **multi-step interactive dialogs** before they can produce data. These are not simple parameter forms — they involve live previews, dynamic discovery, and user selection:

| Plugin | Dialog Complexity |
|---|---|
| **CSV** | Delimiter selector, date format options, column list, **live table preview that updates when delimiter changes**, raw text tab (QCodeEditor) |
| **MCAP** | **Topic table** with encoding/schema/message count, filter, multi-selection, timestamp options |
| **Parquet** | Column list, timestamp column selector |
| **ULog** | Parameter display table |
| **MQTT** | Connection form (host/port/TLS certs) → **Connect** → **async topic discovery** → topic selection. Multi-step. |
| **UDP/WS/ZMQ** | Connection form + parser protocol selection |

From the plugin-by-plugin evaluation, all existing dialog widgets are standard Qt widgets (QLineEdit, QComboBox, QTableWidget, QListWidget, QCheckBox, QRadioButton, QPushButton, QSpinBox, QLabel). No plugin does custom painting, OpenGL, or non-standard rendering.

### 2.8 Mid-Operation Error Dialogs

The CSV loader pops up `QMessageBox::warning()` **during loading** for malformed rows, invalid timestamps, and non-monotonic time. These ask the user "Continue or Abort?" mid-operation. MCAP shows similar warnings for encoding mismatches and parser errors.

These modal dialogs during data processing don't fit any clean plugin protocol.

### 2.9 Summary of Challenges

| Challenge | Severity | Solved by |
|---|---|---|
| ABI fragility (Qt, C++, STL) | High | C ABI for Parsers/Transforms; Qt 6.8 LTS for DataSources |
| Data sources coupled to parsers | High | Host dispatches parsers; delegated mode |
| Artificial DataLoader/DataStreamer split | Medium | Unified DataSource |
| Shared mutable PlotDataMapRef | High | Arrow at boundary; host owns conversion |
| Qt signals for async events | Medium | Polling model with `pollEvents()` |
| `optionsWidget()` and dialog coupling | Medium | `.ui` + JSON dialog protocol |
| Parser widget embedding in streamer dialogs | Medium | Host injects parser selector for delegated sources |
| Mid-operation error dialogs | Low | Error policy config + post-load summary |
| `QAction*` for notification button | Low | `pollEvents()` with notification event |
| Layout file save/restore | Medium | JSON config state replaces `xmlSaveState()` |

---

# Part III — Proposed Architecture

## 3. Parser and Transform: Pure C ABI

Parsers and Transforms are pure computation — no GUI, no I/O threads, no dialogs. They use a C ABI with Arrow for data exchange. This makes them distributable as pre-built binaries, compilable from any language, and WASM-capable.

### 3.1 Common Functions (Parser and Transform)

```c
// API version — checked by host immediately after dlopen, before anything else.
// Must return PJ_API_VERSION. If it doesn't match, host refuses to load the plugin.
#define PJ_API_VERSION 1
uint32_t pj_plugin_api_version();

// Every C ABI plugin exports these
const char* pj_plugin_name();       // human-readable name, e.g. "ROS2 Parser"
const char* pj_plugin_version();    // semver, e.g. "1.0.0"

// Lifecycle: host creates one instance per use
int32_t pj_plugin_init(const char* config_json, uint32_t config_len);
void    pj_plugin_destroy();

// Memory management (see §3.6 for ownership rules)
void    pj_plugin_free(void* ptr);
```

The `plugin.json` manifest also carries the API version for early rejection without loading:

```json
{
    "api_version": 1,
    "name": "my_ros2_parser",
    ...
}
```

### 3.2 Parser Interface

```c
// Declare supported encoding(s), e.g. "ros2", "json", "protobuf"
const char* pj_parser_encoding();

// --- Schema ---
// Called once when a new (topic, type) pair is seen.
// schema_data may be a ROS msg def, a .proto descriptor set, etc.
int32_t pj_parser_configure(const char* topic_name,
                            const char* type_name,
                            const uint8_t* schema_data,
                            uint32_t schema_len);

// --- Parse: native variant ---
// Input: raw serialized message bytes + timestamp.
// Output: Arrow RecordBatch via the C Data Interface (zero-copy handoff).
int32_t pj_parser_parse_native(const uint8_t* msg_data,
                               uint32_t msg_len,
                               double timestamp,
                               struct ArrowArray* out_array,
                               struct ArrowSchema* out_schema);

// --- Parse: WASM variant ---
// Input: same message bytes.
// Output: Arrow IPC-serialized RecordBatch.
int32_t pj_parser_parse_ipc(const uint8_t* msg_data,
                            uint32_t msg_len,
                            double timestamp,
                            uint8_t** out_ipc,
                            uint32_t* out_ipc_len);

// --- Optional: describe parameters ---
// Returns a JSON description of configurable parameters.
// If provided, the host can render a settings form for this parser.
const char* pj_parser_describe_parameters();
```

Each RecordBatch can use one of two formats (the host auto-detects by checking for the `_series_name` column):

**Wide format** (preferred for fixed schemas):
```
columns:  _timestamp (float64)  |  field_a (float64)  |  field_b (utf8)  | ...
rows:     one row per data point
```

**Tall format** (for dynamic/unknown schemas):
```
columns:  _series_name (utf8)  |  _timestamp (float64)  |  _value_numeric (float64, nullable)  |  _value_string (utf8, nullable)
rows:     one row per (series, timestamp, value) triple
```

### 3.3 Transform Interface

Transforms operate on Arrow RecordBatches, not individual series. This naturally supports both SISO (single-input single-output, e.g., derivative) and MIMO (multi-input multi-output, e.g., quaternion→RPY) without a future API break.

```c
// Declare input/output schema.
// input_schema_json describes expected input columns:
//   [{"name": "value", "type": "float64"}]                     (SISO)
//   [{"name": "qx", ...}, {"name": "qy", ...}, ...]            (MIMO)
// The host uses this to bind PlotJuggler series to input columns
// and to create output series from the output schema.
const char* pj_transform_input_schema();   // JSON array of {name, type}
const char* pj_transform_output_schema();  // JSON array of {name, type}

// Configure with bound names (host tells the transform the actual series names)
int32_t pj_transform_configure(const char* config_json, uint32_t config_len);

// --- Native variant ---
// Input batch: _timestamp (float64) + input columns matching input_schema
// Output batch: _timestamp (float64) + output columns matching output_schema
int32_t pj_transform_calculate_native(const struct ArrowArray* input_array,
                                      const struct ArrowSchema* input_schema,
                                      struct ArrowArray* out_array,
                                      struct ArrowSchema* out_schema);

// --- WASM variant ---
int32_t pj_transform_calculate_ipc(const uint8_t* input_ipc,
                                   uint32_t input_len,
                                   uint8_t** out_ipc,
                                   uint32_t* out_ipc_len);

void pj_transform_reset();
```

**SISO example** (derivative): `input_schema = [{"name":"value","type":"float64"}]`, `output_schema = [{"name":"value","type":"float64"}]`. Input batch has columns `{_timestamp, value}`, output batch has `{_timestamp, value}`.

**MIMO example** (quaternion→RPY): `input_schema = [{"name":"qx",...}, {"name":"qy",...}, {"name":"qz",...}, {"name":"qw",...}]`, `output_schema = [{"name":"roll",...}, {"name":"pitch",...}, {"name":"yaw",...}]`. The host maps 4 PlotJuggler series to the input columns and creates 3 output series from the output columns.

### 3.4 Plugin Discovery (Parsers and Transforms)

C ABI plugins ship with a `plugin.json` manifest. PlotJuggler scans directories for manifests, loads via `dlopen` (native) or WASM runtime.

```json
{
    "name": "my_ros2_parser",
    "version": "1.0.0",
    "type": "parser",
    "encoding": "ros2",
    "format": "native",
    "library": "libmy_ros2_parser.so",
    "min_host_version": "4.0.0"
}
```

### 3.5 Parser Configuration and Schema Management

Parsers receive configuration via `pj_plugin_init(config_json)`. For parsers that need schema files (e.g., Protobuf .proto files), the config JSON contains paths or serialized schema data.

The host is responsible for managing parser configuration UI:
- For simple parsers (ROS, JSON, DataTamer): no config needed, or `pj_parser_describe_parameters()` describes basic settings.
- For parsers requiring schema files (Protobuf): the host provides a generic "schema file loader" UI based on `pj_parser_describe_parameters()`. The parser declares it needs files, the host shows a file picker, and passes the result via `pj_plugin_init()`.

This replaces the current `optionsWidget()` pattern. The Protobuf parser is split:
- **C ABI core**: accepts a pre-built `FileDescriptorSet` as schema bytes, decodes protobuf messages. Pure computation, no Qt.
- **Host-side schema management**: the host handles proto file loading, include directory management, and descriptor set compilation. This can be a host module or a thin Qt helper.

### 3.6 Memory Ownership Rules

Memory management across a C ABI boundary is error-prone. These rules are absolute:

**Arrow objects (`ArrowArray`, `ArrowSchema`)**: Owned by the producer (the plugin). The consumer (the host) releases them by calling the `release` callback embedded in the struct, per the Arrow C Data Interface specification:

```c
// Host receives Arrow output from plugin, processes it, then releases:
struct ArrowArray array;
struct ArrowSchema schema;
pj_parser_parse_native(msg, len, ts, &array, &schema);
// ... host reads the data ...
array.release(&array);    // calls back into plugin's release function
schema.release(&schema);
```

After `release()` is called, the struct is zeroed (the Arrow spec requires `release = NULL` after release). The host must not access the data after releasing.

**Raw byte buffers** (e.g., `out_ipc` from WASM parse path, `pj_parser_describe_parameters()` return): Allocated by the plugin, freed by the host via `pj_plugin_free()`:

```c
uint8_t* ipc_data;
uint32_t ipc_len;
pj_parser_parse_ipc(msg, len, ts, &ipc_data, &ipc_len);
// ... host deserializes IPC data ...
pj_plugin_free(ipc_data);
```

**Static strings** (`pj_plugin_name()`, `pj_parser_encoding()`, `pj_plugin_version()`): Static lifetime. The host never frees these. They remain valid for the lifetime of the loaded plugin.

**Config JSON passed to `pj_plugin_init()`**: Owned by the host. The plugin must copy any data it needs to retain. The pointer is invalid after `pj_plugin_init()` returns.

| What | Allocated by | Freed by | Mechanism |
|---|---|---|---|
| `ArrowArray` / `ArrowSchema` | Plugin | Host | `array.release(&array)` (Arrow spec) |
| `out_ipc` byte buffers | Plugin | Host | `pj_plugin_free(ptr)` |
| Static name/version strings | Plugin (static) | Nobody | Valid for plugin lifetime |
| `config_json` input | Host | Host | Plugin copies if needed |

---

## 4. DataSource: Qt C++ Interface with Arrow Output

DataSources need Qt for dialogs and are loaded via `QPluginLoader`. The key changes from the current architecture:
1. They return **Arrow tables** instead of mutating `PlotDataMapRef`.
2. They **never instantiate parsers** — delegated-mode sources just forward raw messages.
3. They provide a **`.ui` file** for their configuration dialog, rendered by the host.

### 4.1 Base Class

```cpp
// New base class replacing both PJ::DataLoader and PJ::DataStreamer
class DataSource : public QObject {
    Q_OBJECT
public:
    virtual ~DataSource() = default;

    virtual const char* name() const = 0;

    // --- Mode ---
    enum Mode { Direct = 1, Delegated = 2, Both = 3 };
    virtual Mode mode() const = 0;

    // For file-based sources: supported extensions (e.g. {"csv", "mcap"})
    // Empty for streaming-only sources.
    virtual QStringList fileExtensions() const { return {}; }

    // --- Dialog Protocol (.ui + JSON) ---

    // Returns the .ui file content for the configuration dialog.
    // Empty = no dialog (headless plugin, or uses saved config).
    virtual QByteArray uiFile() const { return {}; }

    // Returns JSON describing how to populate every widget in the .ui.
    // Called initially and after every onWidgetEvent() that returns true.
    virtual QByteArray getWidgetData() const { return {}; }

    // Called when the user interacts with a widget.
    // event_json: {"widget": "name", "type": "event_type", "value": ..., "form_state": {...}}
    // Returns true if widget data changed — host should call getWidgetData() again.
    virtual bool onWidgetEvent(const QByteArray& event_json) { return false; }

    // Called periodically (~500ms) while dialog is open.
    // Returns true if widget data changed (e.g., MQTT topic discovery).
    virtual bool onTick() { return false; }

    // Called when dialog is accepted (OK clicked).
    virtual bool onDialogAccepted() { return true; }

    // Called when dialog is rejected (Cancel clicked or closed).
    virtual void onDialogRejected() {}

    // --- Configuration (for layout save/restore and headless start) ---

    // Serialize the plugin's current config as JSON.
    // Used by the host to save to layout files and to skip the dialog on replay.
    virtual QByteArray saveConfigJson() const { return {}; }

    // Restore config from JSON (saved layout or external config).
    // If this provides enough config, start() can be called without showing a dialog.
    virtual bool loadConfigJson(const QByteArray& config_json) { return false; }

    // --- Lifecycle ---
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() { return false; }   // optional
    virtual bool resume() { return false; }  // optional

    enum State { Idle, Running, Paused, Error };
    virtual State state() const = 0;

    // --- Polling (called by host at ~50 Hz for streaming, tight loop for files) ---
    // Contract: the host drains ALL available data each poll cycle.
    // The plugin returns everything it has buffered since the last poll.
    // For streaming: the host polls at ~50 Hz; the plugin buffers between polls.
    // For files: the host tight-loops calling poll until has_more == false.

    // Direct mode: plugin produces decoded Arrow tables.
    struct ArrowTable {
        QString topic_name;
        ArrowArray array;       // Arrow C Data Interface
        ArrowSchema schema;
    };
    virtual std::vector<ArrowTable> pollDirect(bool& has_more) {
        has_more = false; return {};
    }

    // Delegated mode: plugin forwards raw messages for the host to parse.
    struct RawMessage {
        QString    topic_name;
        QString    encoding;     // "ros2", "json", "protobuf", ...
        QString    type_name;    // "sensor_msgs/JointState", ...
        QByteArray schema;       // empty if unchanged since last message on this topic
        uint64_t   schema_hash;  // hash of schema bytes; host detects changes per-topic
        QByteArray payload;
        double     timestamp;
    };
    virtual std::vector<RawMessage> pollDelegated(bool& has_more) {
        has_more = false; return {};
    }

    // --- Events (replacing Qt signals) ---
    enum EventType {
        ClearBuffers,     // discard all loaded data
        RemoveGroup,      // remove a named group of series
        Closed,           // plugin shut down (e.g., disconnection)
        Notification,     // notification badge update
        Warning           // non-fatal warning (replaces mid-load QMessageBox)
    };
    struct Event {
        EventType type;
        QString   payload;    // group name for RemoveGroup, message for others
        int       int_value;  // count for Notification
    };
    virtual std::vector<Event> pollEvents() { return {}; }
};

#define PJ_DATASOURCE_IID "facontidavide.PlotJuggler4.DataSource"
Q_DECLARE_INTERFACE(DataSource, PJ_DATASOURCE_IID)
```

### 4.2 Why This Design

- **Arrow output**: `pollDirect()` returns Arrow RecordBatches via C Data Interface structs. `PlotDataMapRef` never crosses the boundary.
- **No parser coupling**: `pollDelegated()` returns raw messages with encoding metadata. The host matches `(encoding, type_name)` to a parser. The DataSource never sees a parser.
- **Unified load/stream**: The `has_more` flag unifies file loading (tight loop until `has_more == false`) and streaming (poll at ~50 Hz). One interface, one code path in the host.
- **`.ui` + JSON dialogs**: The plugin provides layout as a `.ui` file; populates widgets via JSON; reacts to events without touching `QWidget*`. The host renders everything.
- **JSON config**: `saveConfigJson()` / `loadConfigJson()` replaces `xmlSaveState()` / `xmlRestoreState()`. Layout files store this JSON blob. Headless start passes saved config directly.
- **Event-based notifications**: `pollEvents()` with `Warning` and `Notification` types replaces both `QMessageBox` during loading and `notificationAction()`. The host decides how to present them (inline warning, popup, status bar, etc.).

### 4.3 Lifecycle and Threading

```
State machine:
  IDLE ──start()──► RUNNING ──stop()──► IDLE
                      │  ▲
                pause()│  │resume()
                      ▼  │
                    PAUSED

Threading contract:
  All host calls (start, stop, pause, resume, poll*, showConfigDialog)
  happen on the GUI thread.
  The plugin may spawn background I/O threads internally.
  The plugin must synchronize its internal threads with poll*() calls
  (e.g., mutex on an internal buffer, swap on poll).
```

For file-based sources, the background thread is optional. The plugin can do all work synchronously inside `poll*()` while the host tight-loops on `has_more`.

### 4.4 Mode Examples

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

## 5. The .ui + JSON Dialog Protocol

### 5.1 Overview

The plugin provides a `.ui` file (designed in Qt Designer) that defines the **layout and widget types**. The host loads it with `QUiLoader` and manages all widget interaction. The plugin communicates with the rendered dialog exclusively through JSON:

```
  Plugin                        Host
  ──────                        ────
  uiFile() ──────────────────► Load with QUiLoader, create QDialog
  getWidgetData() ───────────► Populate all widgets from JSON
                                User interacts...
             ◄── onWidgetEvent({"widget":"comboBox","type":"currentIndexChanged","value":1})
  (plugin reparses, returns true)
  getWidgetData() ───────────► Re-populate changed widgets
                                User clicks OK
             ◄── onDialogAccepted()
  (plugin stores config, ready for start())
```

### 5.2 Widget Data JSON Format

The plugin returns a JSON object keyed by widget `objectName` from the `.ui` file:

```json
{
  "lineEditHost": {"text": "localhost"},
  "lineEditPort": {"text": "1883"},
  "comboBoxDelimiter": {
    "items": [",", ";", " ", "\\t"],
    "current_index": 0
  },
  "checkBoxClamp": {"checked": true},
  "radioLogTime": {"checked": true},
  "spinBoxMaxArray": {"value": 500, "min": 1, "max": 10000},
  "tableWidgetTopics": {
    "headers": ["Topic", "Encoding", "Messages"],
    "rows": [
      ["/imu", "ros2", "15230"],
      ["/gps", "ros2", "3200"]
    ],
    "selected_rows": [0, 1]
  },
  "listWidgetColumns": {
    "items": ["time", "x", "y", "z"],
    "selected_items": [1, 2, 3]
  },
  "labelStatus": {"text": "Connected"},
  "buttonConnect": {"text": "Disconnect", "enabled": true},
  "buttonBox": {"ok_enabled": false},
  "frameConnection": {"enabled": false, "visible": true}
}
```

### 5.3 Widget Event JSON Format

When the user interacts with a widget, the host sends an event to the plugin:

```json
{
  "widget": "comboBoxDelimiter",
  "type": "currentIndexChanged",
  "value": 1,
  "form_state": {
    "lineEditHost": "192.168.1.10",
    "lineEditPort": "1883",
    "comboBoxDelimiter": 1,
    "checkBoxClamp": true
  }
}
```

The `form_state` contains the current value of all editable widgets, so the plugin always has the full dialog state.

### 5.4 Host-Side Widget Binding Layer

The host implements a generic binding layer that maps JSON keys to widgets by `objectName`. This is a finite set matching the widgets actually used in plugins:

| Widget type | JSON → Widget (write) | Widget → JSON (read) | Events forwarded |
|---|---|---|---|
| QLineEdit | set text | read text | textChanged |
| QComboBox | set items + current_index | read current_index | currentIndexChanged |
| QCheckBox | set checked | read checked | toggled |
| QRadioButton | set checked | read checked | toggled |
| QSpinBox | set value, min, max | read value | valueChanged |
| QListWidget | set items, selected_items, enabled | read selected_items | itemSelectionChanged |
| QTableWidget | set headers, rows, selected_rows | read selected_rows | itemSelectionChanged |
| QLabel | set text | — | — |
| QPushButton | set text, enabled | — | clicked |
| QDialogButtonBox | set ok_enabled | — | accepted/rejected |
| QWidget/QFrame | set enabled, visible | — | — |

This binding layer is approximately 200-300 lines of code in the host, covering all widgets used by existing plugins.

### 5.5 Special Actions

Some interactions require host-side support beyond simple value binding:

**File picker**: A button declared as a file picker in the widget data:
```json
{
  "buttonLoadCert": {
    "action": "file_picker",
    "filter": "Certificate files (*.crt *.pem)",
    "title": "Select Server Certificate"
  }
}
```
When clicked, the host runs `QFileDialog`, then sends an event to the plugin with the selected path.

**Async refresh (onTick)**: For dialogs that need periodic updates (MQTT topic discovery), the host calls `onTick()` every ~500ms while the dialog is open. If it returns true, the host calls `getWidgetData()` and refreshes the dialog. For performance, `getWidgetData()` returns the full state — the host diffs against the previous state and only updates changed widgets.

### 5.6 Host-Injected UI for Delegated-Mode Sources

For delegated-mode DataSources, the **host** is responsible for parser selection. The host:

1. Detects `mode() == Delegated` (or `Both`).
2. After loading the plugin's `.ui` file, inserts a "Message Protocol" section into the dialog:
   - A `QComboBox` listing available parser names (from C ABI parser manifests).
   - A parser-specific config area below the combobox (from `pj_parser_describe_parameters()`).
3. When the user selects a parser, the host manages the parser config area.
4. The selected parser and its config are stored by the host, not the DataSource plugin.

This means the DataSource's `.ui` file does NOT include protocol selection — the host adds it. The DataSource just declares `mode() == Delegated` and the host knows to inject the parser selector.

This eliminates the duplicated `comboBoxProtocol` + `layoutOptions` pattern currently found in MQTT, UDP, WebSocket, and ZMQ plugins.

### 5.7 Custom Widgets

The CSV dialog currently uses `QCodeEditor` for the raw text preview tab. `QUiLoader` doesn't know about custom widgets by default.

Solution: The host registers known custom widgets with `QUiLoader` (e.g., `QCodeEditor` → `QTextEdit` with syntax highlighting). Alternatively, the `.ui` file uses a standard `QTextEdit` and the host applies highlighting.

### 5.8 Layout File Save/Restore

Layout files need to replay a DataSource without showing its dialog:

**Save**: The host calls `saveConfigJson()` and stores the JSON blob in the layout XML.

**Restore**: The host calls `loadConfigJson(saved_json)` with the saved config. If the plugin accepts it (returns true), the host skips the dialog and calls `start()` directly.

For delegated-mode sources, the host also saves/restores the selected parser and its config independently.

This replaces the current `xmlSaveState()` / `xmlRestoreState()` / `plugin_config` mechanism.

---

## 6. Host-Side Architecture

### 6.1 Adapter Layer for C ABI Plugins

Parsers and Transforms loaded via C ABI need an adapter that wraps them for the host's internal use. Native plugins return Arrow via the C Data Interface (zero-copy pointers); WASM plugins return Arrow IPC bytes. The plugin API stays split — native plugins should not pay IPC overhead. But internally, the host uses a single `ArrowBatch` wrapper that holds either representation and lazily converts, so all downstream code (Arrow→PlotDataMapRef conversion) has one path, not two.

```
┌─────────────────────────────────────────────────────────┐
│  Host Application                                       │
│                                                         │
│  ┌──────────────────┐    ┌───────────────────────────┐  │
│  │ NativeParserAdapter│    │ WasmParserAdapter        │  │
│  │ (wraps dlopen)   │    │ (wraps wasmtime)          │  │
│  │                  │    │                           │  │
│  │ calls extern "C" │    │ serializes Arrow IPC,     │  │
│  │ functions, passes│    │ copies into WASM linear   │  │
│  │ ArrowArray by ptr│    │ memory, calls function    │  │
│  └────────┬─────────┘    └───────────┬───────────────┘  │
│           │                          │                  │
│           └──────────┬───────────────┘                  │
│                      ▼                                  │
│            Parser dispatch table                        │
│            (encoding, type) → adapter instance           │
│                      │                                  │
│                      ▼                                  │
│            Arrow → PlotDataMapRef conversion            │
│                                                         │
│  Plugin SDK (ships with plugin, not PJ):                │
│  ┌──────────────────────────────────────────────────┐   │
│  │  - nanoarrow (Arrow C Data Interface)            │   │
│  │  - pj_plugin_api.h (C function signatures)       │   │
│  │  - Nothing about Qt, PlotDataMapRef, or PJ core  │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 6.2 Data Conversion: Arrow → PlotDataMapRef

Both DataSource modes produce **(topic_name, Arrow RecordBatch)** pairs. The host conversion is the same either way:

```
  Direct mode:     DataSource ──→ ArrowTable[]
                                        │
  Delegated mode:  DataSource ──→ RawMessage[] ──→ Parser ──→ Arrow batch
                                                                    │
                   Both converge here: ◄────────────────────────────┘
                          │
                          ▼
                   for each (topic_name, batch):
                     group = getOrCreateGroup(topic_name)

                     if schema has "_series_name" column:
                       // Tall format — dynamic schema
                       for each row:
                         name = topic_name + "/" + row["_series_name"]
                         if _value_numeric is not null:
                           getOrCreateNumeric(name, group).pushBack(...)
                         else:
                           getOrCreateStringSeries(name, group).pushBack(...)
                     else:
                       // Wide format — fixed schema
                       timestamps = batch["_timestamp"]
                       for each column C (skip _timestamp):
                         name = topic_name + "/" + C.name
                         if float64 → getOrCreateNumeric(name, group).pushBack(...)
                         if utf8    → getOrCreateStringSeries(name, group).pushBack(...)
```

### 6.3 Parser Dispatch for Delegated Mode

When a delegated-mode DataSource returns `RawMessage` structs, the host manages parser selection and caching **per-topic**. A single DataSource can produce messages with different encodings on different topics (e.g., an MCAP file with both ROS2 and protobuf channels, or a ZMQ stream mixing JSON and CBOR). The host handles this transparently:

```
  Host receives RawMessage{topic, encoding, type_name, schema, schema_hash, payload, timestamp}
      │
      ▼
  parser_cache.find(topic)
      │
      ├── Found, schema_hash matches → use cached parser instance
      │
      ├── Found, schema_hash differs → schema evolved!
      │         call pj_parser_configure() with new schema
      │         update cached hash
      │
      └── Not found:
            │
            ▼
          parser_registry.find(encoding)     // from plugin.json manifests
               │
               ▼
          Create new parser instance
          Call pj_parser_configure(topic, type_name, schema_bytes)
          Store in parser_cache[topic] with schema_hash
               │
               ▼
          pj_parser_parse_native(payload, timestamp) → Arrow batch
               │
               ▼
          Arrow → PlotDataMapRef conversion (same as direct mode)
```

The cache key is the **topic name**, not the encoding. This means `/imu` using `ros2` and `/config` using `json` within the same DataSource each get their own parser instance automatically. Schema evolution is detected via `schema_hash`: if the hash changes for an existing topic, the host reconfigures the parser with the new schema before continuing to parse.

Schema caching: the DataSource sets `schema` bytes to empty and keeps `schema_hash` unchanged for messages where the schema hasn't changed. The host only calls `pj_parser_configure()` when it sees a new topic or a changed `schema_hash`.

### 6.4 Error and Warning Handling

The current pattern of `QMessageBox::warning()` during loading is replaced by the `Warning` event type in `pollEvents()`. The host handles warnings according to a configurable policy:

**During dialog**: The DataSource can set a policy in its `.ui` file (e.g., radio buttons for "On errors: skip / stop"). This is part of the plugin's config.

**During loading/streaming**: The plugin emits `Warning` events via `pollEvents()`. The host:
1. Counts warnings.
2. Optionally shows an inline notification in the loading progress dialog.
3. After loading completes, shows a summary ("Loaded 15,230 rows, skipped 23 with errors").
4. If the configured policy is "stop on error," the host calls `stop()`.

This is cleaner than modal dialogs interrupting the loading loop.

### 6.5 Notification Button

The current `notificationAction()` returning `QAction*` is replaced by:

1. The plugin emits `Notification` events via `pollEvents()` with a message and count.
2. The host manages the notification badge and button.
3. When clicked, the host shows the notification message (stored from the last event).

The plugin no longer needs to create or manage `QAction` objects.

### 6.6 Arrow Transport: Native vs. WASM

| | Native plugins | WASM plugins |
|---|---|---|
| **Library** | `.so` / `.dll` / `.dylib` | `.wasm` |
| **Loading** | `dlopen` / `LoadLibrary` | Wasmtime C API |
| **Arrow transport** | C Data Interface (pointer-based) | IPC format (serialized bytes, one copy) |
| **Plugin SDK** | nanoarrow.h | nanoarrow.h + nanoarrow_ipc.h |

---

## 7. Plugin SDK

### 7.1 Contents

```
plotjuggler-plugin-sdk/
├── include/
│   ├── nanoarrow/
│   │   ├── nanoarrow.h         # Arrow C Data Interface + utilities
│   │   └── nanoarrow_ipc.h     # IPC read/write (WASM plugins)
│   ├── pj_plugin_api.h         # C function declarations
│   └── pj_plugin_helpers.hpp   # Optional C++ convenience wrappers
├── examples/
│   ├── parser_json/
│   ├── transform_derivative/
│   └── CMakeLists.txt
├── CMakeLists.txt
└── README.md
```

### 7.2 C++ Convenience Layer

Plugin authors get header-only helpers hiding the Arrow boilerplate:

```cpp
class MyParser : public pj::PluginParser {
public:
    const char* name() override { return "my_json_parser"; }
    const char* encoding() override { return "json"; }

    void parse(const uint8_t* msg, uint32_t len, double timestamp,
               pj::OutputBatch& output) override
    {
        output.add_numeric("temperature", timestamp, 23.5);
        output.add_string("status", timestamp, "ok");
    }
};
PJ_EXPORT_PARSER(MyParser);
```

`PJ_EXPORT_PARSER` generates all `extern "C"` functions and Arrow plumbing automatically.

---

## 8. WASM Considerations

**Runtime**: Wasmtime C API (recommended) or WAMR for lighter weight.

**Memory protocol**: Host serializes to Arrow IPC → calls `wasm_malloc()` in plugin → copies bytes into linear memory → calls plugin function. Output follows the reverse path. The SDK provides `wasm_malloc`/`wasm_free` exports automatically.

---

## 9. Plugin-by-Plugin Migration Assessment

### 9.1 DataSource Plugins

| Plugin | Target Mode | Difficulty | Key Notes |
|---|---|---|---|
| **DataLoadCSV** | Direct | Medium | Interactive delimiter/preview dialog. Mid-load QMessageBox → Warning events + error policy. QCodeEditor → register or fallback. |
| **DataLoadMCAP** | Delegated | Medium | Biggest beneficiary — all parser coupling code removed. Just returns raw messages. |
| **DataLoadParquet** | Direct | Easy | **Already uses Arrow internally.** Just stop converting to PlotDataMapRef. |
| **DataLoadULog** | Direct | Easy | Simple format, simple dialog. |
| **DataStreamMQTT** | Delegated | Hard | Multi-step dialog (connect → discover → select). Async topic discovery via `onTick()`. TLS cert file pickers. Host injects parser selector. |
| **DataStreamSample** | Direct | Easy | No dialog. Good first PoC. |
| **DataStreamUDP** | Delegated | Medium | Host injects parser selector. Simple connection form. |
| **DataStreamWebsocket** | Delegated | Medium | Same pattern as UDP. |
| **DataStreamZMQ** | Delegated | Medium | Per-topic parser caching handled by host. SUB/XSUB mode selector. |

### 9.2 Parser Plugins

| Plugin | Difficulty | Key Notes |
|---|---|---|
| **ParserDataTamer** | Easy | Pure computation. No UI, no settings. Ideal C ABI candidate. |
| **ParserIDL** | Easy | Same as DataTamer. |
| **ParserLineInflux** | Easy | Dynamic field names → good test for tall format Arrow output. |
| **ParserROS** (ROS1+ROS2) | Medium | Two separate C ABI parsers. Schema handling for message definitions. Recursive field traversal. |
| **ParserProtobuf** | Hard | **Split architecture**: C ABI core (deserialize given FileDescriptorSet) + host-side schema management (proto file loading, include dirs, type selection). |

### 9.3 Unchanged Plugins

| Plugin | Reason |
|---|---|
| **StatePublisherCSV** | Reads data (not writes). Different pattern. Future phase. |
| **StatePublisherZMQ** | Same. |
| **ToolboxFFT** | Deeply UI-integrated. Reads AND writes PlotDataMapRef. |
| **ToolboxLuaEditor** | Scripting environment. Most complex plugin. |
| **ToolboxQuaternion** | ToolboxPlugin stays unchanged. The `QuaternionToRollPitchYaw` TransformFunction can become a C ABI Transform — the MIMO RecordBatch→RecordBatch API (§3.3) supports its 4→3 pattern natively. |

---

## 10. Implementation Phases

### Phase 1: Foundation — Arrow + C ABI Parsers + DataSource Base (8-10 weeks)

**1a. Arrow infrastructure (2 weeks)**

1. Integrate nanoarrow into the build system.
2. Implement host-side Arrow → `PlotDataMapRef` conversion (wide + tall formats).
3. Define the `ArrowArray`/`ArrowSchema` wrapper types.
4. Implement format auto-detection (wide vs. tall by schema inspection).

**1b. C ABI Parser interface (2-3 weeks)**

1. Define `pj_plugin_api.h` for parsers.
2. Create the parser SDK (nanoarrow + C++ helpers + `PJ_EXPORT_PARSER` macro).
3. Implement parser loading via `dlopen` + `plugin.json` manifests.
4. Implement host-side parser dispatch: match `(encoding, type_name)` → parser instance.
5. Implement parser schema caching per (topic, encoding, type) triple.
6. Port **ParserDataTamer** (simplest, validates the C ABI pipeline end-to-end).

**1c. New DataSource base class (2-3 weeks)**

1. Define the `DataSource` base class with `pollDirect()`, `pollDelegated()`, `pollEvents()`.
2. Implement the host-side polling loop (50 Hz for streaming, tight loop for files).
3. Port **DataStreamSample** (direct mode, no dialog — validates lifecycle + Arrow output).
4. Port **DataLoadParquet** (direct mode — already has Arrow; validates file loading path).

**1d. Host-side dialog engine (2 weeks)**

1. Implement `QUiLoader`-based dialog rendering.
2. Implement the widget binding layer (JSON ↔ widget synchronization).
3. Implement `onWidgetEvent` → `getWidgetData` round-trip.
4. Implement `onTick()` polling for async dialogs.
5. Implement file picker action support.
6. Port **DataLoadULog** (simple dialog, validates the .ui + JSON protocol).

### Phase 2: Delegated Mode + Parser Ecosystem (4-6 weeks)

**2a. Delegated mode pipeline (2-3 weeks)**

1. Implement host-side parser injection UI for delegated-mode DataSource dialogs.
2. Port **ParserROS** (ROS1 + ROS2) to C ABI — validates complex parser with schema handling.
3. Port **DataLoadMCAP** (delegated mode — validates the full pipeline: MCAP → raw messages → host parser dispatch → Arrow → PlotDataMapRef).
4. Verify MCAP works with both ROS1 and ROS2 parsers without any coupling between them.

**2b. Remaining parsers (1-2 weeks)**

1. Port **ParserIDL** to C ABI.
2. Port **ParserLineInflux** to C ABI (test tall format output).
3. Port **ParserProtobuf** — C ABI core + host-side schema file management.
4. Implement `pj_parser_describe_parameters()` for Protobuf's schema config.

**2c. Remaining DataSources (1-2 weeks)**

1. Port **DataStreamMQTT** (delegated, hardest dialog — validates multi-step flow + async discovery + file pickers).
2. Port **DataStreamUDP**, **DataStreamWebsocket**, **DataStreamZMQ** (all delegated, simple dialogs — now trivial since host handles parser selection).
3. Port **DataLoadCSV** (direct mode, complex dialog — validates interactive preview update, error policy).

### Phase 3: Cleanup + Transform C ABI (3-4 weeks)

1. Define Transform C ABI (`pj_transform_*` functions) with RecordBatch→RecordBatch API.
2. Create transform SDK + `PJ_EXPORT_TRANSFORM` macro.
3. Port SISO transforms (derivative, moving average, etc.).
4. Port `QuaternionToRollPitchYaw` as a MIMO transform (validates the 4→3 pattern).
5. Remove old `DataLoader`, `DataStreamer`, `ParserFactoryPlugin` base classes.
6. Implement `saveConfigJson()` / `loadConfigJson()` for all ported DataSources.
7. Update layout file format to use JSON config blobs.
8. Implement error policy config and post-load warning summary in the host.

### Phase 4: WASM Support (3-4 weeks)

1. Integrate Wasmtime C API (optional dependency).
2. Implement WASM adapter with Arrow IPC transport for parsers and transforms.
3. Compile a parser to WASM, validate parity with native.

### Phase 5: Ecosystem (ongoing)

1. Plugin marketplace infrastructure.
2. Documentation and examples (SDK, .ui file guidelines, C ABI parser tutorial).
3. Third-party plugin onboarding.

---

## 11. Open Questions

1. **Wide vs. Tall RecordBatch**: Support both with auto-detect? Current plan says yes.
2. **Batch size for streaming**: One parse call per message, or batch multiple messages per poll cycle?
3. **Incremental transforms**: Full history or delta since last call?
4. **Thread safety**: Host guarantees single-threaded calls to each plugin instance — confirmed by design.
5. **Widget data diffing**: For performance with large topic lists (MQTT), the host should diff `getWidgetData()` output against previous state and only update changed widgets. Implementation detail, not a protocol change.

---

## 12. Dependencies

| Component | Library | Size | License | Required? |
|---|---|---|---|---|
| Arrow C Data Interface | nanoarrow | ~150 KB header-only | Apache 2.0 | Yes |
| UI file loading | Qt QUiLoader | Part of Qt | LGPL/Commercial | Yes (for DataSources) |
| WASM runtime | Wasmtime C API | ~20 MB | Apache 2.0 | Optional (Phase 4) |
| JSON | nlohmann/json or rapidjson | header-only | MIT | Yes |

---

## 13. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| nanoarrow missing features | Medium | Low | Full Arrow C++ on host side as fallback |
| QUiLoader limitations (custom widgets) | Low | Low | Register custom widgets; fall back to standard equivalents |
| Widget binding layer edge cases | Medium | Medium | Start with the 10 widget types actually used; extend as needed |
| Dialog protocol too restrictive for future plugins | Low | Medium | Plugin can always fall back to `showConfigDialog(QWidget*)` escape hatch |
| Arrow conversion overhead | Medium | Medium | Benchmark early; Phase 5+ columnar refactor |
| Wasmtime C API changes | Low | Medium | Pin version, abstract behind adapter |
| Plugin author adoption | Medium | High | Make SDK simple; provide examples; migrate all built-in plugins first |
| ParserProtobuf split complexity | Medium | Medium | Start with simpler parsers; Protobuf is Phase 2 |
