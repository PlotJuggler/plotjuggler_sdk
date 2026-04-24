# Plugin System Architecture

## 0a. ABI stability and evolution rules (v4)

Seven rules the loader and every plugin author rely on. Breaking any of
these is an ABI break and requires a v5 bump.

1. **Boot-level ABI symbol.** Every plugin .so exports
   `pj_plugin_abi_version` as a `const uint32_t` symbol independent of
   any vtable. The host `dlsym`s it BEFORE fetching the family vtable;
   missing or mismatched symbol is a fail-fast rejection with a specific
   error. Emitted automatically by `PJ_DATA_SOURCE_PLUGIN`,
   `PJ_MESSAGE_PARSER_PLUGIN`, `PJ_TOOLBOX_PLUGIN` macros. Current value
   is `PJ_ABI_VERSION == 4`.

2. **Min-vtable-size floor, pinned at v4.0.** Each family header defines
   `PJ_<FAMILY>_MIN_VTABLE_SIZE` — the byte count of the vtable as
   shipped in v4.0. The loader accepts
   `struct_size >= MIN_VTABLE_SIZE`. This constant MUST NEVER GROW
   within the v4 series. Growing it would reject plugins compiled
   against older v4 headers (which correctly report a smaller size),
   silently breaking the forward-compatibility promise.

3. **Tail-slot gating.** Every vtable slot added after v4.0 is a tail
   slot. Host reads must go through the `PJ_HAS_TAIL_SLOT(vtable_type,
   vtable_ptr, field)` macro, which verifies both that the plugin's
   `struct_size` reaches the slot AND that the slot is non-null. Skipping
   this gate is undefined behaviour on plugins built against older
   headers.

4. **Frozen vs appendable struct classification.** Each ABI-visible
   struct carries a header comment declaring its policy:
   - **ABI-FROZEN**: `PJ_error_t`, `PJ_string_view_t`, `PJ_bytes_view_t`,
     `PJ_borrowed_dialog_t`, `PJ_service_t`, `PJ_service_registry_t`,
     handle types, primitive-value unions. Layout permanent; any change
     is a v4 break. `PJ_error_t` has `extended` + `extended_kind` slots
     reserved as its one growth path — do not add further top-level
     fields.
   - **ABI-APPENDABLE**: all `*_vtable_t` types, service-host vtables,
     `PJ_service_registry_vtable_t`. New slots at the tail; read with
     `PJ_HAS_TAIL_SLOT`.

5. **Compile-time ABI layout sentinels.** `pj_base/tests/abi_layout_sentinels_test.cpp`
   consists entirely of `static_assert`s pinning `sizeof`, `alignof`,
   and `offsetof` for every ABI struct plus `sizeof(void*)` (64-bit
   guard) and enum-size pins (defends against `-fshort-enums`). A
   failed assertion at compile time is ALWAYS a serious signal:
   - Offset changes = field reorder = ABI break.
   - MIN-size increase = floor moved = forward-compat break.
   - sizeof growth = deliberate append, update the assertion.

6. **Service-name grammar (compile-time enforced).**
   | Pattern | Stability |
   |---|---|
   | `"pj.<name>.v<N>"` | Stable. Frozen for ≥3 releases before deprecation. |
   | `"pj.experimental.<name>/draft-<N>"` | Unstable. No guarantees. |
   `sdk/service_traits.hpp` calls `detail::isValidServiceName()` in a
   `static_assert` at every trait's `kName`. Requesting a
   `pj.experimental.*` service should log a runtime warning through the
   `pj.runtime.v1` log channel.

7. **Exception discipline at the ABI boundary.** Every C ABI entry
   point (SDK trampolines and host-side service trampolines) must
   catch all exceptions and convert to a `PJ_error_t` out-param (or a
   safe default for non-fallible calls). C++ exceptions across
   `dlopen` boundaries are undefined behaviour in practice. The
   `data_source_trampolines.hpp` / `message_parser_trampolines.hpp` /
   `toolbox_trampolines.hpp` files centralize this pattern — mirror it
   exactly in any new trampoline.

