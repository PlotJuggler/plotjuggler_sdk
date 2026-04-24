# Writing a DataSource Plugin

> **Tracks the v4 plugin ABI** (`PJ_ABI_VERSION == 4`). For the full
> evolution rules (tail-slot gating, MIN_VTABLE_SIZE, ABI-FROZEN vs
> ABI-APPENDABLE structs, Arrow C Data Interface at the write boundary,
> PJ_NOEXCEPT discipline) see `ARCHITECTURE.md`. This guide walks
> through the author-facing workflow; `ARCHITECTURE.md` is the binding
> reference when the two disagree.

## What is a DataSource?

A DataSource plugin is a shared library (`.so` / `.dylib` / `.dll`) that
acquires data — from files, network streams, hardware, etc. — and feeds it
into PlotJuggler. Plugins link only against `pj_base` (no Qt, no host
internals) and communicate through a stable C ABI.

## Quick Start

1. Subclass `PJ::FileSourceBase` (file importer) or `PJ::StreamSourceBase`
   (live stream), or `PJ::DataSourcePluginBase` for full control.
2. Implement the required virtuals (see Common Patterns below).
3. Export with `PJ_DATA_SOURCE_PLUGIN(YourClass, R"({"name":"...","version":"..."})")`
4. Build as a shared library linking `pj_base`

A complete example lives at `pj_plugins/examples/mock_data_source.cpp`.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>

class MyCsvLoader : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }

  PJ::Status importData() override;
};
```

### 2. Implement the work method

When `importData()` (or `onStart()`/`onPoll()` for streams) is called, both
host bindings are already available via `writeHost()` and `runtimeHost()`.
Return `okStatus()` on success, or `unexpected("reason")` on failure.

```cpp
PJ::Status MyCsvLoader::importData() {
  // Create a topic and write data
  auto topic = writeHost().ensureTopic("my/topic");
  if (!topic) {
    return PJ::unexpected(topic.error());
  }

  const PJ::sdk::NamedFieldValue fields[] = {
      {.name = "temperature", .value = 23.5}};
  auto status = writeHost().appendRecord(
      *topic, PJ::Timestamp{1000}, PJ::Span(fields));
  if (!status) {
    return PJ::unexpected(status.error());
  }

  return PJ::okStatus();
}
```

### 3. Export the plugin

At file scope, after the class definition. The second argument is a JSON
manifest string literal (see Manifest Schema below):

```cpp
PJ_DATA_SOURCE_PLUGIN(MyCsvLoader,
    R"({"name":"CSV Loader","version":"1.0.0","file_extensions":[".csv"]})")
