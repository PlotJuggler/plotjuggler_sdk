# Writing a Toolbox Plugin

> **Tracks the v5 plugin ABI** (`PJ_ABI_VERSION == 5`). Toolbox plugins
> read time series via the host's `read_series_arrow` slot, which
> returns a caller-owned `ArrowSchema` + `ArrowArray` pair (no more
> materialised `std::vector`). Wrap returns in
> `PJ::sdk::ArrowSchemaHolder` / `ArrowArrayHolder` for scope-bound
> release. See `ARCHITECTURE.md` for the full ABI rules.

> **Vocabulary used throughout this guide**:
> - `PJ::Status` — alias for `PJ::Expected<void>`. Return `PJ::okStatus()` or
>   `PJ::unexpected("reason")`.
> - `PJ::sdk::ArrowSchemaHolder` / `ArrowArrayHolder` / `ArrowStreamHolder` —
>   RAII wrappers from `pj_base/sdk/arrow.hpp` that release Arrow C Data
>   Interface structs at scope exit.
> - "Catalog snapshot" — a read-only view of every data source, topic, and
>   field present in the host at the moment of acquisition.

> **Toolbox is the most powerful family.** It alone can read existing data,
> create new data sources, and write derived outputs. Treat that power with
> care — see [Plugin Contract](#plugin-contract) below for the rules and
> conventions the host expects you to follow.

## What is a Toolbox?

A Toolbox plugin is a shared library (`.so` / `.dylib` / `.dll`) that provides
**stateful interactive tools** with full read+write access to host data. Unlike
DataSource (write-only, streaming lifecycle) and MessageParser (headless,
request/response), Toolbox plugins are long-lived, UI-driven, and may create
new data sources or transform existing data into new outputs.
Plugins link only against `pj_base` (no Qt, no host internals) and communicate
through a stable C ABI.

Typical Toolbox use cases: FFT analysis, quaternion rotation, Lua scripting
editor, custom data transforms.

## Quick Start

1. Subclass `PJ::ToolboxPluginBase`
2. Override `capabilities()` (required) and optionally `bind()` (for
   acquiring services), `saveConfig()`, `loadConfig()`, `getDialog()`
3. Export with `PJ_TOOLBOX_PLUGIN(YourClass, R"({"id":"...","name":"...","version":"..."})")`
4. If you ship an embedded dialog, also declare it as a
   `DialogPluginTyped` subclass and add `PJ_DIALOG_PLUGIN(YourDialog, kManifestJson)`
5. Build as a shared library linking `pj_base` (+ `pj_dialog_sdk` if
   you have a dialog)

A complete example lives at `pj_plugins/examples/mock_toolbox.cpp`.

## Plugin Contract

Follow these rules. The toolbox family has read+write+create permissions, so
the contract is broader than the other families.

**MUST**
- Return `PJ::okStatus()` / `PJ::unexpected("reason")` from every fallible
  method.
- Call `runtimeHost().notifyDataChanged()` after any successful write that the
  user should see in the UI. Coalesce per logical operation, not per record —
  one call per "I just produced a new series" is the right granularity.
- Wrap all `read_series_arrow` returns in `PJ::sdk::ArrowSchemaHolder` /
  `ArrowArrayHolder` so the release callbacks fire on scope exit.
- Persist tool state in `saveConfig()` so a layout reload restores the same
  view. The host has no ambient persistence.
- Only call host methods from the host's callback thread. Background work
  must marshal back through the host thread to write data.

**MUST NOT**
- Throw exceptions across virtual overrides.
- Hold an `ArrowSchema*` / `ArrowArray*` past the scope of the holders that
  own them — the host may reuse the underlying buffers.
- Treat `catalogSnapshot()` as live data. Snapshots are immutable views at
  acquisition time; reacquire after writes if you need to see your own
  changes.
- Create ambiguous output sources whose names collide with existing user data
  unless the user explicitly chose that name. Duplicate names create distinct
  datasets, but they are confusing in the UI; check the catalog and pick a
  unique derived-data name by default.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/toolbox_plugin_base.hpp>
#include <pj_plugins/sdk/dialog_plugin_base.hpp>  // only if you have a dialog

class MyToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kToolboxCapabilityHasDialog;
  }

  // Hand the host a typed borrowed reference to the embedded dialog.
  // PJ::borrowDialog picks up the matching vtable automatically —
  // no extern "C" forward declaration needed in your source.
  PJ_borrowed_dialog_t getDialog() override {
    return PJ::borrowDialog(dialog_);
  }

  PJ::Status loadConfig(std::string_view json) override;
  std::string saveConfig() const override;

 private:
  MyDialog dialog_;
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
    R"({"id":"my-toolbox","name":"My Toolbox","version":"1.0.0",)"
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
create -> bind(registry) -> load_config
  -> [show dialog] -> user interacts -> plugin reads/writes via toolbox host
  -> plugin calls notifyDataChanged()
  -> save_config -> destroy
