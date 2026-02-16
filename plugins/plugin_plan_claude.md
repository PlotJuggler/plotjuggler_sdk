# PlotJuggler Portable Plugin System — Architecture Plan (v3)

This document supersedes `plugin-plan.md` (v2). It preserves the original architecture and vision while incorporating all critical fixes from the Codex and Claude reviews. Where the original plan stated something well, this document references it briefly rather than repeating it verbatim.

---

# Part I — Goals and Vision

## 1.1 Core Goals (Unchanged)

The five original goals remain the foundation:

- **G1 — Clean Data Boundary.** Arrow at the plugin boundary; host owns `PlotDataMapRef`.
- **G2 — Decoupled Plugin Families.** DataSources and Parsers are fully independent; the host dispatches.
- **G3 — Unified DataSource.** One interface replaces both `DataLoader` and `DataStreamer`.
- **G4 — Distributable Parsers and Transforms.** C ABI + Arrow enables cross-compiler, cross-language, and WASM support.
- **G5 — Accept Qt for DataSources.** Pin Qt 6.8 LTS. DataSources use Qt freely.

## 1.2 Non-Goals (Early Phases)

1. Rewriting all DataSource dialogs into `.ui` + JSON immediately.
2. Solving every plugin type in Phase 1 (focus first on parser/transform pipeline).
3. WASM support before the native C ABI pipeline is proven.

## 1.3 Revised Goal: G6 — Dialog Strategy

**G6 — Native Dialogs First, Host-Rendered Dialogs as Opt-In.**

The original plan required all DataSource dialogs to use `.ui` + JSON from the start. Both reviews identified this as the highest-risk component. The revised strategy:

- DataSources keep native `QWidget*` dialogs initially (Phase 2).
- The `.ui` + JSON host-rendered dialog protocol is developed in Phase 5 as an **opt-in** improvement.
- Complex plugins (MQTT, CSV) may keep native dialogs indefinitely — the real value is in the data boundary, not the dialog boundary.

## 1.4 What Changes, What Stays

| Plugin type | Qt dependency | Interface | Distributable without recompiling PJ? |
|---|---|---|---|
| **Parser** | None | C ABI + Arrow (handle-based) | Yes (native `.so`, `.wasm`, any language) |
| **Transform** | None | C ABI + Arrow (handle-based) | Yes |
| **DataSource** | Qt 6.8 LTS | C++ virtual methods + Arrow + native `QWidget*` dialog | Only within same Qt 6.8.x series |
| **StatePublisher** | Qt 6.8 LTS | (unchanged for now) | Same as above |
| **ToolboxPlugin** | Qt 6.8 LTS | (unchanged for now) | Same as above |

## 1.5 Data Flow

Unchanged from original plan Section 1.5. Both DataSource modes converge at **(topic_name, Arrow RecordBatch)** pairs; the host converts to `PlotDataMapRef`.

---

# Part II — Challenges

The original plan (Part II, Sections 2.1-2.9) thoroughly documents:

- ABI fragility across Qt/C++/STL boundaries
- Tight coupling between DataSources and Parsers
- Artificial DataLoader/DataStreamer split
- Shared mutable `PlotDataMapRef`
- Qt signals as plugin-to-host communication
- Complex interactive plugin dialogs
- Mid-operation error dialogs

All of these remain valid. The challenge summary table (Section 2.9) is unchanged except for one adjustment: the `.ui` + JSON dialog protocol is now deferred rather than being a primary solution, reducing immediate risk.

---

# Part III — C ABI Plugin Interface

## 3.1 Instance Handles (Critical Fix)

The original plan used global functions without instance handles. This breaks when the host needs multiple instances of the same parser (e.g., two MCAP files open with the same encoding). Every successful C plugin API (OBS, JACK, GStreamer) uses instance handles.

All C ABI functions now take an opaque handle as first parameter after creation.

## 3.2 ABI Versioning Strategy

The original plan had a single `PJ_API_VERSION` integer. This is insufficient for safe evolution.

**Versioning model** (inspired by OpenGL/Vulkan context versioning):

```c
// Each ABI family has independent versioning
#define PJ_PARSER_ABI_MAJOR 1    // breaking changes increment this
#define PJ_PARSER_ABI_MINOR 0    // additive changes increment this

#define PJ_TRANSFORM_ABI_MAJOR 1
#define PJ_TRANSFORM_ABI_MINOR 0
```

**Compatibility rules:**

1. Host loads only plugins with matching `abi_major`.
2. Host accepts plugins with `abi_minor <= host_abi_minor` (older plugins on newer host).
3. New optional functions are probed via `dlsym` returning NULL — the host checks before calling.
4. The `plugin.json` manifest carries version info for early rejection without `dlopen`.

## 3.3 Parser Interface