```

This generates the `extern "C"` entry point that the host resolves via dlsym.
The manifest is embedded as a compile-time constant in the vtable, so the host
can read it without creating an instance.

### 4. Build

```cmake
add_library(my_source_plugin SHARED my_source.cpp)
target_link_libraries(my_source_plugin PRIVATE pj_base)
```

No other dependencies are needed for headless sources. If your source includes
a configuration dialog (see Dialog Integration below), also link `pj_dialog_sdk`:

```cmake
target_link_libraries(my_source_plugin PRIVATE pj_base pj_dialog_sdk)
```

## Common Patterns

DataSource plugins fall into two families. A **finite importer** reads a file
or snapshot, writes all records, then self-terminates. A **continuous streamer**
connects to a live source, does incremental work in `onPoll()`, and runs until
the host calls `stop()`.

The SDK provides two derived base classes that manage the lifecycle state
machine for you: `FileSourceBase` and `StreamSourceBase`. Both live in
`<pj_base/sdk/data_source_patterns.hpp>`. For full manual control, subclass
`DataSourcePluginBase` directly (see `pj_plugins/examples/mock_data_source.cpp`).

### File importer — CSV file loader

Subclass `FileSourceBase` and implement `importData()`. The base class handles
state transitions, host notifications, and `requestStop()` automatically.

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>

// File I/O and JSON parsing — use whatever libraries you prefer.
// #include <fstream>
// #include <my_json_lib.h>

class CsvFileLoader : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }

  PJ::Status loadConfig(std::string_view json) override {
    config_ = std::string(json);
    // Extract "filepath" from the JSON config envelope.
    // Use whatever JSON library your plugin links.
    // path_ = parse(json)["filepath"];
    return PJ::okStatus();
  }

  std::string saveConfig() const override { return config_; }

  // Helper: simple split string
  static std::vector<std::string> splitString(const std::string& str, char delimiter);

  // Helper:  count the number of rows in the file
  int64_t countRows(std::ifstream& file);

  // Helper: parse a string to double. it may fail and return std::nullopt
  std::optional<double> parseToDouble(const std::string& value_str);

  PJ::Status importData() override {
    // Open the file at path_.
    std::ifstream file(path_);
    if (!file) {
      return PJ::unexpected("cannot open " + path_);
    }

    // create the topic. Topics are group of fields sharing a timestamp
    auto topic = writeHost().ensureTopic("csv_table");

    // Let's assume that the first row contains the name of the column
    std::string line; // one line of the CSV
    std::getline(file, line);
    auto column_names = splitString(line, ',');

    // Optional: pre-register columns for the faster bound-write path.
    // Without this, fields are auto-created on first non-null write.
    for(const auto& name: column_names) {
      writeHost().ensureField(*topic, name, PJ::PrimitiveType::kFloat64);
    }

    // prepare one NamedFieldValue per column
    std::vector<PJ::sdk::NamedFieldValue> row_fields;
    for(const auto& name: column_names) {
      row_fields.push_back({name, PJ::kNull});
    }

    const int64_t total_rows = countRows(file);
    runtimeHost().progressStart("Importing CSV", total_rows, true);

    // parse all the other lines
    int64_t row = 0;
    while (std::getline(file, line)) {
      auto row_values = splitString(line, ',');
      for(size_t index = 0; index < row_values.size(); index++) {
        // if we fail to parse the value string, use PJ::kNull
        row_fields[index].value = parseToDouble(row_values[index]).value_or(PJ::kNull);
      }
       // Push data. Timestamps are nanoseconds since Unix epoch.
      // Here we use row number for simplicity — real plugins should
      // extract or compute an absolute nanosecond timestamp.
      PJ::Timestamp timestamp = row;
      writeHost().appendRecord(*topic, timestamp, row_fields);
      // update the progress bar
      runtimeHost().progressUpdate(row++);
    }
    return PJ::okStatus();
  }

 private:
  std::string config_;
  std::string path_;
};

PJ_DATA_SOURCE_PLUGIN(CsvFileLoader,
    R"({"name":"CSV File Loader","version":"1.0.0",)"
    R"("description":"Import numeric CSV files",)"
    R"("file_extensions":[".csv",".tsv"]})")
```

Key traits of `FileSourceBase`:
- `capabilities()` automatically includes `kCapabilityFiniteImport`; you
  provide additional flags via `extraCapabilities()`.
- All work goes in `importData()` — the base class calls it from `start()`,
  manages state transitions, and calls `requestStop()` on success.
- Return `okStatus()` on success, `unexpected("reason")` on failure.
- `stop()` and `currentState()` are managed by the base class.
- `progressFinish()` is called automatically after `importData()` returns —
  plugins should NOT call it themselves.
- **Filepath contract**: the host passes `{"filepath":"/path/to/file", ...}`
  via `loadConfig()`. Extract and preserve the `"filepath"` key.

### Continuous streamer — UDP receiver (delegated ingest)

Subclass `StreamSourceBase` and implement `onStart()`, `onPoll()`, and
`onStop()`. The base class manages the state machine.

This example uses **delegated ingest** — the canonical pattern for transport
sources where the payload encoding varies. The source pushes raw bytes to the
host, which routes them through the appropriate `MessageParser` plugin. The
source never decodes payloads itself.

Because `onPoll()` is called from the host's thread at the host's chosen rate,
a source that calls `recv()` directly inside `onPoll()` risks losing data when
the host polls too slowly. The correct pattern: spawn your own receive thread,
buffer incoming data, and use `onPoll()` to flush the buffer into the host.

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