### abidiff drift gate

The rules above are enforced mechanically by `abidiff` (from
libabigail) against a checked-in baseline at
`pj_base/abi/baseline.abi`. Opt in with
`-DPJ_ENABLE_ABI_CHECK=ON`; two CMake targets become available:

| Target | Purpose |
|---|---|
| `abi_check` | Diff the current build's `mock_data_source_plugin` DSO against `baseline.abi`. Fatal on incompatible changes (libabigail bit 8); warning on backward-compatible additions (bit 4). |
| `abi_update_baseline` | Regenerate `baseline.abi` via `abidw`. Run deliberately when landing a reviewed ABI change (tail-slot promotion, MIN_VTABLE_SIZE repin, v-bump). |

Adding `PJ_BUILD_TESTS=ON` also registers `abi_check_test` with CTest
so `./test.sh` picks it up. The plumbing lives in
`cmake/PjAbiCheck.cmake` and `cmake/PjAbiCheckRun.cmake`.

### Plugin extension query (CLAP-style)

Each family vtable has a tail slot
`const void* (*get_plugin_extension)(void* ctx, PJ_string_view_t id)`
that plugins use to expose additional capabilities to the host without
bumping the family protocol version. The plugin returns a static POD
for known ids or `nullptr`. Hosts call via `handle.getPluginExtension(id)`
(tail-slot-gated). Use the experimental namespace for work-in-progress
extensions; graduate to stable (`pj.<name>.v1`) once locked in.

## 0. Protocol v4 (current)

All four plugin families (DataSource, MessageParser, Toolbox, Dialog) track
protocol v4. Key v4 distinguishing features (a superset of everything the
previously-circulated v3 design included — v3 was never an official
release, and its changes roll into v4):

- **Arrow C Data Interface at the data boundary.** The write-host
  vtables expose `append_arrow_stream(ArrowArrayStream*)` as the
  canonical bulk path; per-record `append_record` / `append_bound_record`
  remain for streaming producers. Toolbox read-side returns host-owned
  `ArrowSchema` + `ArrowArray` via `read_series_arrow` (no more
  materialised `std::vector` at the boundary).
- **PJ_NOEXCEPT on every vtable slot.** Exceptions across `extern "C"`
  are UB; the noexcept specifier is part of the C++17 function type and
  enforced at compile time. Trampolines catch and translate internally.
- **Thread-class tags on every slot.** Every function-pointer field in
  the ABI headers carries a `[main-thread]` / `[stream-thread]` /
  `[thread-safe]` comment. Host-side runtime checking is optional
  (reserved for a future `"pj.thread_check.v1"` service).
- **Sidecar-based plugin discovery.** `pj_emit_plugin_manifest` (CMake
  helper in `cmake/PjPluginManifest.cmake`) writes a
  `<target>.pjmanifest.json` beside each DSO at build and install
  time. The sidecar is the DSO's own `manifest.json` plus two
  autogenerated keys — `"abi_major"` (matches `PJ_ABI_VERSION`) and
  `"family"` (one of `data_source`, `message_parser`, `toolbox`,
  `dialog`). Host-side `PJ::scanPluginSidecars(dir)` (in
  `pj_plugins/host/plugin_catalog.hpp`) parses every sidecar in a
  directory into `PluginDescriptor` records — name, version, category,
  file extensions, encoding, capabilities — WITHOUT dlopen'ing any
  shared library. On activation the host dlopens the DSO, calls
  `get_plugin_manifest`, and warns (not errors) if the two disagree —
  DSO truth wins.
- **No more RTLD_DEEPBIND.** The loader uses `RTLD_NOW | RTLD_LOCAL`
  only (DEEPBIND was a documented ASAN/allocator-interposition trap).
  Plugin-local symbol isolation is left to `-fvisibility=hidden`.

Structural shape inherited from the pre-v4 design work (carries the
service registry, error out-params, and typed borrowed-dialog patterns
that had been developed in the unreleased v3 iteration):

