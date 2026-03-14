# Writing a Toolbox Plugin

## What is a Toolbox?

A Toolbox plugin is a shared library (`.so` / `.dylib` / `.dll`) that provides
**stateful interactive tools** with full read+write access to host data. Unlike
DataSource (write-only, streaming lifecycle) and MessageParser (headless,
request/response), Toolbox plugins are long-lived, UI-driven, and may create
new data sources, transform existing data, or perform destructive updates.
Plugins link only against `pj_base` (no Qt, no host internals) and communicate
through a stable C ABI.

Typical Toolbox use cases: FFT analysis, quaternion rotation, Lua scripting
editor, custom data transforms.

## Quick Start

1. Subclass `PJ::ToolboxPluginBase`
2. Override `capabilities()` (required) and optionally `bindToolboxHost()`,
   `bindRuntimeHost()`, `saveConfig()`, `loadConfig()`, `dialogContext()`
3. Export with `PJ_TOOLBOX_PLUGIN(YourClass, R"({"name":"...","version":"..."})")`
4. Build as a shared library linking `pj_base`

A complete example lives at `pj_plugins/examples/mock_toolbox.cpp`.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/toolbox_plugin_base.hpp>

class MyToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kToolboxCapabilityHasDialog;
  }

  void* dialogContext() override { return this; }

  PJ::Status loadConfig(std::string_view json) override;
  std::string saveConfig() const override;
};
```

### 2. Implement data operations

When the user interacts with your tool's dialog, use the toolbox host to
read and write data. Both host bindings are available via `toolboxHost()`
and `runtimeHost()`.

```cpp
void MyToolbox::applyTransform() {
  auto host = toolboxHost();

  // Create a new data source for the output
  auto source = host.createDataSource("fft_output");

  // Create a topic within the source
  auto topic = host.ensureTopic(*source, "spectrum");

  // Read existing data from the catalog
  auto catalog = host.catalogSnapshot();

  // Write transformed data
  const PJ::sdk::NamedFieldValue fields[] = {
      {.name = "frequency", .value = freq},
      {.name = "magnitude", .value = mag}};
  host.appendRecord(*topic, timestamp, fields);

  // Tell the host to refresh the UI
  runtimeHost().notifyDataChanged();
}
```

### 3. Export the plugin

At file scope, after the class definition:

```cpp
PJ_TOOLBOX_PLUGIN(MyToolbox,
    R"({"name":"My Toolbox","version":"1.0.0",)"
    R"("description":"Apply FFT to selected signals"})")
```

### 4. Build

```cmake
add_library(my_toolbox_plugin SHARED my_toolbox.cpp)
target_link_libraries(my_toolbox_plugin PRIVATE pj_base)
```

No other dependencies are needed.

## Lifecycle

Toolbox plugins have no state machine — they are either alive or destroyed.
Activation and deactivation are dialog visibility concerns handled by the host.

```
create → bind_toolbox_host → bind_runtime_host → load_config
  → [show dialog] → user interacts → plugin reads/writes via toolbox host
  → plugin calls notifyDataChanged()
  → save_config → destroy
```

The host guarantees the following call ordering:

1. `create()` — always first.
2. `bind_toolbox_host()` and `bind_runtime_host()` — before any interaction.
3. `load_config()` — before showing the dialog, may be called multiple times.
4. User interaction phase — plugin reads/writes data on demand.
5. `save_config()` — before destroy, when the host persists layout.
6. `destroy()` — always last.

## Host Services Available to Plugins

Two host bindings are provided before the plugin becomes interactive:

### Toolbox host — data plane

Access via `toolboxHost()`. This provides full read+write access to the host's
data store.

| Method | Purpose |
|---|---|
| `createDataSource(name)` | Create a new data source. Returns a handle. |
| `ensureTopic(source, topic_name)` | Create or look up a topic within a source. |
| `ensureField(topic, name, type)` | Optional: pre-register a field. Enables `appendBoundRecord`. |
| `appendRecord(topic, timestamp, fields)` | Write a row of named field values. Auto-creates new fields. |
| `appendBoundRecord(topic, timestamp, fields)` | Write using pre-resolved field handles (faster). |
| `appendArrowIpc(topic, ipc_stream, ts_col)` | Write an Arrow IPC stream directly (bulk columnar). |
| `catalogSnapshot()` | Acquire a read-only snapshot of all data sources, topics, and fields. |
| `readSeries(field)` | Read the full time series for a field. |

### Runtime host — control plane

Access via `runtimeHost()`. Use this for diagnostics and UI refresh.

| Method | Purpose |
|---|---|
| `reportMessage(level, text)` | Send info/warning/error to the host UI log. |
| `notifyDataChanged()` | Tell the host that data was modified; refresh UI. |
| `lastError()` | Read the last host-side error message. |

## Configuration Persistence

Override `saveConfig()` / `loadConfig()` to support layout save/restore:

```cpp
std::string saveConfig() const override { return config_json_; }

PJ::Status loadConfig(std::string_view json) override {
  config_json_ = std::string(json);
  // Parse and apply settings...
  return PJ::okStatus();
}
```

## Capability Flags Reference

| Flag | Value | When to use |
|---|---|---|
| `kToolboxCapabilityHasDialog` | `1 << 0` | Plugin provides a persistent UI panel |

## Manifest Schema

The manifest is a JSON string literal embedded in the vtable. The host reads
it without instantiating the plugin.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `description` | string | no | Short description of the plugin. |

Example:
```json
{
  "name": "FFT Toolbox",
  "version": "1.0.0",
  "description": "Apply FFT transforms to selected signals"
}
```

## Error Handling

All fallible host methods return `PJ::Status` or `PJ::Expected<T>`. Use the
check-and-propagate pattern:

```cpp
auto source = toolboxHost().createDataSource("output");
if (!source) {
  runtimeHost().reportMessage(
      PJ::ToolboxMessageLevel::kError,
      "failed to create source: " + source.error());
  return;
}
```

**Exception safety** — the SDK base class catches all C++ exceptions in virtual
method trampolines and converts them to `setLastError()` + false return. No
exceptions cross the C ABI boundary.

## Threading Model

All plugin callbacks — `bindToolboxHost()`, `bindRuntimeHost()`,
`loadConfig()`, `saveConfig()`, `dialogContext()` — are called **on the host's
thread**. The host guarantees single-threaded access per plugin instance.

Toolbox host and runtime host methods must be called from the same thread that
invoked the callback. If your plugin uses internal threading, synchronize
access and only call host methods from the host's thread.

## Examples

- `pj_plugins/examples/mock_toolbox.cpp` — minimal test fixture that exercises
  the full `ToolboxPluginBase` API surface: capabilities, config persistence,
  host binding, and dialog context.
