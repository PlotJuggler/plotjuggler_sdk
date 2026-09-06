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
> create data sources and write derived outputs. Follow the rules and
> conventions in [Plugin Contract](#plugin-contract).

## What is a Toolbox?

A Toolbox plugin is a shared library (`.so` / `.dylib` / `.dll`) with full
read+write access to host data. It provides **stateful interactive tools**.
Toolbox plugins are long-lived and UI-driven. They may create data sources or
transform existing data into new outputs. DataSource is write-only with a
streaming lifecycle; MessageParser is headless with request/response calls.

Link only `pj_base`, without Qt or host internals. Plugins communicate through
a stable C ABI.

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

For namespaced classes in a static build, provide each getter-symbol token
separately: `PJ_TOOLBOX_PLUGIN_NAMED(my::Toolbox, MyToolbox, kManifest)` and
`PJ_DIALOG_PLUGIN_NAMED(my::Dialog, MyDialog, kManifest)`. Dynamic builds may
use either form.

A complete example lives at `pj_plugins/examples/mock_toolbox.cpp`.

## Plugin Contract

Follow these rules. The toolbox family has read+write+create permissions, so
the contract is broader than the other families.

**MUST**
- Return `PJ::okStatus()` / `PJ::unexpected("reason")` from every fallible
  method.
- Call `runtimeHost().notifyDataChanged()` after any successful write that the
  user should see in the UI. Coalesce per logical operation, not per record.
  For example, call once after producing a new series.
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

Use `toolboxHost()` to read and write data when the user interacts with your
dialog. Access the runtime host through `runtimeHost()`.

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

Toolbox plugins are either alive or destroyed; they have no state machine.
The host handles activation and deactivation through dialog visibility.

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

The data and runtime hosts are bound before the plugin becomes interactive.
Additional optional services are acquired through `services().get<Service>()`.

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
| `registerObjectTopic(source, name, type[, extra_metadata])` | Register a built-in media/object topic under a data source. The typed overload emits the canonical `builtin_object_type` renderer metadata and returns an `ObjectTopicHandle`. |
| `registerObjectTopic(source, name, metadata_json)` | Raw-metadata overload for custom or untyped object topics. The store retains the JSON verbatim. |
| `pushOwnedObject(topic, ts, payload)` | Eager-push serialized object bytes into the ObjectStore under an object topic; the host copies the bytes, so the plugin's buffer is free immediately after the call returns. |

### Runtime host — control plane

Access via `runtimeHost()`. Use this for diagnostics and UI refresh.

| Method | Purpose |
|---|---|
| `reportMessage(level, text)` | Send info/warning/error to the host UI log. |
| `notifyDataChanged()` | Tell the host that data was modified; refresh UI. Idempotent and cheap; coalesce per logical operation, not per record. |

### Playback, viewport, and owned tabs (SDK 0.28.0)

Include `pj_base/sdk/service_traits.hpp` and acquire the services you need:

| Service trait | Methods and scope |
|---|---|
| `PJ::sdk::PlaybackHostService` | `play`, `pause`, `seek`, `setPlaybackRate`, `state`: the global playback cursor. `toDisplayTime` and `toDisplayTimeForSource` convert absolute nanoseconds to display-axis seconds. |
| `PJ::sdk::PlotTabHostService` | `create`, `close`, `list`, `configOf`, `addCurve`, `removeCurve`, `clear`: only the calling plugin's tabs. |
| `PJ::sdk::ViewportHostService` | `zoomToTimeRange`, `zoomReset`: all eligible plots in the calling plugin's tabs. |

All calls run on the main thread. Services are optional. Check acquisition
and each operation's result. A host offering viewport control also offers
owned tabs.

Zoom preserves each plot's Y range and skips empty/XY plots. It fails if no plot
is eligible. Playback and viewport coordinates use **display-axis seconds**.
Read/write timestamps use **absolute int64 nanoseconds**.

Choose the source from a fresh `toolboxHost().catalogSnapshot()`.
Each `dataSources()` entry has a `handle`. Each `topics()` entry has its owning
`source` handle. Retain that identity with the selected series. Do not look it
up again by source or topic name; names may be duplicated.
For example, in a toolbox callback:

```cpp
#include <pj_base/sdk/service_traits.hpp>

PJ::Status MyToolbox::seekSample(PJ::sdk::DataSourceHandle source, int64_t absolute_ns) {
  auto playback = services().require<PJ::sdk::PlaybackHostService>();
  if (!playback) {
    return PJ::unexpected(playback.error());
  }
  auto seconds = playback->toDisplayTimeForSource(source, absolute_ns);
  if (!seconds) {
    return PJ::unexpected(seconds.error());
  }
  return playback->seek(*seconds);
}
```

`toDisplayTimeForSource` rejects invalid/unloaded handles. Its optional C tail
slot, `to_display_time_for_source`, is guarded by `struct_size` and nullability;
the C++ wrapper returns an unsupported error on older hosts. Do not silently
fall back to another dataset. The existing `toDisplayTime(topic, absolute_ns)`
requires a nonempty topic to identify exactly one loaded dataset; an empty topic
explicitly selects the host's representative dataset. Converted values must be
recomputed after user edits to source offsets or the time reference.

Tab `id` and visible `title` are separate. `create("run-a", "Temperature")` and
`create("run-b", "Temperature")` create two independently addressable tabs.
Renaming or reordering tabs preserves their IDs. Recreating an ID replaces its
contents.

IDs are scoped to the plugin binding and live only while `list()` returns them.
Re-read after workspace changes. `configOf(id)` reports the resolved curves
and title.

`addCurve`/`removeCurve` takes topic, field and optional dataset source separately.
An omitted dataset must resolve uniquely. The host prevents access to other
plugins' and user-created tabs.

### Dataset-qualified processor inputs

`pj.data_processors.v1` accepts `dataset_source:topic/field` inputs via the shared
`pj_base/sdk/dataset_qualified_name.hpp` helper. This is a catalog-dependent
lookup syntax: the longest loaded source prefix wins, and an unmatched prefix
leaves the entire string as a bare lookup, without stripping it. Unknown whole
names and ambiguous dataset matches fail. A processor's qualified inputs must
agree on one dataset. Marker per-series output keys follow the same rule;
transform output names create new topics and are unqualified.

Colons can occur in both parts: `(a, b:/t/f)` and `(a:b, /t/f)` compose identically,
and with both sources loaded the parser chooses `a:b`. Composition round-trips
only when the intended source is the longest matching prefix. Do not treat this
string as a persistent dataset identity. Hosts still validate the split result.

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

`readSeriesArrow` / `appendArrowStream` cover *scalar* columns.
Use object writes for **canonical media**, such as images, point clouds and
annotation overlays. They route to the host `ObjectStore`, not the columnar engine:

1. `registerObjectTopic(source, name, type[, extra_metadata])` declares a topic
   under a data source you created. The typed overload writes the canonical
   `builtin_object_type` metadata key with the exact `PJ::sdk::name()` value
   (for example, `"kImage"`). It returns an `ObjectTopicHandle`. The raw
   `metadata_json` overload remains available for custom or untyped topics.
2. `pushOwnedObject(topic, ts, payload)` pushes serialized bytes (e.g. a
   `PJ.Image` produced via `serializeImage()` from `pj_base/builtin/image_codec.hpp`).
   The push is **eager**: the host copies the bytes immediately.
   You may reuse or free the buffer when the call returns. Toolbox has no
   lazy/fetch variant because it already holds the bytes when writing them.

```cpp
PJ::sdk::ObjectTopicMetadataBuilder metadata;
metadata.string("image_codec", "pj_image_v1");

auto topic = toolboxHost().registerObjectTopic(
    source, "mosaic/preview", PJ::sdk::BuiltinObjectType::kImage, metadata);
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

> **Older-host compatibility:** these methods are appended tail slots on the
> toolbox host vtable. The SDK gates each call on the host's `struct_size`.
> Both return `unexpected("…older host")` on hosts that predate the slots.
> Check the returned `Expected`/`Status`. Degrade gracefully if you must support
> pre-object-write hosts.

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

The store captures `appendRecord` writes and counts `createDataSource` and
`notifyDataChanged` calls. `flatRecords()` gives a flat (timestamp, name, value)
view. `writtenRecords()` preserves nested rows of fields.
See `pj_plugins/testing/toolbox_test_store.hpp` for the full API.

## Examples

- `pj_plugins/examples/mock_toolbox.cpp` — minimal test fixture that exercises
  the full `ToolboxPluginBase` API surface: capabilities, config persistence,
  host binding, and dialog context.
- `pj_plugins/tests/toolbox_plugin_test.cpp` — end-to-end host-side test
  using `PJ::testing::ToolboxTestStore` (in `pj_plugins/include/pj_plugins/testing/`)
  to drive a toolbox plugin through ingest, transform, and config scenarios.

## Descriptor import and source promotion (0.20.0)

A toolbox (or any plugin family) that can re-create a dataset from a persisted
descriptor — a cloud session, a database query — advertises
`pj.descriptor_import.v1` from `pluginExtension()` by returning a static
`PJ_descriptor_import_provider_v1_t` (`pj_base/descriptor_import_protocol.h`):

- `query_descriptor` is synchronous and strictly bounded (no network, no
  credential resolution, no blocking locks): it classifies trust
  (refused / needs-confirmation / trusted), reports whether the artifact is
  already materialized locally, and ALWAYS returns the provider's canonical
  `source_identity`, the planned `local_path_utf8`, and `estimated_bytes`
  (0 = unknown).
- `start_import` launches the asynchronous import job (caller-sized request:
  descriptor + flags + `max_transfer_bytes` ceiling). Exactly two serialized
  callbacks: `on_dataset` (zero-or-one — announce the provisional dataset
  BEFORE any progress/publication/promotion) and `on_terminal` (exactly-once,
  last). Progress, publish ticks and cooperative stop do NOT ride the job:
  they ride the dataset-scoped ingest lifecycle below.

During import, drive the standard ingest lifecycle through
`ToolboxRuntimeHostView::createDatasetIngest(dataset_id)`. This is the canonical
dataset-scoped surface for delegated parsing and direct writes.
Use `ensureParserBinding` / `pushMessage` for delegated parsing.
For direct writes, append Arrow or scalars through `ToolboxHostView` and use
the ingest view only for progress/stop. Refresh the host through
`notifyDataChanged()`.

When the artifact file is complete, request promotion to a stock file-backed
source through `PJ::SourcePromotionHostView::promoteToFileSource()`.
This uses the optional per-instance `pj.source_promotion.v1` service.
The request names the dataset, artifact path, provider's `source_identity`,
descriptor and loader (`loader_plugin_id` + `loader_config_json`).
That loader can re-ingest the artifact with eager-path-identical semantics.

Promotion is asynchronous. Acceptance by `promoteToFileSource()` means only
queued; success arrives via the result callback. A provider that cannot yet
produce such an artifact reports `SUCCEEDED_EAGER_ONLY` instead.

C++ consumers: `PJ::DescriptorImportProviderView`, `PJ::JoinableJob`,
`PJ::SourcePromotionHostView` in `pj_base/sdk/descriptor_import.hpp`.

**Provider-side support.** Link `plotjuggler_sdk::source` alongside `plugin_sdk`.
Start with [Existing SDK utilities](../../docs/sdk-utilities.md), then follow
[the provider contract](../../docs/provider-guide.md). It covers the existing
job/cache machinery, descriptor validation, completion, Stop and test fixtures.
The pre-0.31 `descriptor_import_support` component and include directory forward
to `source` for one release; new code uses `pj_base/sdk/source/`.

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