- **Service registry as the sole binding mechanism.** Plugin vtables expose
  a single `bind(ctx, registry, err)` slot. The host registers all services
  (write hosts, runtime hosts, colormap, etc.) under canonical
  reverse-DNS-style names (e.g. `"pj.source_write.v1"`,
  `"pj.runtime.v1"`, `"pj.toolbox_runtime.v1"`, `"pj.colormap.v1"`). Plugins
  acquire only the services they use.
- **Structured errors everywhere.** All fallible ABI calls take a
  `PJ_error_t* out_error` out-parameter. The old per-plugin `get_last_error`
  slot is gone.
- **Unified write surface.** The three previous write-host vtables
  (`PJ_source_write_host_vtable_t`, `PJ_parser_write_host_vtable_t`,
  `PJ_toolbox_host_vtable_t`) collapse into one `PJ_write_surface_vtable_t`.
  Service name selects semantics; host implementations enforce scope.
  Three SDK facade views (`SourceWriteHostView`, `ParserWriteHostView`,
  `ToolboxHostView`) still present family-appropriate APIs at the C++ level.
- **Typed borrowed dialog.** `get_dialog_context()` returning `void*` is
  replaced by `get_dialog()` returning a `PJ_borrowed_dialog_t` fat pointer
  `{ctx, const PJ_dialog_vtable_t* vtable}`.
- **Uniform plugin-vtable prefix.** Every family vtable starts with
  `protocol_version, struct_size, create, destroy, manifest_json,
  capabilities, bind, save_config, load_config` in that order. Host-side
  generic code can iterate all families through a common header layout.

Service traits (`pj_base/sdk/service_traits.hpp`,
`sdk/toolbox_plugin_base.hpp`) map canonical names to their ABI type and
C++ view. `PJ::ServiceRegistryBuilder` (`pj_plugins/host/`) is the
host-side assembler that populates a `PJ_service_registry_t` from
registered services.

## 1. Three-Level Design

Every plugin family follows the same three-level pattern:

```
C ABI protocol  →  C++ SDK base class  →  Host loader + RAII handle
   (pj_base)          (pj_base)            (pj_plugins)
```

1. **C ABI protocol** — a vtable struct in a plain-C header. Defines the
   binary contract between host and plugin. No C++ types cross this boundary.

2. **C++ SDK base class** — plugin authors subclass this, override virtuals,
   and export with a macro. The SDK generates the vtable with exception-safe
   trampolines automatically.

3. **Host loader + RAII handle** — host-side code that dlopen's the `.so`,
   resolves the vtable symbol, validates version/size, and wraps instances
   in move-only RAII handles.

## 2. Module Structure

```
pj_base/
  include/pj_base/
    data_source_protocol.h        ← C ABI
    message_parser_protocol.h     ← C ABI
    toolbox_protocol.h            ← C ABI
    plugin_data_api.h             ← shared data-plane ABI (write hosts)
    sdk/
      data_source_plugin_base.hpp   ← C++ SDK
      data_source_patterns.hpp      ← FileSourceBase, StreamSourceBase
      message_parser_plugin_base.hpp
      toolbox_plugin_base.hpp
      plugin_data_api.hpp           ← C++ wrappers for data hosts

pj_plugins/
  dialog_protocol/
    include/pj_plugins/
      dialog_protocol.h            ← C ABI for dialogs
      sdk/
        dialog_plugin_base.hpp      ← C++ SDK
        dialog_plugin_typed.hpp     ← typed event dispatch
        widget_data.hpp             ← WidgetData builder
      host/
        dialog_library.hpp          ← host-side loader
        dialog_handle.hpp           ← RAII handle (owned + borrowed)
    src/
      dialog_engine.cpp             ← Qt rendering + reactive loop
      dialog_library.cpp
      widget_binding.cpp            ← apply WidgetData to Qt widgets
  include/pj_plugins/host/
    data_source_library.hpp         ← host-side loader
    data_source_handle.hpp          ← RAII handle
    message_parser_library.hpp
    message_parser_handle.hpp
    toolbox_library.hpp
    toolbox_handle.hpp
    config_envelope.hpp             ← versioned config wrapper
  src/
    data_source_library.cpp
    message_parser_library.cpp
    toolbox_library.cpp

pj_datastore/
  include/pj_datastore/
    plugin_data_host.hpp            ← DatastoreSourceWriteHost,
                                       DatastoreParserWriteHost,
                                       DatastoreToolboxHost
```