class UdpReceiver : public PJ::StreamSourceBase {
 public:

  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDelegatedIngest;
  }

  PJ::Status loadConfig(std::string_view json) override {
    config_ = std::string(json);
    // Extract parser encoding from config. The dialog or saved layout
    // provides this — e.g. "json", "protobuf", "ros1msg".
    encoding_ = parseJson(json)["encoding"];
    return PJ::okStatus();
  }

  std::string saveConfig() const override { return config_; }

  PJ::Status onStart() override {
    fd_ = openSocket(9870);

    // Bind a parser for the configured encoding. The host resolves the
    // parser library, creates a parser instance, binds the write host
    // and schema, and returns a handle for pushRawMessage().
    binding_ = runtimeHost().ensureParserBinding({
        .topic_name = "udp/data",
        .parser_encoding = encoding_,
    });
    if (!binding_) {
      return PJ::unexpected(binding_.error());
    }

    // Spawn a background thread that runs a blocking recv loop.
    // Data arrives asynchronously — we must not rely on the host's
    // poll rate to drain the socket buffer.
    running_.store(true);
    recv_thread_ = std::thread([this] { recvLoop(); });

    runtimeHost().reportMessage(
        PJ::DataSourceMessageLevel::kInfo, "listening on :9870");

    return PJ::okStatus();
  }

  PJ::Status onPoll() override {
    // Flush buffered data into the host. Called periodically from the
    // host's thread — this is the only place we may call host methods.
    // Swap the buffer out under the lock so the recv thread isn't blocked.
    std::vector<BufferedMsg> batch;
    {
      std::lock_guard lock(mu_);
      batch.swap(buffer_);
    }

    for (const auto& msg : batch) {
      auto status = runtimeHost().pushRawMessage(
          *binding_, msg.timestamp,
          PJ::Span<const uint8_t>(msg.payload.data(), msg.payload.size()));
      if (!status) {
        return PJ::unexpected(status.error());
      }
    }
    return PJ::okStatus();
  }

  void onStop() override {
    running_.store(false);
    closeSocket(fd_);  // unblocks the blocking recvDatagram()
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
    fd_ = -1;
  }

 private:
  // Background receive loop — runs on its own thread.
  // Never calls host methods; only writes to the shared buffer.
  void recvLoop() {
    std::vector<uint8_t> datagram;
    while (running_.load()) {
      if (!recvDatagram(fd_, datagram)) {
        return;
      }

      auto now = std::chrono::system_clock::now().time_since_epoch();
      PJ::Timestamp ts =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

      std::lock_guard lock(mu_);
      buffer_.push_back({ts, std::move(datagram)});
    }
  }

  struct BufferedMsg {
    PJ::Timestamp timestamp;
    std::vector<uint8_t> payload;
  };

  int fd_ = -1;
  std::string config_;
  std::string encoding_ = "json";
  PJ::Expected<PJ::sdk::ParserBindingHandle> binding_ =
      PJ::unexpected(std::string("unset"));

  std::atomic<bool> running_{false};
  std::thread recv_thread_;
  std::mutex mu_;
  std::vector<BufferedMsg> buffer_;  // guarded by mu_
};

PJ_DATA_SOURCE_PLUGIN(UdpReceiver,
    R"({"name":"UDP Receiver","version":"1.0.0",)"
    R"("description":"Receive datagrams on UDP 9870 with delegated parsing"})")