```c
// --- Info (called once after dlopen, before any instance creation) ---

typedef struct {
    uint32_t    abi_major;
    uint32_t    abi_minor;
    const char* plugin_name;       // e.g. "ROS2 Parser"
    const char* plugin_version;    // semver, e.g. "1.0.0"
    const char* encoding;          // e.g. "ros2", "json", "protobuf"
} pj_parser_info;

int32_t pj_parser_get_info(pj_parser_info* out_info);

// --- Instance lifecycle ---

typedef struct pj_parser_instance* pj_parser_handle;

// Create an instance. config_json may contain global settings (e.g., max_array_size).
// The handle is opaque — the plugin allocates its own state internally.
int32_t pj_parser_create(const char* config_json,
                         uint32_t config_len,
                         pj_parser_handle* out_handle);

void pj_parser_destroy(pj_parser_handle h);

// Free plugin-allocated memory (IPC buffers, parameter descriptions, etc.)
void pj_parser_free(void* ptr);

// --- Schema configuration (called once per new topic/type pair) ---
// config_json carries per-topic settings (e.g., timestamp field override,
// max array size). Distinct from the global config passed to pj_parser_create.

int32_t pj_parser_configure(pj_parser_handle h,
                            const char* topic_name,
                            const char* type_name,
                            const uint8_t* schema_data,
                            uint32_t schema_len,
                            const char* config_json,
                            uint32_t config_len);

// --- Parse: native variant ---
// Input: raw message bytes + host-provided timestamp (fallback).
// Output: Arrow RecordBatch via C Data Interface.
// Timestamp contract: the _timestamp column in the output batch is authoritative.
// The parser MAY use host_timestamp as a fallback when the message contains no
// embedded timestamp, or it MAY ignore it and extract timestamps from the payload.

int32_t pj_parser_parse_native(pj_parser_handle h,
                               const uint8_t* msg_data,
                               uint32_t msg_len,
                               double host_timestamp,
                               struct ArrowArray* out_array,
                               struct ArrowSchema* out_schema);

// --- Parse: WASM variant (optional, probed via dlsym) ---

int32_t pj_parser_parse_ipc(pj_parser_handle h,
                            const uint8_t* msg_data,
                            uint32_t msg_len,
                            double host_timestamp,
                            uint8_t** out_ipc,
                            uint32_t* out_ipc_len);

// --- Optional: describe configurable parameters (probed via dlsym) ---
// Returns JSON description. Host may render a settings form.
// Returned pointer freed via pj_parser_free().

const char* pj_parser_describe_parameters(pj_parser_handle h);
```

### RecordBatch Formats

Unchanged from original plan Section 3.2:

- **Wide format** (fixed schemas): `_timestamp` (float64) + one column per field.
- **Tall format** (dynamic schemas): `_series_name` (utf8) + `_timestamp` (float64) + `_value_numeric` (float64, nullable) + `_value_string` (utf8, nullable).

Auto-detection by presence of `_series_name` column.

### Arrow Mapping for scatter_xy and user_defined

The original plan only mapped timeseries data. Two additional data types need explicit Arrow representation:

**Scatter/XY data** (no synthetic timestamp):
```
columns:  _x (float64)  |  _y (float64)  |  _series_name (utf8, optional)
```
Host detects scatter format by presence of `_x`/`_y` without `_timestamp`. Maps to `PlotDataMapRef::scatter_xy`.

**Metadata / user_defined**:
Attached as a JSON blob in the Arrow schema metadata (key: `pj:metadata`). The host extracts it and maps to `PlotDataMapRef::user_defined` or stores it as application-level metadata. This handles the MCAP metadata use case (`addUserDefined`).

```c
// In the ArrowSchema metadata:
// key = "pj:metadata", value = JSON string
// e.g. {"file_path": "/data/log.mcap", "start_time": 1234567890.0}
```

## 3.4 Transform Interface

Same handle-based pattern. Unchanged from original plan Section 3.3 except for handle parameters:

```c
typedef struct pj_transform_instance* pj_transform_handle;

int32_t pj_transform_get_info(pj_transform_info* out_info);  // same pattern as parser

int32_t pj_transform_create(const char* config_json, uint32_t config_len,
                            pj_transform_handle* out_handle);
void    pj_transform_destroy(pj_transform_handle h);
void    pj_transform_free(void* ptr);

const char* pj_transform_input_schema(pj_transform_handle h);
const char* pj_transform_output_schema(pj_transform_handle h);

int32_t pj_transform_configure(pj_transform_handle h,
                               const char* config_json, uint32_t config_len);

int32_t pj_transform_calculate_native(pj_transform_handle h,
                                      const struct ArrowArray* input_array,
                                      const struct ArrowSchema* input_schema,
                                      struct ArrowArray* out_array,
                                      struct ArrowSchema* out_schema);

// Optional WASM variant (probed via dlsym)
int32_t pj_transform_calculate_ipc(pj_transform_handle h,
                                   const uint8_t* input_ipc, uint32_t input_len,
                                   uint8_t** out_ipc, uint32_t* out_ipc_len);

void pj_transform_reset(pj_transform_handle h);
```

SISO and MIMO examples are unchanged from original plan Section 3.3.

## 3.5 Plugin Discovery

### Manifest

Each C ABI plugin ships a `plugin.json`:

```json
{
    "name": "my_ros2_parser",
    "type": "parser",
    "abi_family": "pj_parser",
    "abi_major": 1,
    "abi_minor": 0,
    "plugin_version": "1.2.0",
    "encoding": "ros2",
    "format": "native",
    "library": "libmy_ros2_parser.so",
    "min_host_version": "4.0.0"
}
```

### Search Paths (scanned in order, first match wins for conflicts)

1. User marketplace cache: `~/.local/share/plotjuggler/plugins/`
2. User local plugins: `~/.plotjuggler/plugins/`
3. System install: `<install_prefix>/lib/plotjuggler/plugins/`
4. Environment variable: `$PJ_PLUGIN_PATH` (colon-separated on Unix, semicolon on Windows)

### Loading Policy

1. **`RTLD_LOCAL`** is mandatory for `dlopen` — prevents symbol bleed between plugins sharing identical `extern "C"` names.
2. **Platform naming**: `lib<name>.so` (Linux), `<name>.dll` (Windows), `lib<name>.dylib` (macOS). The manifest `library` field uses the platform-native name.
3. **Symbol resolution order**: resolve all required symbols first (`pj_parser_get_info`, `pj_parser_create`, `pj_parser_destroy`, `pj_parser_free`, `pj_parser_configure`, `pj_parser_parse_native`). If any are missing, reject the plugin with a diagnostic message.
4. **Optional symbols** (`pj_parser_parse_ipc`, `pj_parser_describe_parameters`): probed via `dlsym`, NULL means unsupported.

## 3.6 Memory Ownership Rules

Unchanged from original plan Section 3.6. The four ownership patterns remain:

| What | Allocated by | Freed by | Mechanism |
|---|---|---|---|
| `ArrowArray` / `ArrowSchema` | Plugin | Host | `array.release(&array)` (Arrow spec) |
| `out_ipc` byte buffers | Plugin | Host | `pj_parser_free(ptr)` |
| Static info strings | Plugin (static) | Nobody | Valid for plugin lifetime |
| `config_json` input | Host | Host | Plugin copies if needed |

## 3.7 Timestamp Ownership Contract

Explicit rule (was underspecified in original plan):

1. The host passes `host_timestamp` to `pj_parser_parse_native()`. This is the transport-layer timestamp (e.g., receive time, MCAP log time).
2. The parser's output `_timestamp` column is **authoritative**. The parser may:
   - Use `host_timestamp` directly (simple parsers with no embedded time).
   - Extract timestamps from the message payload (ROS header stamps, Protobuf timestamps).
   - Produce multiple rows with different timestamps from a single message.
3. The host always reads timestamps from the output `_timestamp` column, never from `host_timestamp` after parsing.

---

# Part IV — DataSource Interface

## 4.1 Base Class (Native QWidget* Dialogs)

The key difference from the original plan: DataSources use native `QWidget*` dialogs (like today) rather than `.ui` + JSON. This eliminates the highest-risk component while preserving all other improvements.

```cpp
class DataSource : public QObject {
    Q_OBJECT
public:
    virtual ~DataSource() = default;

    virtual const char* name() const = 0;

    // --- Mode ---
    enum Mode { Direct = 1, Delegated = 2, Both = 3 };
    virtual Mode mode() const = 0;

    // For file-based sources: supported extensions (e.g. {"csv", "mcap"})
    virtual QStringList fileExtensions() const { return {}; }

    // --- Dialog (native QWidget*) ---
    // Returns the plugin's configuration dialog. The host embeds it in a
    // standard QDialog with OK/Cancel buttons.
    // For delegated-mode sources, the host appends a parser selector section
    // below the plugin's widget (see Section 4.5).
    // Return nullptr for headless plugins.
    virtual QWidget* configWidget(QWidget* parent) { return nullptr; }

    // Called when the user accepts the dialog.
    virtual bool onDialogAccepted() { return true; }
    virtual void onDialogRejected() {}

    // --- Post-Load Hook ---
    // Called by the host after data loading/initial streaming completes.
    // The plugin may return a widget to display (e.g., ULog parameters table).
    // The host shows it in a non-modal dialog. Return nullptr for no post-load UI.
    virtual QWidget* postLoadWidget(QWidget* parent) { return nullptr; }

    // --- Configuration (for layout save/restore and headless start) ---
    virtual QByteArray saveConfigJson() const { return {}; }
    virtual bool loadConfigJson(const QByteArray& config_json) { return false; }

    // --- Lifecycle ---
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() { return false; }
    virtual bool resume() { return false; }

    enum State { Idle, Running, Paused, Error };
    virtual State state() const = 0;

    // --- Polling (called by host at ~50 Hz for streaming, tight loop for files) ---

    // Direct mode: plugin produces decoded Arrow tables.
    struct ArrowTable {
        QString     topic_name;
        ArrowArray  array;
        ArrowSchema schema;
    };
    virtual std::vector<ArrowTable> pollDirect(bool& has_more) {
        has_more = false; return {};
    }

    // Delegated mode: plugin forwards raw messages for the host to parse.
    struct RawMessage {
        QString    topic_name;
        QString    encoding;
        QString    type_name;
        QByteArray schema;
        uint64_t   schema_hash;
        QByteArray payload;
        double     timestamp;
    };
    virtual std::vector<RawMessage> pollDelegated(bool& has_more) {
        has_more = false; return {};
    }

    // --- Events (replacing Qt signals) ---
    enum EventType {
        ClearBuffers,
        RemoveGroup,
        Closed,
        Notification,
        Warning
    };
    struct Event {
        EventType type;
        QString   payload;
        int       int_value;
    };
    virtual std::vector<Event> pollEvents() { return {}; }
};

#define PJ_DATASOURCE_IID "facontidavide.PlotJuggler4.DataSource"
Q_DECLARE_INTERFACE(DataSource, PJ_DATASOURCE_IID)
```