```

The host guarantees the following call ordering:

1. `create()` — always first.
2. `bind(registry)` — before any interaction. The SDK default bind resolves
   `"pj.toolbox_write.v1"` and `"pj.toolbox_runtime.v1"`.
3. `load_config()` — before showing the dialog, may be called multiple times.
4. User interaction phase — plugin reads/writes data on demand.
5. `save_config()` — before destroy, when the host persists layout.
6. `destroy()` — always last.

## Host Services Available to Plugins

Two host services are provided before the plugin becomes interactive:

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
| `appendArrowStream(topic, stream, ts_col)` | Hand an `ArrowArrayStream*` (Arrow C Data Interface) to the host for bulk ingest. Same ownership rule as the source write path: success transfers, failure retains. |
| `catalogSnapshot()` | Acquire a read-only snapshot of all data sources, topics, and fields. |
| `readSeriesArrow(field, schema*, array*)` | Read one field's full time series into host-owned `ArrowSchema` + `ArrowArray` out-params (two columns: `timestamp` int64 ns, then the typed field value). |
| `registerObjectTopic(source, name, metadata_json)` | Register a media/object topic under a data source. `metadata_json` is opaque to the store and retained verbatim; viewers and parsers read it to pick a renderer. Returns an `ObjectTopicHandle`. |
| `pushOwnedObject(topic, ts, payload)` | Eager-push serialized object bytes into the ObjectStore under an object topic; the host copies the bytes, so the plugin's buffer is free immediately after the call returns. |

### Runtime host — control plane

Access via `runtimeHost()`. Use this for diagnostics and UI refresh.

| Method | Purpose |
|---|---|
| `reportMessage(level, text)` | Send info/warning/error to the host UI log. |
| `notifyDataChanged()` | Tell the host that data was modified; refresh UI. Idempotent and cheap; coalesce per logical operation, not per record. |

### Reading a series via Arrow

`readSeriesArrow()` is the only read path in v4 — it returns
`ArrowSchema` + `ArrowArray` out-params populated by the host.
Wrap the out-params in the RAII holders from `pj_base/sdk/arrow.hpp`
so they are released automatically at scope exit:

```cpp
#include <pj_base/sdk/arrow.hpp>

void MyToolbox::runFft(PJ::sdk::FieldHandle field) {
  PJ::sdk::ArrowSchemaHolder schema;
  PJ::sdk::ArrowArrayHolder  array;

  auto status = toolboxHost().readSeriesArrow(field, schema.out(), array.out());
  if (!status) {
    runtimeHost().reportMessage(PJ::ToolboxMessageLevel::kError,
                                "readSeriesArrow failed: " + status.error());
    return;
  }

  // array.get() now points to a two-column Arrow struct:
  //   column 0: "timestamp"   — int64 nanoseconds since Unix epoch
  //   column 1: <field name>  — typed to the field's primitive type
  // Walk children[0]->buffers / children[1]->buffers per Arrow spec,
  // or hand array.get() directly to analytics code that speaks Arrow
  // (DuckDB, Polars, pandas via PyCapsule, …).
}
// schema and array are released here by their destructors.
```

**Bulk-write output:** pair `readSeriesArrow` with `appendArrowStream`
to round-trip data through a transform. Use the rvalue-ref overload:

```cpp
PJ::sdk::ArrowStreamHolder stream(buildOutputStream());
auto status = toolboxHost().appendArrowStream(
    out_topic, std::move(stream), "timestamp");
// Success: stream is inert. Failure: destructor releases it. No manual
// release() dance required.
```

### Writing object payloads (images, point clouds, annotations)

`readSeriesArrow` / `appendArrowStream` cover *scalar* columns. To emit
**canonical media** — an image a toolbox renders, a point cloud, an annotation
overlay — use the object-write surface, which routes to the host `ObjectStore`
rather than the columnar engine:

1. `registerObjectTopic(source, name, metadata_json)` declares a topic under a
   data source you created. The `metadata_json` is opaque to the store; viewers
   and parsers read it to choose a renderer (e.g. `{"object_type":"image"}` for
   the MediaViewer). It returns an `ObjectTopicHandle`.
2. `pushOwnedObject(topic, ts, payload)` pushes serialized bytes (e.g. a
   `PJ.Image` produced via `serializeImage()` from `pj_base/builtin/image_codec.hpp`).
   The push is **eager**: the host copies the bytes immediately, so your buffer
   may be reused or freed the moment the call returns. There is no lazy/fetch
   variant on the toolbox surface — a toolbox already holds the bytes by the
   time it writes them.

```cpp
auto topic = toolboxHost().registerObjectTopic(
    source, "mosaic/preview", R"({"object_type":"image"})");