```

The delegated-ingest pattern separates transport from decoding: the source
handles I/O (socket lifecycle, polling, timestamps), while the parser handles
payload semantics (field extraction, schema binding). Changing the parser
encoding — from JSON to Protobuf, for example — requires no source code
changes, only a different config value.

**Threading and `onPoll()` semantics.** `onPoll()` is the host→plugin
callback for flushing accumulated data. The host calls it periodically from
its own thread. For sources with asynchronous I/O (sockets, hardware), the
plugin must manage its own receive thread and use `onPoll()` as the sync
point where buffered data is handed off. Host methods (`pushRawMessage`,
`appendRecord`, etc.) must only be called from `onPoll()` / the host's
thread, never from the background thread.

Key traits of `StreamSourceBase`:
- `capabilities()` automatically includes `kCapabilityContinuousStream`; you
  provide additional flags via `extraCapabilities()`.
- `onStart()` opens connections and creates topics or parser bindings.
- `onPoll()` flushes buffered data into the host — drain what your plugin
  has accumulated and return immediately. Must not block. Host methods
  (`pushRawMessage`, `appendRecord`, etc.) may only be called from this
  callback.
- `onStop()` tears down connections. Must be idempotent.
- `stop()`, `start()`, `poll()`, and `currentState()` are managed by the
  base class.
- **Pause/resume** are NOT wired by `StreamSourceBase`. To support pause,
  override `pause()`/`resume()` from `DataSourcePluginBase` directly and
  add `kCapabilitySupportsPause` to `extraCapabilities()`.

## Host Services Available to Plugins

Two host bindings are provided before `start()` is called:

### Write host — data plane

Access via `writeHost()`. Use this to write decoded data into the storage
engine.

| Method | Purpose |
|---|---|
| `ensureTopic(name)` | Create or look up a topic. Returns a handle. |
| `ensureField(topic, name, type)` | Optional: pre-register a field. Enables `appendBoundRecord`. |
| `appendRecord(topic, timestamp, fields)` | Write a row of named field values. Auto-creates new fields. |
| `appendBoundRecord(topic, timestamp, fields)` | Write using pre-resolved field handles (faster). |
| `appendArrowStream(topic, stream, ts_col)` | Hand an `ArrowArrayStream*` (Arrow C Data Interface) to the host for bulk ingest. Host drains and releases on success. |

### Runtime host — control plane

Access via `runtimeHost()`. Use this for lifecycle coordination and diagnostics.

| Method | Purpose |
|---|---|
| `reportMessage(level, text)` | Send info/warning/error to the host UI log. |
| `progressStart(label, total, cancellable)` | Begin a progress bar. |
| `progressUpdate(step)` | Advance progress. Returns false if cancelled. |
| `progressFinish()` | End the progress sequence. |
| `notifyState(state)` | Tell the host your state changed. |
| `requestStop(terminal_state, reason)` | Ask the host to stop you (self-terminate). |
| `isStopRequested()` | Check if the host wants you to stop. |
| `ensureParserBinding(request)` | Bind a parser for delegated ingest (see below). |
| `pushRawMessage(handle, timestamp, payload)` | Push raw bytes through a parser binding. |

## Optional Features

### Pause and resume

Override `pause()` and `resume()`, and declare `kCapabilitySupportsPause` in
your capabilities:

```cpp
uint64_t extraCapabilities() const override {
  return PJ::kCapabilityDirectIngest | PJ::kCapabilitySupportsPause;
}

PJ::Status pause() override {
  // Suspend your data source...
  return PJ::okStatus();
}

PJ::Status resume() override {
  // Resume your data source...
  return PJ::okStatus();
}
```

### Periodic polling

Override `onPoll()` (via `StreamSourceBase`) or `poll()` (via
`DataSourcePluginBase`) for streaming sources. The host calls it periodically
while the plugin is running. Return an error `Status` to signal failure.

### Configuration persistence

Override `saveConfig()` / `loadConfig()` to support layout save/restore:

```cpp
std::string saveConfig() const override { return my_config_json_; }
PJ::Status loadConfig(std::string_view json) override {
  my_config_json_ = std::string(json);
  return PJ::okStatus();
}
```

### Progress reporting

Report progress during long operations (e.g. file imports):

```cpp
runtimeHost().progressStart("Importing CSV", total_rows, /*cancellable=*/true);
for (uint64_t i = 0; i < total_rows; ++i) {
  if (!runtimeHost().progressUpdate(i)) {
    return PJ::okStatus();  // user cancelled — just return
  }
  // ... process row ...
}
```

> **Note:** `FileSourceBase` calls `progressFinish()` automatically after
> `importData()` returns. Do not call it yourself — just return from
> `importData()`. If you subclass `DataSourcePluginBase` directly, you must
> call `progressFinish()` manually.

### Delegated parsing

If your source is a transport or container (MQTT, ZMQ, MCAP) where the payload
encoding varies, use delegated ingest instead of writing decoded data directly.
Declare `kCapabilityDelegatedIngest` and use the runtime host:

```cpp
// 1. Bind a parser for a topic
auto binding = runtimeHost().ensureParserBinding({
    .topic_name = "sensor/imu",
    .parser_encoding = "protobuf",
    .type_name = "imu_sample",
    .schema = schema_bytes,
});
if (!binding) { return PJ::unexpected(binding.error()); }