## 4.2 Key Differences from Original Plan

1. **`configWidget(parent)` replaces `.ui` + JSON protocol** — plugins create their own widgets as they do today. The host wraps them in a standard dialog shell.
2. **`postLoadWidget(parent)` is new** — addresses the ULog parameters dialog that currently opens after data loading. The host calls this after the initial data drain completes and shows the returned widget in a non-modal dialog.
3. **Everything else is unchanged** — `pollDirect`, `pollDelegated`, `pollEvents`, lifecycle state machine, `saveConfigJson`/`loadConfigJson` all remain as designed.

## 4.3 Config Precedence Rules

When restoring configuration, the host applies this precedence (highest to lowest):

1. **Layout-restored config** — `loadConfigJson()` with saved JSON blob. Highest priority for session replay.
2. **User global preferences** — `QSettings`-based defaults for new sessions (e.g., Protobuf include directories, MQTT default host).
3. **Plugin built-in defaults** — what the plugin uses when no config is provided.

When a layout is loaded, the host calls `loadConfigJson()` with the saved config. The plugin is responsible for merging this with any global preferences it manages. The host stores parser config (for delegated sources) separately from the DataSource config.

## 4.4 Warning and Error UX

The `Warning` event type in `pollEvents()` replaces mid-load `QMessageBox` dialogs. The host accumulates warnings during loading:

1. Counts warnings and shows an inline progress indicator ("Loading... 23 warnings").
2. After loading completes, shows a summary dialog: "Loaded 15,230 rows. 23 rows skipped due to errors. [Show Details]".
3. The "Show Details" button opens a scrollable list of warning messages with row/line context.
4. Before loading, the DataSource can expose an error policy in its `configWidget()` (e.g., radio buttons for "On errors: skip / stop").

This preserves user agency (inspect and review after the fact) without blocking the load loop with modal dialogs.

## 4.5 Host-Injected Parser Selector for Delegated Sources

For `mode() == Delegated` or `Both`, the host:

1. Detects the mode.
2. Wraps the plugin's `configWidget()` in a layout that appends a "Message Protocol" section below:
   - A `QComboBox` listing available parser names (from C ABI parser manifests).
   - A parser-specific config area (from `pj_parser_describe_parameters()`).
3. The selected parser and its config are stored by the host, not the DataSource.

This eliminates the duplicated `comboBoxProtocol` + `layoutOptions` pattern in MQTT, UDP, WebSocket, and ZMQ.

## 4.6 Lifecycle and Threading

Unchanged from original plan Section 4.3. All host calls happen on the GUI thread. The plugin may spawn internal I/O threads and must synchronize with `poll*()` calls.

---

# Part V — Host-Side Architecture

## 5.1 Arrow to PlotDataMapRef Conversion

The host conversion logic handles all RecordBatch formats:

```
for each (topic_name, batch):
    group = getOrCreateGroup(topic_name)

    // Detect format by schema inspection
    if schema has "_x" and "_y" columns (no "_timestamp"):
        // Scatter format -> PlotDataMapRef::scatter_xy
        for each row:
            name = topic_name + "/" + (row["_series_name"] or "default")
            getOrCreateScatterXY(name, group).pushBack(row["_x"], row["_y"])

    else if schema has "_series_name" column:
        // Tall format -> dynamic series
        for each row:
            name = topic_name + "/" + row["_series_name"]
            if _value_numeric is not null:
                getOrCreateNumeric(name, group).pushBack(ts, val)
            else:
                getOrCreateStringSeries(name, group).pushBack(ts, val)

    else:
        // Wide format -> one series per column
        timestamps = batch["_timestamp"]
        for each column C (skip _timestamp):
            name = topic_name + "/" + C.name
            if float64 -> getOrCreateNumeric(name, group).pushBack(...)
            if utf8    -> getOrCreateStringSeries(name, group).pushBack(...)

    // Extract metadata sidecar if present
    if schema metadata has "pj:metadata":
        store in PlotDataMapRef::user_defined or application metadata
```

## 5.2 Parser Dispatch for Delegated Mode

Unchanged from original plan Section 6.3. Key details:

- Cache key is **topic name** (not encoding) — supports mixed-encoding sources.
- Schema evolution detected via `schema_hash`; reconfigure on change.
- Each topic gets its own parser instance (created via `pj_parser_create` + `pj_parser_configure`).
- Parser instances are created from the parser registry (populated from `plugin.json` manifests).