**Dependency direction:** Plugins depend only on `pj_base`. The host links
`pj_plugins` (which depends on `pj_base` and optionally Qt). `pj_datastore`
provides the concrete data-host implementations that bridge plugin writes to
the columnar storage engine.

## 3. C ABI Protocols

Each protocol header defines:

- A **plugin vtable** struct with `protocol_version`, `struct_size`,
  `create`/`destroy`, `manifest_json`, and family-specific methods.
- An **entry point symbol** (e.g. `PJ_get_data_source_vtable`) that the
  host resolves via `dlsym`.
- For families with host-to-plugin services: a **runtime host vtable** and
  a fat pointer (`{ctx, vtable}`) pairing context with vtable.

| Family | Protocol header | Entry point symbol | Protocol version |
|---|---|---|---|
| DataSource | `data_source_protocol.h` | `PJ_get_data_source_vtable` | 4 |
| MessageParser | `message_parser_protocol.h` | `PJ_get_message_parser_vtable` | 4 |
| Toolbox | `toolbox_protocol.h` | `PJ_get_toolbox_vtable` | 4 |
| Dialog | `dialog_protocol.h` | `PJ_get_dialog_vtable` | 4 |

**String ownership:** Plugin-returned `const char*` pointers remain valid
until the next call to the same function on the same context. The host copies
if it needs to retain.

**Version safety:** The host validates `protocol_version` and `struct_size`
at load time. Mismatches produce a clear error.

## 4. SDK Base Classes

| Family | Base class | Key virtuals | Export macro |
|---|---|---|---|
| DataSource | `DataSourcePluginBase` | `capabilities()`, `start()`, `stop()`, `currentState()` | `PJ_DATA_SOURCE_PLUGIN(Class, manifest)` |
| DataSource (file) | `FileSourceBase` | `importData()`, `extraCapabilities()` | same macro |
| DataSource (stream) | `StreamSourceBase` | `onStart()`, `onPoll()`, `onStop()`, `extraCapabilities()` | same macro |
| MessageParser | `MessageParserPluginBase` | `parse()` | `PJ_MESSAGE_PARSER_PLUGIN(Class, manifest)` |
| Toolbox | `ToolboxPluginBase` | `capabilities()` | `PJ_TOOLBOX_PLUGIN(Class, manifest)` |
| Dialog | `DialogPluginTyped` | `manifest()`, `ui_content()`, `widget_data()`, event handlers | `PJ_DIALOG_PLUGIN(Class)` |

All SDK base classes:
- Generate the C vtable via `vtableWithCreate()` at static init.
- Validate the manifest JSON string literal (required keys) via `PJ_ASSERT`.
- Catch all C++ exceptions in trampolines, store via `setLastError()`, and
  return `false`/`null` across the ABI boundary.

**Trampoline pattern:** Each base class has a private set of `static`
trampoline functions (e.g. `trampoline_start`) that cast the `void* ctx` to
the concrete class, call the virtual, and wrap the result for C ABI return.
These live in `sdk/detail/*_trampolines.hpp`.

## 5. Host Loaders

Each family has a loader that:
1. Calls `dlopen` (or `LoadLibrary` on Windows) on the `.so` path.
2. Calls `dlsym` for the entry point symbol.
3. Validates `protocol_version` and `struct_size`.
4. Stores the vtable pointer for creating handles.

| Family | Loader class | Load method |
|---|---|---|
| DataSource | `DataSourceLibrary` | `load(path) → Expected<DataSourceLibrary>` |
| MessageParser | `MessageParserLibrary` | `load(path) → Expected<MessageParserLibrary>` |
| Toolbox | `ToolboxLibrary` | `load(path) → Expected<ToolboxLibrary>` |
| Dialog | `DialogLibrary` | `load(path) → Expected<DialogLibrary>` |