// 2. Push raw payloads — the host parses and stores them
auto status = runtimeHost().pushRawMessage(*binding, timestamp_ns, payload);
```

The host manages parser instances, caches bindings, and handles schema
evolution automatically.

**Integrated parser dialog:** If your source dialog includes a `pj_parser_slot`
placeholder widget, the host detects it and renders the selected parser's
configuration dialog inline. The source and parser dialogs share one window but
persist config independently — `ConfigEnvelope.source_config` for the source,
`ConfigEnvelope.parser_binding` for the parser. See
`pj_plugins/docs/message-parser-guide.md` § "Dialog integration" for details.

## State Machine

```
idle --> configuring --> starting --> running --> stopping --> stopped
                                       |  ^
                                 pause |  | resume
                                       v  |
                                     paused

Any state --> failed
```

- **stopped** and **failed** are terminal — create a new instance to restart.
- Always call `runtimeHost().notifyState()` when you transition.
- Use `runtimeHost().requestStop(kStopped, reason)` to self-terminate.
- Check `runtimeHost().isStopRequested()` during long operations.

## Capability Flags Reference

| Flag | Value | When to use |
|---|---|---|
| `kCapabilityFiniteImport` | `1 << 0` | File importers that load all data at once |
| `kCapabilityContinuousStream` | `1 << 1` | Live streaming sources |
| `kCapabilityDirectIngest` | `1 << 2` | Plugin decodes data and writes via write host |
| `kCapabilityDelegatedIngest` | `1 << 3` | Plugin pushes raw bytes for host-side parsing |
| `kCapabilitySupportsPause` | `1 << 4` | pause()/resume() are implemented |
| `kCapabilityHasDialog` | `1 << 5` | Plugin provides a configuration dialog |

Combine with bitwise OR.

## Manifest Schema

The manifest is a JSON string literal embedded in the vtable. The host reads
it without instantiating the plugin.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `description` | string | no | Short description of the plugin. |
| `file_extensions` | string[] | no | File extensions this source handles, e.g. `[".csv", ".tsv"]`. Plugins declaring `kCapabilityFiniteImport` SHOULD include this so the host can build file-dialog filters. |

Example:
```json
{
  "name": "CSV Loader",
  "version": "1.0.0",
  "description": "Import numeric CSV files",
  "file_extensions": [".csv", ".tsv"]
}
```

## Write Patterns

### Named vs bound writes

For simple sources, use `appendRecord()` with named fields — fields are
auto-created on first non-null value, and names are resolved on each call:

```cpp
const PJ::sdk::NamedFieldValue fields[] = {
    {.name = "temperature", .value = 23.5},
    {.name = "humidity", .value = 61.0}};
writeHost().appendRecord(*topic, timestamp, fields);
```

For high-throughput sources, pre-register fields with `ensureField()` and use
`appendBoundRecord()` — field handles are resolved once at startup:

```cpp
// During start():
auto temp = writeHost().ensureField(*topic, "temperature",
                                     PJ::PrimitiveType::kFloat64);
auto hum = writeHost().ensureField(*topic, "humidity",
                                    PJ::PrimitiveType::kFloat64);

// During poll():
const PJ::sdk::BoundFieldValue fields[] = {
    {.field = *temp, .value = 23.5},
    {.field = *hum, .value = 61.0}};
writeHost().appendBoundRecord(*topic, timestamp, fields);
```

### Bulk Arrow writes

For sources that already hold data in Arrow columnar format (e.g. Parquet
file readers, Arrow Flight streams, MCAP-to-Arrow shims), use
`appendArrowStream()` to hand the host an `ArrowArrayStream*` (Arrow C
Data Interface). The host pulls batches via the stream's `get_next()`
callback and takes ownership on success — no row-at-a-time overhead.

The recommended overload takes an `ArrowStreamHolder` by rvalue
reference and disarms the holder on success, so the ownership-transfer
contract is unforgettable:

```cpp
#include <pj_base/sdk/arrow.hpp>