One clarification from the reviews: when the user selects a specific parser in the dialog (for delegated sources), that selection applies as the **default** for new topics. But if a source provides explicit encoding metadata per-message (e.g., MCAP channels with different encodings), the per-message encoding takes precedence over the user default. This allows mixed-encoding MCAP files to work correctly without the user manually selecting parsers for each channel.

## 5.3 Adapter Layer for C ABI Plugins

Unchanged from original plan Section 6.1. Native plugins use Arrow C Data Interface (zero-copy); WASM plugins use Arrow IPC (one copy). The host uses a single `ArrowBatch` wrapper internally.

## 5.4 Notification Button

Unchanged from original plan Section 6.5. `pollEvents()` with `Notification` type replaces `QAction*`.

---

# Part VI — Plugin SDK

## 6.1 Contents

```
plotjuggler-plugin-sdk/
+-- include/
|   +-- nanoarrow/
|   |   +-- nanoarrow.h           # Arrow C Data Interface + utilities
|   |   +-- nanoarrow_ipc.h       # IPC read/write (WASM plugins only)
|   +-- pj_plugin_api.h           # C function declarations (handle-based)
|   +-- pj_plugin_helpers.hpp     # Optional C++ convenience wrappers
+-- examples/
|   +-- parser_json/              # Minimal parser example
|   +-- parser_ros2/              # Schema-based parser example
|   +-- transform_derivative/     # SISO transform example
|   +-- transform_quaternion/     # MIMO transform example
|   +-- CMakeLists.txt
+-- tests/
|   +-- test_abi_contract.c       # Validates symbol presence and lifecycle
|   +-- test_memory_ownership.c   # Validates Arrow release callbacks
+-- CMakeLists.txt
```

## 6.2 C++ Convenience Layer

Updated to use handle-based API:

```cpp
class MyParser : public pj::PluginParser {
public:
    const char* name() override { return "my_json_parser"; }
    const char* encoding() override { return "json"; }

    // Called per-instance, state is managed by the base class
    void configure(std::string_view topic, std::string_view type_name,
                   std::span<const uint8_t> schema) override { }

    void parse(const uint8_t* msg, uint32_t len, double host_timestamp,
               pj::OutputBatch& output) override
    {
        output.add_numeric("temperature", host_timestamp, 23.5);
        output.add_string("status", host_timestamp, "ok");
    }
};
PJ_EXPORT_PARSER(MyParser);
```

`PJ_EXPORT_PARSER` generates all `extern "C"` functions including `pj_parser_create` (allocates a `MyParser` instance) and `pj_parser_destroy` (deletes it). Each instance has independent state.

---

# Part VII — Migration Assessment

## 7.1 DataSource Plugins

| Plugin | Target Mode | Difficulty | Key Notes |
|---|---|---|---|
| **DataStreamSample** | Direct | Easy | No dialog. First PoC target. |
| **DataLoadParquet** | Direct | Easy | Already uses Arrow internally. Stop converting to PlotDataMapRef. |
| **DataLoadULog** | Direct | Easy | Simple format. Use `postLoadWidget()` for parameters table. |
| **DataLoadCSV** | Direct | Medium | Interactive delimiter/preview dialog stays native. Mid-load QMessageBox becomes Warning events + post-load summary. |
| **DataLoadMCAP** | Delegated | Medium | Biggest beneficiary — all parser coupling removed. Just returns raw messages. |
| **DataStreamMQTT** | Delegated | Hard | Multi-step dialog (connect, discover, select) stays native. Async topic discovery via internal timer. Host injects parser selector. TLS cert file pickers. |
| **DataStreamUDP** | Delegated | Medium | Host injects parser selector. Uses custom `LineEdit` widget (stays native). |
| **DataStreamWebsocket** | Delegated | Medium | Same pattern as UDP. Custom `LineEdit`. |
| **DataStreamZMQ** | Delegated | Medium | Per-topic parser caching by host. SUB/XSUB mode selector. Custom `LineEdit`. |

## 7.2 Parser Plugins

| Plugin | Difficulty | Key Notes |
|---|---|---|
| **ParserDataTamer** | Easy | Pure computation. No UI, no settings. Ideal first C ABI parser. |
| **ParserIDL** | Easy | Same as DataTamer. |
| **ParserLineInflux** | Easy | Dynamic field names — good test for tall format Arrow output. |
| **ParserROS** (ROS1+ROS2) | Medium | Two separate C ABI parsers. Schema handling for message definitions. Recursive field traversal. |
| **ParserProtobuf** | Hard | **Split architecture**: C ABI core (deserialize given FileDescriptorSet) + host-side schema management (proto file loading, include dirs, descriptor compilation). |

## 7.3 Additional Plugins (Previously Omitted)