Loaders also provide `resolveDialogVtable()` to find the dialog vtable in a
plugin `.so` that exports both a family vtable and a dialog vtable (e.g. a
DataSource with an embedded dialog).

## 6. RAII Handles

Each family has a move-only RAII handle:

- Constructor calls `vt->create()` to allocate the plugin instance.
- Destructor calls `vt->destroy(ctx)`.
- No copy, move-only semantics.
- Methods delegate to vtable functions with the stored context pointer.

**Borrowed handles:** `DialogHandle` supports a `borrowed()` factory for
dialogs that are members of another plugin (e.g. a DataSource's dialog).
A borrowed handle does NOT call `create()` or `destroy()` — it wraps a
pre-existing context pointer obtained via `getDialog()` (which plugin
authors implement with the SDK helper `PJ::borrowDialog(dialog_member_)`).

## 7. Dialog Engine

The dialog engine (`dialog_engine.cpp`) is the Qt-side runtime that renders
dialog plugins. It operates in two modes:

- **Qt mode** — loads the `.ui` XML via `QUiLoader`, creates real Qt widgets,
  wires signals, and runs the reactive loop.
- **Headless mode** — skips Qt rendering, useful for testing dialog logic
  without a display server.

### Reactive loop

```
1. Read widget_data() from plugin → JSON
2. Parse JSON into WidgetDataView
3. Apply WidgetDataView to Qt widgets (widget_binding.cpp)
4. Wait for user interaction or tick timer
5. On widget signal → build event JSON → call on_widget_event()
6. If returns true → goto 1 (re-read widget_data)
7. On tick timer → call on_tick()
8. If returns true → goto 1
9. On accept → call on_accepted(final_state_json)
10. On reject → call on_rejected()
```

### Widget binding

`widget_binding.cpp` handles the bidirectional bridge:

- **`applyWidgetData()`** — pushes `WidgetDataView` values into Qt widgets
  (signal-blocked to prevent feedback loops).
- **`connectWidgetSignals()`** — wires Qt signals to `WidgetEventBuilder`
  output, which produces event JSON for the plugin's `on_widget_event()`.

### `requestAccept()`

A plugin can request the host to close the dialog with OK by setting
`__request_accept` in `widget_data()`. The engine checks this flag after
applying widget state and calls `dialog->accept()` if set.

## 8. Config Envelope

`ConfigEnvelope` (`config_envelope.hpp`) wraps a DataSource's plugin-owned
config alongside host-owned parser binding state:

```json
{"version": 1, "source_config": "...", "parser_binding": "..."}
```

- `pack(source_config, parser_binding)` → envelope JSON string.
- `unpack(envelope_json)` → `Expected<Unpacked>` with both fields.
- The source plugin never sees `parser_binding` — the host manages it.
- Used for layout save/restore of delegated-ingest sources.

## 9. Plugin Data Host Bridge

The data-plane bridge lives in `pj_datastore` and connects plugin write
calls to the columnar storage engine:

| Host adapter | C ABI type | Plugin SDK view | Plugin family |
|---|---|---|---|
| `DatastoreSourceWriteHost` | `PJ_source_write_host_t` | `SourceWriteHostView` | DataSource |
| `DatastoreParserWriteHost` | `PJ_parser_write_host_t` | `ParserWriteHostView` | MessageParser |
| `DatastoreToolboxHost` | `PJ_toolbox_host_t` | `ToolboxHostView` | Toolbox |

All three share a common internal `WriteCore` that handles:
- Topic and field resolution.
- Named → bound field handle lookup and caching.
- Row-level append with type coercion.
- Arrow IPC stream import via nanoarrow.

`DatastoreToolboxHost` additionally provides:
- `CatalogSnapshot` — read-only view of all data sources, topics, fields.
- `MaterializedSeries` — host-internal decompressed time-series type
  used by the toolbox host's C++ implementation. **Not part of the v4
  plugin ABI** — at the boundary, `read_series_arrow` returns
  host-owned `ArrowSchema` + `ArrowArray` structs instead.

### Arrow C Data Interface ownership rules

The v4 write path, `append_arrow_stream(ctx, topic, stream,
timestamp_column, err)`:

- The plugin constructs the `ArrowArrayStream` (typically via
  nanoarrow's `ArrowIpcArrayStreamReaderInit`, Parquet's
  `arrow::RecordBatchReader`, or custom code) and populates its
  `release` callback.
- On **success** (returns `true`): the host has already drained the
  stream via `get_next()` and invoked `stream->release`. The plugin
  MUST NOT release it again. Using `PJ::sdk::ArrowStreamHolder`, call
  `.release()` on the holder after a successful append so its
  destructor becomes a no-op.
- On **failure** (returns `false`): ownership is NOT transferred. The
  host guarantees it has already called `stream->release` on any
  partially-consumed stream before surfacing the error via
  `PJ_error_t` — but the stream struct itself stays on the plugin
  side. `ArrowStreamHolder`'s destructor handles this automatically.
- `timestamp_column` names the int64 column whose values are
  nanoseconds since Unix epoch. Passing an empty view means "synthesise
  a monotonic timestamp per row"; useful for streams with no natural
  time axis.

The v4 read path, `read_series_arrow(ctx, field, out_schema,
out_array, err)`:

- Caller passes zero-initialised `ArrowSchema*` + `ArrowArray*`
  (typically `ArrowSchemaHolder::out()` + `ArrowArrayHolder::out()`).
- On success the host populates both and installs a `release`
  callback. The caller owns the structs and MUST invoke both
  `release`s when done — the RAII holders do this at scope exit.
- The returned array is a two-column struct: `timestamp` (int64 ns
  epoch) and `<field_name>` (typed to the field's primitive type).
  Validity bitmaps follow the Arrow spec for nullable fields.

## 10. Testing Structure

### Mock plugins (`pj_plugins/examples/`)

| Mock | Exercises |
|---|---|
| `mock_data_source.cpp` | Full DataSourcePluginBase: capabilities, direct/delegated ingest, progress, pause/resume, config |
| `mock_file_source.cpp` | FileSourceBase pattern with importData() |
| `mock_source_with_dialog.cpp` | DataSource-owned dialog: two vtables, shared state, borrowed handle |
| `mock_json_parser.cpp` | Minimal MessageParser: text→double |
| `mock_schema_parser.cpp` | Schema binding, bound writes, config persistence |
| `mock_toolbox.cpp` | ToolboxPluginBase: read→transform→write, notifyDataChanged |
| `mock_dialog.cpp` | Standalone dialog: QLineEdit, QSpinBox, QCheckBox, config persistence |

### Test files (`pj_plugins/tests/`)

| Test | Coverage |
|---|---|
| `data_source_library_test.cpp` | Library loading, vtable validation |
| `file_source_integration_test.cpp` | FileSourceBase end-to-end |
| `delegated_ingest_integration_test.cpp` | Parser binding + raw message dispatch |
| `source_dialog_integration_test.cpp` | DataSource dialog + config envelope |
| `message_parser_library_test.cpp` | Parser library loading |
| `toolbox_plugin_test.cpp` | Toolbox loading, host binding, read+write flow |
| `dialog_engine_test.cpp` | Reactive loop, widget data apply, event dispatch |
| `dialog_handle_test.cpp` | Owned and borrowed handle lifecycle |
| `dialog_library_test.cpp` | Dialog library loading |
| `dialog_plugin_typed_test.cpp` | Typed event dispatch |
| `widget_data_test.cpp` | WidgetData builder |
| `widget_data_view_test.cpp` | WidgetDataView JSON parsing |
| `widget_event_builder_test.cpp` | Event JSON generation |
| `widget_event_test.cpp` | Event parsing |
| `plugin_lifecycle_test.cpp` | Plugin create/destroy lifecycle |