// Plugin builds the stream (e.g. via nanoarrow or arrow::RecordBatchReader).
PJ::sdk::ArrowStreamHolder stream(buildMyArrowStream());

// Hand it off. The host takes ownership on success, plugin retains
// on failure — either way, no manual release() call.
auto status = writeHost().appendArrowStream(*topic, std::move(stream), "timestamp");
if (!status) {
  return PJ::unexpected(status.error());
}
```

`timestamp_column` names an int64 column in the stream's schema whose
values are nanoseconds since Unix epoch. Pass an empty view to have the
host synthesise a monotonic timestamp per row.

If your data is already in an Arrow **IPC** byte buffer (file or
Flight wire format), wrap it with nanoarrow's
`ArrowIpcArrayStreamReaderInit` to obtain an `ArrowArrayStream*` and
feed that through `appendArrowStream()` — v4 no longer exposes a
separate IPC-bytes write slot.

A raw-pointer overload (`appendArrowStream(topic, ArrowArrayStream*,
...)`) is kept as an ABI escape hatch, but the rvalue-ref form above
is the documented default.

## Threading Model

All plugin callbacks — `start()`, `stop()`, `poll()`, `pause()`, `resume()`,
`loadConfig()`, `saveConfig()` — are called **on the host's thread**. The host
guarantees single-threaded access per plugin instance: no two callbacks will
overlap for the same instance.

Write host and runtime host methods (`appendRecord()`, `ensureParserBinding()`,
etc.) must be called from the same thread that invoked the callback. Do not
cache host views and call them from a background thread.

If your plugin needs internal threading (e.g. a network receive thread), you
must synchronize access to shared state yourself and only call host methods
from the callback thread.

## Lifecycle Invariants

The host guarantees the following call ordering:

1. `create()` — always first.
2. `bind_write_host()` and `bind_runtime_host()` — before `start()`.
3. `load_config()` — before `start()`, may be called multiple times.
4. `start()` — transitions from idle/configuring to starting.
5. `poll()` — only while running (after `start()` returns success).
6. `pause()`/`resume()` — only while running/paused.
7. `stop()` — may be called at any time after `start()`.
8. `destroy()` — always last.

The host will never call `poll()` before `start()`, nor `start()` after
`stop()`. Terminal states (`stopped`, `failed`) are final — the host creates
a new instance to restart.

## Error Handling

All fallible SDK methods return `PJ::Status` (`Expected<void>`). Return
`okStatus()` on success, `unexpected("reason")` on failure.

### Patterns

**Check-and-propagate** — the standard pattern for host calls:

```cpp
auto topic = writeHost().ensureTopic("data");
if (!topic) {
  return PJ::unexpected(topic.error());
}
```

**Mid-batch errors** — if `appendRecord()` fails during a loop, decide based
on severity:
- **Skip and continue**: log via `runtimeHost().reportMessage()` and proceed
  to the next row. Appropriate for data-quality issues (malformed row).
- **Abort**: propagate the error upward. Appropriate for I/O or resource
  failures.

```cpp
for (const auto& row : rows) {
  auto status = writeHost().appendRecord(*topic, row.timestamp, row.fields);
  if (!status) {
    // Recoverable: skip this row
    runtimeHost().reportMessage(
        PJ::DataSourceMessageLevel::kWarning,
        "skipped row: " + status.error());
    continue;
    // Fatal alternative: return PJ::unexpected(status.error());
  }
}
```

**setLastError()** — available for `void` methods (e.g. `stop()`) that cannot
return a status. For all other methods, prefer returning `unexpected()`.

**Exception safety** — the SDK base class catches all C++ exceptions in virtual
method trampolines and converts them to `setLastError()` + false return. You
never need to worry about exceptions crossing the C ABI boundary.

## Dialog Integration

A DataSource can provide a configuration dialog by declaring
`kCapabilityHasDialog` and exporting a dialog vtable from the same `.so`.
The dialog is owned by the DataSource as a member — they share state directly,
with no JSON serialization needed at runtime.

### Architecture

```
     Plugin .so