if (!topic) {
  runtimeHost().reportMessage(PJ::ToolboxMessageLevel::kError, topic.error());
  return;
}

std::vector<uint8_t> bytes = PJ::serializeImage(my_image);
auto status = toolboxHost().pushOwnedObject(
    *topic, timestamp_ns, PJ::Span<const uint8_t>(bytes.data(), bytes.size()));
if (!status) {
  runtimeHost().reportMessage(PJ::ToolboxMessageLevel::kError, status.error());
}
```

> **Older-host compatibility:** these two methods are tail slots appended to the
> toolbox host vtable. Against a host built before they existed, both return
> `unexpected("…older host")` instead of crashing — the SDK gates each call on
> the host's `struct_size`. Check the returned `Expected`/`Status` and degrade
> gracefully if you must support pre-object-write hosts.

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
| `id` | string | yes | Stable plugin identifier used by the host catalog. Must be unique per plugin. |
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `description` | string | no | Short description of the plugin. |

Example:
```json
{
  "id": "fft-toolbox",
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
method trampolines and converts them to `PJ_error_t` out-params plus `false`.
No exceptions cross the C ABI boundary.

## Threading Model

All plugin callbacks — `bind()`,
`loadConfig()`, `saveConfig()`, `getDialog()` — are called **on the host's
thread**. The host guarantees single-threaded access per plugin instance.

Toolbox host and runtime host methods must be called from the same thread that
invoked the callback. If your plugin uses internal threading, synchronize
access and only call host methods from the host's thread.

## Testing

Use `PJ::testing::ToolboxTestStore` from
`pj_plugins/include/pj_plugins/testing/toolbox_test_store.hpp` to write
unit tests without hand-rolling an Arrow C Data Interface mock:

```cpp
#include <pj_plugins/testing/toolbox_test_store.hpp>

TEST(MyToolboxTest, Basic) {
  auto library = PJ::ToolboxLibrary::load(PJ_MY_TOOLBOX_PLUGIN_PATH);
  auto handle = library->createHandle();

  PJ::testing::ToolboxTestStore store;
  store.addTopic("input")
       .addField("input", "x", timestamps, values);

  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::ToolboxHostService>(store.makeHost());
  registry.registerService<PJ::sdk::ToolboxRuntimeHostService>(store.makeRuntimeHost());
  ASSERT_TRUE(handle.bind(registry.view()));

  ASSERT_TRUE(handle.loadConfig(R"({...})"));

  EXPECT_EQ(store.notifyDataChangedCalls(), 1);
  EXPECT_DOUBLE_EQ(store.flatRecords()[0].numeric, expected);
}
```

The store captures `appendRecord` writes and counts `createDataSource`
+ `notifyDataChanged` invocations. `flatRecords()` gives a flat
(timestamp, name, value) view; `writtenRecords()` preserves the nested
row-of-fields shape. See
`pj_plugins/testing/toolbox_test_store.hpp` for the full API.

## Examples

- `pj_plugins/examples/mock_toolbox.cpp` — minimal test fixture that exercises
  the full `ToolboxPluginBase` API surface: capabilities, config persistence,
  host binding, and dialog context.
- `pj_plugins/tests/toolbox_plugin_test.cpp` — end-to-end host-side test
  using `PJ::testing::ToolboxTestStore` (in `pj_plugins/include/pj_plugins/testing/`)
  to drive a toolbox plugin through ingest, transform, and config scenarios.

## Common Mistakes

| Symptom | Cause | Fix |
|---|---|---|
| New series do not appear in the host UI after a write | `notifyDataChanged()` was never called | Call it once after each logical write batch |
| `read_series_arrow` succeeds but later code crashes accessing the data | `ArrowSchema` / `ArrowArray` released early, or held by raw pointer past holder scope | Use `PJ::sdk::ArrowSchemaHolder` / `ArrowArrayHolder` and keep them alive while the data is read |
| Catalog reads stale data immediately after writing | `catalogSnapshot()` was acquired before the write | Reacquire the snapshot after `notifyDataChanged()` |
| Duplicate source names appear in the UI | `createDataSource(name)` always creates a new dataset, even when the display name already exists | Check the catalog first; pick a unique derived-data name or surface a confirmation in the dialog |
| Plugin works in tests but crashes in the host | Host method called from a thread the toolbox spawned | Marshal back to the host thread (use the dialog's `onTick` or a host-thread queue) |
| Bulk transform output is one row at a time | Output written record-by-record instead of via Arrow | Build an `ArrowArrayStream` and use `appendArrowStream()` for the output |
| Plugin restarts but the tool's view is empty | Config not persisted | Round-trip every UI-relevant field through `saveConfig()` / `loadConfig()` |