| Plugin | Type | Migration Stance |
|---|---|---|
| **PluginsZcm/dataload_zcm** | DataLoader | Delegated DataSource. ZCM messages need a ZCM parser (C ABI). Medium difficulty. |
| **PluginsZcm/datastream_zcm** | DataStreamer | Delegated DataSource. Same ZCM parser. Medium difficulty. |
| **VideoViewer** | StatePublisher | Unchanged. Stays on legacy interface. Reads data, does not produce it. |

## 7.4 ROS Plugins (Same Maintainer)

| Plugin | Migration Notes |
|---|---|
| **DataStreamROS2** | Delegated DataSource. Uses `rclcpp` for DDS subscription — receives CDR-serialized messages. Maps to `encoding = "ros2"`, `type_name = "sensor_msgs/msg/JointState"`, `schema = ROS message definition`, `payload = CDR bytes`. |
| **DataLoadROS2** | Delegated DataSource. Reads rosbag2 files. Same encoding model as DataStreamROS2. |
| **TopicPublisherROS2** | StatePublisher — unchanged. |

**ROS integration note**: `DataStreamROS2` doesn't receive "raw bytes" in the generic sense — it uses DDS middleware for topic discovery and gets CDR-serialized ROS messages. The Delegated mode model maps cleanly: the ROS2 parser (C ABI) handles CDR deserialization. These plugins are ported as part of the main migration (same maintainer).

## 7.5 Unchanged Plugins

| Plugin | Reason |
|---|---|
| **StatePublisherCSV** | Reads data (not writes). Different pattern. Future phase. |
| **StatePublisherZMQ** | Same. |
| **ToolboxFFT** | Deeply UI-integrated. Reads AND writes PlotDataMapRef. |
| **ToolboxLuaEditor** | Scripting environment. Most complex plugin. |
| **ToolboxQuaternion** | ToolboxPlugin stays unchanged. The `QuaternionToRollPitchYaw` TransformFunction can become a C ABI Transform — the MIMO RecordBatch API supports its 4-to-3 pattern. |

## 7.6 Custom Widget Inventory

The following custom widgets are used in existing plugin `.ui` files and must be accounted for in any future `.ui` + JSON dialog protocol work:

| Widget | Source | Used By |
|---|---|---|
| `QCodeEditor` | Third-party | DataLoadCSV (raw text preview tab) |
| `LineEdit` | `plotjuggler_base/include/PlotJuggler/line_edit.h` | DataStreamUDP, DataStreamWebsocket, DataStreamZMQ |

Since Phase 2 uses native `QWidget*` dialogs, these work without any changes. If the `.ui` + JSON protocol is adopted later (Phase 5), the host's `QUiLoader` must register these custom widgets.

---

# Part VIII — Implementation Phases

## Phase 0: Decouple Parsers in Current Qt C++ System (3-4 weeks)

**Goal**: Prove the dispatch architecture without any new ABI boundaries. Highest-value fix with lowest risk.

1. Move parser selection/instantiation from MQTT, UDP, WebSocket, ZMQ plugins into the host.
2. Host provides a parser selector widget that these plugins embed (or the host injects).
3. Remove duplicated `comboBoxProtocol` + `layoutOptions` pattern from all four streamers.
4. Keep all existing interfaces (`DataStreamer`, `DataLoader`, `ParserFactoryPlugin`) unchanged.
5. Keep `PlotDataMapRef` as the data boundary — no Arrow yet.

**Exit gates:**
- Functional parity with current behavior for all affected streamers.
- No regression in parser selection UX.
- Mixed-encoding sources (MCAP with ROS2 + Protobuf channels) work correctly.

## Phase 1: Arrow Infrastructure + C ABI Parsers (6-8 weeks)

**1a. Arrow infrastructure (2-3 weeks)**

1. Integrate nanoarrow into the build system.
2. Implement host-side Arrow to `PlotDataMapRef` conversion (wide, tall, scatter formats).
3. Implement format auto-detection by schema inspection.
4. Implement metadata sidecar extraction (`pj:metadata`).

**1b. C ABI parser interface (3-4 weeks)**

1. Define `pj_plugin_api.h` with handle-based parser API.
2. Create the parser SDK (nanoarrow + C++ helpers + `PJ_EXPORT_PARSER` macro).
3. Implement plugin discovery: scan directories, parse `plugin.json` manifests.
4. Implement `dlopen` loading with `RTLD_LOCAL`, required/optional symbol resolution.
5. Implement host-side parser dispatch: match `(encoding, type_name)` to parser instance.
6. Port **ParserDataTamer** as first C ABI parser (validates full pipeline end-to-end).

**Exit gates:**
- C ABI contract tests pass (symbol presence, lifecycle, memory ownership).
- ParserDataTamer produces identical output through C ABI as through the old path.

### Performance Validation Gate

Before proceeding to Phase 2, benchmark the full pipeline:

| Metric | Test Case | Acceptable Overhead |
|---|---|---|
| Throughput | 1M-message MCAP file, ROS2 parser | <= 20% slower than legacy path |
| Memory | Same file | <= 30% more peak RSS |
| Latency | Live stream at 1 kHz | <= 2ms additional latency per poll cycle |

If any threshold is exceeded, adjust Arrow conversion strategy before continuing.