┌──────────────────────────────────────┐
│  class MyDialog                      │  ← PJ::DialogPluginTyped
│    (UI logic, event handlers)        │
│                                      │
│  class MySource                      │  ← PJ::StreamSourceBase
│    MyDialog dialog_;  ← member       │
│    (business logic)                  │
│    getDialog() → borrowDialog(...)   │
│                                      │
│  PJ_DATA_SOURCE_PLUGIN(MySource)     │  → exports DataSource vtable
│  PJ_DIALOG_PLUGIN(MyDialog)          │  → exports Dialog vtable
└──────────────────────────────────────┘
```

One `.so`, two vtables, one DataSource instance. The dialog instance is a
member of the DataSource — not independently created. The host obtains a
**borrowed** reference to the dialog through the DataSource vtable.

### Implementation steps

**1. Define the dialog class** with read-only accessors for the source:

```cpp
class MyDialog : public PJ::DialogPluginTyped {
 public:
  const std::string& host() const { return host_; }
  int port() const { return port_; }

  // ... manifest(), ui_content(), widget_data(), event handlers,
  //     saveConfig(), loadConfig() — same as any standalone dialog.

 private:
  std::string host_ = "localhost";
  int port_ = 9090;
};
```

**2. Define the source class** owning the dialog as a member:

```cpp
class MySource : public PJ::StreamSourceBase {
 public:
  PJ_borrowed_dialog_t getDialog() override {
    return PJ::borrowDialog(dialog_);
  }

  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest | PJ::kCapabilityHasDialog;
  }

  PJ::Status onStart() override {
    // Read dialog state directly — no JSON parsing needed.
    auto topic = writeHost().ensureTopic("data/" + dialog_.host());
    if (!topic) return PJ::unexpected(topic.error());
    // ...
    return PJ::okStatus();
  }

  std::string saveConfig() const override { return dialog_.saveConfig(); }
  PJ::Status loadConfig(std::string_view json) override {
    return dialog_.loadConfig(json) ? PJ::okStatus()
                                    : PJ::unexpected(std::string("bad config"));
  }

 private:
  MyDialog dialog_;
};
```

**3. Export both vtables** at file scope:

```cpp
PJ_DATA_SOURCE_PLUGIN(MySource, R"({"name":"My Source","version":"1.0.0"})")
PJ_DIALOG_PLUGIN(MyDialog)
```

### Host-side flow

```
1. DataSourceLibrary::load("plugin.so")
2. lib.createHandle()  →  DataSourceHandle
3. source.capabilities() & kCapabilityHasDialog?
4. lib.resolveDialogVtable()  →  dialog vtable from same .so
5. source.getDialog()  →  typed PJ_borrowed_dialog_t {ctx, vtable}
6. DialogHandle::borrowed(dialog_vt, dialog_ctx)  →  non-owning handle
7. DialogEngine(borrowed_handle).showDialog()
   → dialog modifies source's internal state directly
8. source.start()  ←  already has the config
```

### Config persistence

`source.saveConfig()` serializes everything (dialog + source state).
`loadConfig()` restores it. Headless restart: `loadConfig(saved) → start()` —
no dialog needed.

The host wraps the source config in a versioned envelope (`ConfigEnvelope`)
that also holds host-owned parser binding state:

```json
{"version": 1, "source_config": "...", "parser_binding": "..."}
```

The source never sees `parser_binding` — the host manages it.

A complete example lives at `pj_plugins/examples/mock_source_with_dialog.cpp`.
See `pj_plugins/docs/dialog-plugin-guide.md` for the dialog protocol itself.

## Examples

- **Finite import** and **continuous stream** patterns are documented above
  in the "Common Patterns" section using `FileSourceBase` and `StreamSourceBase`.
- `pj_plugins/examples/mock_data_source.cpp` is a comprehensive test fixture
  that exercises the full `DataSourcePluginBase` API surface: capabilities,
  direct ingest, delegated ingest, progress reporting, pause/resume, and
  config persistence.
- `pj_plugins/examples/mock_source_with_dialog.cpp` demonstrates the
  DataSource-owned dialog pattern: a combined `.so` with two vtables, shared
  state via member ownership, and dialog read-only accessors.
