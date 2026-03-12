# Writing a DataSource Plugin

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
      {.name = "temperature", .is_null = false,
       .value = PJ::sdk::ValueRef{double(23.5)}}};
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

No other dependencies are needed.

## Common Patterns

DataSource plugins fall into two families. A **finite importer** reads a file
or snapshot, writes all records, then self-terminates. A **continuous streamer**
connects to a live source, does incremental work in `onPoll()`, and runs until
the host calls `stop()`.

The SDK provides two derived base classes that manage the lifecycle state
machine for you: `FileSourceBase` and `StreamSourceBase`. Both live in
`<pj_base/sdk/data_source_patterns.hpp>`. For full manual control, subclass
`DataSourcePluginBase` directly (see `pj_plugins/examples/mock_data_source.cpp`).

### Finite importer — CSV file loader

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

  PJ::Status importData() override {
    // Open the file at path_.
    // std::ifstream file(path_);
    // if (!file) return PJ::unexpected("cannot open " + path_);

    auto topic = writeHost().ensureTopic("csv");
    if (!topic) return PJ::unexpected(topic.error());

    // Count rows for progress, then iterate.
    uint64_t total_rows = 100;  // placeholder
    runtimeHost().progressStart("Importing CSV", total_rows, true);

    for (uint64_t row = 0; row < total_rows; ++row) {
      if (!runtimeHost().progressUpdate(row)) {
        return PJ::okStatus();  // clean cancellation
      }

      // Parse each row and write it.
      double value = 0;  // = parse_row(...)
      const PJ::sdk::NamedFieldValue fields[] = {
          {.name = "value", .is_null = false,
           .value = PJ::sdk::ValueRef{value}}};
      auto status = writeHost().appendRecord(
          *topic, PJ::Timestamp{static_cast<int64_t>(row)},
          PJ::Span<const PJ::sdk::NamedFieldValue>(fields, 1));
      if (!status) {
        return PJ::unexpected(status.error());
      }
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

### Continuous streamer — UDP receiver

Subclass `StreamSourceBase` and implement `onStart()`, `onPoll()`, and
`onStop()`. The base class manages the state machine.

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>

// Socket headers — platform-specific.
// #include <arpa/inet.h>
// #include <sys/socket.h>

class UdpReceiver : public PJ::StreamSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }

  PJ::Status onStart() override {
    // Open a non-blocking UDP socket.
    // fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    // if (fd_ < 0) return PJ::unexpected("socket() failed");
    // bind(fd_, ...);

    topic_ = writeHost().ensureTopic("udp/data");
    if (!topic_) return PJ::unexpected(topic_.error());

    runtimeHost().reportMessage(
        PJ::DataSourceMessageLevel::kInfo, "listening on :9870");
    return PJ::okStatus();
  }

  PJ::Status onPoll() override {
    // Drain all available datagrams (must not block).
    // while (auto n = recv(fd_, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
    //   double value = parse(buf, n);
    //   ... appendRecord ...
    // }
    return PJ::okStatus();
  }

  void onStop() override {
    // Close the socket. Must be idempotent.
    // if (fd_ >= 0) { close(fd_); fd_ = -1; }
  }

 private:
  int fd_ = -1;
  int64_t seq_ = 0;
  PJ::Expected<PJ::sdk::TopicHandle> topic_ =
      PJ::unexpected(std::string("unset"));
};

PJ_DATA_SOURCE_PLUGIN(UdpReceiver,
    R"({"name":"UDP Receiver","version":"1.0.0",)"
    R"("description":"Receive numeric datagrams on UDP 9870"})")
```

Key traits of `StreamSourceBase`:
- `capabilities()` automatically includes `kCapabilityContinuousStream`; you
  provide additional flags via `extraCapabilities()`.
- `onStart()` opens connections and creates topics.
- `onPoll()` does incremental work — drain what is available and return
  immediately. Must not block.
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
| `ensureField(topic, name, type)` | Pre-register a field for fast writes. |
| `appendRecord(topic, timestamp, fields)` | Write a row of named field values. |
| `appendRecordFast(topic, timestamp, fields)` | Write using pre-resolved field handles. |

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

## Error Handling

- All fallible SDK virtuals (`start`, `loadConfig`, `pause`, `resume`, `poll`,
  `bindWriteHost`, `bindRuntimeHost`) return `PJ::Status`. Return
  `okStatus()` on success, `unexpected("reason")` on failure.
- `setLastError(msg)` is still available for `void` methods (e.g. `stop()`).
- The base class catches all exceptions thrown from virtual methods and stores
  them automatically — you never need to worry about exceptions crossing the
  C ABI boundary.
- Check host operations: `writeHost().appendRecord()` and
  `runtimeHost().ensureParserBinding()` return `Expected<T>` / `Status` —
  always check before proceeding.

## Dialog Integration

A DataSource can provide a configuration dialog by declaring
`kCapabilityHasDialog` and exporting a dialog vtable from the same `.so`.
The dialog is owned by the DataSource as a member — they share state directly,
with no JSON serialization needed at runtime.

### Architecture

```
     Plugin .so
┌──────────────────────────────────┐
│  class MyDialog                  │  ← PJ::DialogPluginTyped
│    (UI logic, event handlers)    │
│                                  │
│  class MySource                  │  ← PJ::StreamSourceBase
│    MyDialog dialog_;  ← member   │
│    (business logic)              │
│    dialogContext() → &dialog_    │
│                                  │
│  PJ_DATA_SOURCE_PLUGIN(MySource) │  → exports DataSource vtable
│  PJ_DIALOG_PLUGIN(MyDialog)      │  → exports Dialog vtable
└──────────────────────────────────┘
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
  void* dialogContext() override { return &dialog_; }

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
5. source.dialogContext()  →  borrowed pointer to source's internal dialog
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
See `pj_plugins/dialog_protocol/docs/dialog-plugin-guide.md` for the dialog
protocol itself.

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