## Phase 2: New DataSource Base Class (4-6 weeks)

**2a. DataSource base class (2-3 weeks)**

1. Define the `DataSource` base class with `pollDirect()`, `pollDelegated()`, `pollEvents()`, native `configWidget()`, `postLoadWidget()`.
2. Implement the host-side polling loop (50 Hz for streaming, tight loop for files).
3. Implement host-injected parser selector for delegated-mode sources.
4. Port **DataStreamSample** (direct mode, no dialog — validates lifecycle + Arrow output).
5. Port **DataLoadParquet** (direct mode — already has Arrow; validates file loading path).

**2b. Simple DataSources (2-3 weeks)**

1. Port **DataLoadULog** (direct mode, simple dialog, validates `postLoadWidget()` for parameters).
2. Port **DataStreamUDP**, **DataStreamWebsocket**, **DataStreamZMQ** (delegated, simple dialogs — now trivial since host handles parser selection).

**Exit gates:**
- All ported DataSources produce identical output to legacy versions.
- Layout save/restore works (`saveConfigJson`/`loadConfigJson`).

## Phase 3: Delegated Mode Pipeline + Remaining Parsers (4-6 weeks)

**3a. Parser ecosystem (2-3 weeks)**

1. Port **ParserROS** (ROS1 + ROS2) to C ABI — validates complex parser with schema handling.
2. Port **ParserIDL** and **ParserLineInflux** to C ABI.
3. Port **ParserProtobuf** — C ABI core + host-side schema management.

**3b. Complex DataSources (2-3 weeks)**

1. Port **DataLoadMCAP** (delegated mode — validates full pipeline: MCAP to raw messages to host parser dispatch to Arrow to PlotDataMapRef).
2. Port **DataStreamMQTT** (delegated, hardest dialog — validates multi-step flow + async discovery).
3. Port **DataLoadCSV** (direct mode, complex dialog — validates interactive preview, error policy, post-load warning summary).
4. Port **ZCM plugins** (delegated mode).

**Exit gates:**
- All parsers produce identical output through C ABI path.
- MCAP works with ROS1, ROS2, Protobuf, and JSON parsers without coupling.
- MQTT multi-step dialog works correctly with host-injected parser selector.

## Phase 4: Transform C ABI + Cleanup (3-4 weeks)

1. Define transform C ABI (`pj_transform_*` functions) with handle-based RecordBatch API.
2. Create transform SDK + `PJ_EXPORT_TRANSFORM` macro.
3. Port SISO transforms (derivative, moving average, etc.).
4. Port `QuaternionToRollPitchYaw` as MIMO transform (validates the 4-to-3 pattern).
5. Implement delta/batching strategy for transforms on large historical series to avoid full-history recompute.
6. Implement error policy config and post-load warning summary in the host.

**Exit gates:**
- Transform output matches legacy to within floating-point tolerance.
- Live update latency acceptable on datasets with 1M+ points.

## Phase 5: .ui + JSON Dialog Protocol (Optional, 4-6 weeks)

This is the opt-in dialog modernization. Complex plugins can keep native dialogs indefinitely.

1. Implement `QUiLoader`-based dialog rendering.
2. Implement widget binding layer (JSON to/from widget synchronization) for the ~10 widget types used.
3. Register custom widgets (`QCodeEditor`, `LineEdit`) with `QUiLoader`.
4. Implement `onWidgetEvent` / `getWidgetData` round-trip.
5. Implement `onTick()` polling for async dialogs.
6. Migrate willing simple plugins (DataLoadULog, DataStreamSample) as proof of concept.

The full `.ui` + JSON protocol design is in the original plan Sections 5.1-5.8 and remains valid for this phase.

## Phase 6: WASM Support (3-4 weeks)

1. Integrate Wasmtime C API (optional dependency).
2. Implement WASM adapter with Arrow IPC transport for parsers and transforms.
3. Compile a parser to WASM, validate parity with native.
4. Memory protocol: host serializes Arrow IPC, calls `wasm_malloc()` in plugin, copies bytes into linear memory.

## Phase 7: Ecosystem (Ongoing)

1. Remove old `DataLoader`, `DataStreamer`, `ParserFactoryPlugin` base classes once all built-in plugins are ported.
2. Plugin marketplace infrastructure.
3. Documentation: SDK guide, C ABI parser tutorial, DataSource migration guide.
4. Third-party plugin onboarding (prioritize `plotjuggler-ros-plugins`).

---

# Part IX — Testing Strategy

## 9.1 C ABI Contract Tests

A standalone test harness (no PlotJuggler dependency) that validates any C ABI plugin:

| Test | What It Validates |
|---|---|
| Symbol presence | All required symbols resolve via `dlsym` |
| Info correctness | `pj_parser_get_info` returns valid `abi_major`, non-null strings |
| Lifecycle | `create` then `destroy` with no leaks (run under ASan) |
| Multi-instance | Two `create` calls return different handles; operations are independent |
| Configure-before-parse | `parse_native` returns error if called before `configure` |
| Memory ownership | Arrow `release` callbacks fire correctly; `pj_parser_free` works |
| Optional symbols | Missing optional symbols (`parse_ipc`, `describe_parameters`) return NULL from `dlsym` |

Ship this harness as part of the SDK so plugin authors can validate their plugins locally.

## 9.2 Compatibility Parity Tests

Same input through legacy and new paths; compare output:

| Input | Comparison |
|---|---|
| MCAP file with ROS2 messages | Series names, point counts, timestamp values (within epsilon) |
| MCAP file with mixed encodings | All channels parsed correctly, no missing series |
| CSV file with errors | Same rows loaded, same rows skipped, same warning count |
| ULog file | Series match, parameters table match |
| Live MQTT stream (recorded) | Replay against both paths, compare accumulated data |

Use a golden-file approach: record expected output once, compare on every CI run.

## 9.3 Performance Benchmarks

Run as part of CI with regression detection:

| Benchmark | Metric | Threshold |
|---|---|---|
| MCAP load (1M messages, ROS2) | Wall time, peak RSS | Defined in Phase 1 gate |
| CSV load (10M rows) | Wall time | <= 1.5x legacy |
| Live stream parse (10 kHz) | Per-poll latency | <= 2ms |
| Transform (1M points, derivative) | Calculate time | <= 2x direct iteration |
| Arrow to PlotDataMapRef conversion | Throughput (points/sec) | >= 10M points/sec |

## 9.4 Dialog Behavior Tests

Automated tests for key UI flows (can use QTest framework):

- CSV: change delimiter, verify preview updates correctly.
- MQTT: connect, discover topics, select, verify config saved/restored.
- MCAP: filter topics, verify selection persists across dialog re-open.
- Delegated sources: verify host-injected parser selector appears and functions.

---

# Part X — Dependencies and Risks

## 10.1 Dependencies

| Component | Library | Size | License | Required? |
|---|---|---|---|---|
| Arrow C Data Interface | nanoarrow | ~150 KB header-only | Apache 2.0 | Yes |
| WASM runtime | Wasmtime C API | ~20 MB | Apache 2.0 | Optional (Phase 6) |
| JSON | nlohmann/json or rapidjson | header-only | MIT | Yes |
| UI file loading | Qt QUiLoader | Part of Qt | LGPL/Commercial | Optional (Phase 5) |

Note: `QUiLoader` is now optional since the `.ui` + JSON protocol is deferred to Phase 5.

## 10.2 Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Arrow conversion overhead | Medium | Medium | Performance gate at end of Phase 1; benchmark before committing to remaining phases |
| Transform roundtrip cost for large series | Medium | High | Delta/batching strategy in Phase 4; benchmark with 1M+ point series |
| Plugin author adoption | Medium | High | Ship SDK with examples + contract tests; migrate all built-in plugins first |
| ROS ecosystem disruption | Low | Medium | Same maintainer; ROS plugins ported as part of the main migration |
| nanoarrow missing features | Low | Low | Full Arrow C++ on host side as fallback |
| `.ui` + JSON dialog protocol sluggishness | Medium | Medium | Deferred to Phase 5; complex plugins can keep native dialogs |
| C ABI evolution pressure | Low | Medium | Strict major/minor policy; optional symbols via `dlsym` probing |
| ParserProtobuf split complexity | Medium | Medium | Start with simpler parsers; Protobuf is Phase 3 |

---

# Part XI — Open Questions

## Resolved (from original plan)

These questions from the original plan are now resolved:

| Question | Resolution |
|---|---|
| Wide vs. Tall RecordBatch | Support both with auto-detect. Also added scatter format. |
| Thread safety | Host guarantees single-threaded calls to each plugin instance — confirmed by handle-based design. |
| Widget data diffing | Deferred — `.ui` + JSON is Phase 5. |

## Open

1. **Batch size for streaming**: One parse call per message, or batch multiple messages per poll cycle? Batching reduces function call overhead but adds latency. Needs prototyping in Phase 1.

2. **Incremental transforms**: Full history or delta since last call? For live streaming at 50 Hz with growing series, full recompute becomes expensive. Options:
   - Always full history (simplest, may be too slow for large series).
   - Host tracks last-processed index, passes only new points.
   - Plugin declares "stateless" (full recompute) or "incremental" (delta-capable).

3. **ROS2 subscription model**: `DataStreamROS2` currently uses `rclcpp` to subscribe to DDS topics and receives deserialized or CDR-serialized messages. Needs a prototype to validate the Delegated mode RawMessage model maps cleanly to the CDR byte extraction path.

4. **Parser-to-parser composition**: Can a parser call another parser? E.g., a "compressed protobuf" parser that decompresses then delegates to the protobuf parser. Current design says no — each parser is independent. If needed, this could be a host-level feature (pipeline parsers).

5. **Hot reload of plugins**: Can the host unload and reload a plugin without restarting? Useful for development. Requires careful handle invalidation and resource cleanup.

6. **Marketplace trust model**: Code signing, sandboxing, review process for marketplace-distributed plugins. Out of scope for architecture but critical for distribution.
