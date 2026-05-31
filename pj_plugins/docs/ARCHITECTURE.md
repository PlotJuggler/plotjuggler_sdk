# Plugin System Architecture

## 0a. ABI stability and evolution rules (v5)

Seven rules the loader and every plugin author rely on. Breaking any of
these is an ABI break and requires a future `PJ_ABI_VERSION` bump.

1. **Boot-level ABI symbol.** Every plugin .so exports
   `pj_plugin_abi_version` as a `uint32_t` symbol independent of any
   vtable. The host `dlsym`s it BEFORE fetching the family vtable;
   missing or mismatched symbol is a fail-fast rejection with a specific
   error. The symbol is emitted at file scope by
   `pj_base/include/pj_base/plugin_abi_export.hpp`, which is transitively
   included by every family SDK base header
   (`data_source_plugin_base.hpp`, `dialog_plugin_base.hpp`,
   `message_parser_plugin_base.hpp`, `toolbox_plugin_base.hpp`). Weak
   linkage (`__attribute__((weak))` / `__declspec(selectany)`) folds
   duplicate definitions across translation units in one DSO, so a single
   .so can host multiple plugin families (e.g. DataSource + Dialog) with
   one `PJ_*_PLUGIN(...)` macro per family — no duplicate-symbol error.
   Current value is `PJ_ABI_VERSION == 5`.

2. **Min-vtable-size floor, pinned at v4.0.** Each family header defines
   `PJ_<FAMILY>_MIN_VTABLE_SIZE` — the byte count of the vtable as
   shipped in v4.0. The loader accepts
   `struct_size >= MIN_VTABLE_SIZE`. This constant MUST NEVER GROW
   within a major series. Growing it would reject plugins compiled
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
     is an ABI break. `PJ_error_t` has `extended` + `extended_kind` slots
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

## 0. C protocol v4 (current under ABI v5)

All four plugin families (DataSource, MessageParser, Toolbox, Dialog) keep
the v4 C protocol layouts under the v5 boot ABI. Key v4 distinguishing
features (a superset of everything the
previously-circulated pre-v4 design included):

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
- **Embedded-manifest plugin discovery.** Each DSO exports a
  family-specific protocol vtable with embedded metadata (`manifest_json`
  for data sources, parsers, toolboxes, and newly built dialogs; legacy v4.0
  dialogs fall back to `create()` + `get_manifest()` during inspection).
  Host-side `PJ::scanPluginDsos(dir)` (in
  `pj_plugins/host/plugin_catalog.hpp`) walks platform plugin libraries,
  loads each candidate, validates the ABI and protocol vtable, and parses
  `id`, `name`, `version`, family-specific fields, and optional metadata
  directly from the embedded manifest. Broken or incompatible candidates
  are reported as diagnostics while discovery continues.
- **No more RTLD_DEEPBIND.** The loader uses `RTLD_NOW | RTLD_LOCAL`
  only (DEEPBIND was a documented ASAN/allocator-interposition trap).
  Plugin-local symbol isolation is left to `-fvisibility=hidden`.

Structural shape inherited from the pre-v4 design work (carries the
service registry, error out-params, and typed borrowed-dialog patterns):

- **Service registry as the sole binding mechanism.** Plugin vtables expose
  a single `bind(ctx, registry, err)` slot. The host registers all services
  (write hosts, runtime hosts, colormap, settings, etc.) under canonical
  reverse-DNS-style names (e.g. `"pj.source_write.v1"`,
  `"pj.runtime.v1"`, `"pj.toolbox_runtime.v1"`, `"pj.colormap.v1"`,
  `"pj.settings.v1"`). Plugins acquire only the services they use.
  `"pj.settings.v1"` (optional) is a QSettings-like key/value store any plugin
  family can use for persistent state — the plugin sees a Qt-free
  `sdk::SettingsView` (`setValue(key, v)` returns a `Status`; reads return an
  `Expected`, e.g. `if (auto v = settings.value(key)) v->toInt(42)`, so a host
  backend fault surfaces instead of silently masquerading as a missing key); the
  host backs it (QSettings in the GUI app, JSON in a headless host) and
  namespaces keys per plugin.
- **Structured errors everywhere.** All fallible ABI calls take a
  `PJ_error_t* out_error` out-parameter. The old per-plugin `get_last_error`
  slot is gone.
- **Shared write contract, typed ABI services.** DataSource, MessageParser,
  and Toolbox all write through the same datastore backend and follow the same
  scalar/Arrow ownership rules, but the ABI keeps three distinct service
  vtables: `PJ_source_write_host_vtable_t`,
  `PJ_parser_write_host_vtable_t`, and `PJ_toolbox_host_vtable_t`.
  The service name selects the family-specific type (`"pj.source_write.v1"`,
  `"pj.parser_write.v1"`, `"pj.toolbox_write.v1"`), so the compiler prevents
  a parser from calling source/toolbox-only operations.
- **Typed borrowed dialog.** `get_dialog_context()` returning `void*` is
  replaced by `get_dialog()` returning a `PJ_borrowed_dialog_t` fat pointer
  `{ctx, const PJ_dialog_vtable_t* vtable}`.
- **Family-specific plugin vtables after the common prefix.** DataSource,
  MessageParser, and Toolbox vtables share
  `protocol_version, struct_size, create, destroy, manifest_json`; subsequent
  slots are family-specific. For example, DataSource and Toolbox have
  `capabilities`, while MessageParser has `bind_schema`. Dialogs expose a
  GUI-oriented protocol with `get_manifest()`/`get_ui_content()` and an
  optional static `manifest_json` tail slot for metadata-only discovery.

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
   in move-only RAII handles. Handles retain shared ownership of the loaded
   DSO, so plugin code remains mapped until every instance created from that
   DSO has been destroyed.

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
      dialog_library.cpp
  include/pj_plugins/host/
    data_source_library.hpp         ← host-side loader
    data_source_handle.hpp          ← RAII handle
    message_parser_library.hpp
    message_parser_handle.hpp
    toolbox_library.hpp
    toolbox_handle.hpp
    plugin_catalog.hpp              ← embedded-manifest DSO scanner
    plugin_runtime_catalog.hpp      ← shared host catalog (all families)
    service_registry_builder.hpp    ← service wiring into bind()
    config_envelope.hpp             ← versioned config wrapper
  include/pj_plugins/sdk/
    message_parser_plugin_base.hpp  ← C++ SDK (parser base lives here, NOT pj_base)
    object_ingest_policy.hpp        ← ObjectIngestPolicyResolver
    detail/
      message_parser_trampolines.hpp
  include/pj_plugins/testing/
    toolbox_test_store.hpp          ← fake Arrow host for toolbox tests
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
`pj_plugins` (which depends on `pj_base`). `pj_datastore`
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
| Dialog | `DialogPluginTyped` | `manifest()`, `ui_content()`, `widget_data()`, event handlers | `PJ_DIALOG_PLUGIN(Class, manifest)` (or legacy `PJ_DIALOG_PLUGIN(Class)`; works standalone or co-resident with another family) |

All SDK base classes:
- Generate the C vtable via `vtableWithCreate()` at static init.
- Validate compile-time manifest JSON string literals (required keys) via `PJ_ASSERT`; dialog manifests supplied through the static macro path are parsed by the host catalog without instantiation, while legacy dialog manifests are validated through the fallback runtime path.
- Catch all C++ exceptions in trampolines, populate `PJ_error_t` out-params
  when available, and return `false`/`null` across the ABI boundary.

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

### 5.1 Host-side diagnostic propagation

Host code that loads plugins accepts an
optional `PJ::DiagnosticSink` (`pj_base/include/pj_base/diagnostic_sink.hpp`)
in its constructor. The sink is a `std::function<void(const PJ::Diagnostic&)>`
the host invokes for every plugin-load lifecycle event — failed `dlopen`,
missing required manifest fields, malformed JSON, successful loads,
hot-reload detection, etc. Each event carries a level
(`kInfo`/`kWarning`/`kError`), a `source` string, an optional plugin id, a
message, and a timestamp.

Embedding apps wire one sink into their host loaders and any application-level
extension services so the GUI can show one chronological diagnostic stream.
Pure-C++ host loaders remain toolkit-free; GUI hosts provide any event-loop
adapter needed to marshal diagnostics onto their UI thread.

A default-constructed sink discards events at zero cost, so loaders that
take no sink behave as before.

## 6. RAII Handles

Each family has a move-only RAII handle:

- Constructor calls `vt->create()` to allocate the plugin instance.
- Destructor calls `vt->destroy(ctx)`.
- Handles created by a loader retain a shared DSO owner; destroying or
  hot-reloading the loader/catalog entry cannot `dlclose` the plugin while
  live handles still call its vtable.
- No copy, move-only semantics.
- Methods delegate to vtable functions with the stored context pointer.

**Borrowed handles:** `DialogHandle` supports a `borrowed()` factory for
dialogs that are members of another plugin (e.g. a DataSource's dialog).
A borrowed handle does NOT call `create()` or `destroy()` — it wraps a
pre-existing context pointer obtained via `getDialog()` (which plugin
authors implement with the SDK helper `PJ::borrowDialog(dialog_member_)`).
The owning plugin handle must outlive the borrowed dialog because it owns both
the dialog object storage and the shared DSO lifetime token.

## 7. Dialog Host Runtime

The core repository provides the toolkit-neutral dialog C ABI, C++ SDK,
host-side loader, and `DialogHandle` lifecycle wrapper. A consuming GUI
application supplies the concrete renderer/reactive loop for its UI toolkit.

### Reactive loop

```
1. Read widget_data() from plugin → JSON
2. Parse JSON into WidgetDataView
3. Apply WidgetDataView to host widgets
4. Wait for user interaction or tick timer
5. On widget signal → build event JSON → call on_widget_event()
6. If returns true → goto 1 (re-read widget_data)
7. On tick timer → call on_tick()
8. If returns true → goto 1
9. On accept → call on_accepted(final_state_json)
10. On reject → call on_rejected()
```

### Widget binding

The concrete host binding handles the bidirectional bridge:

- push `WidgetDataView` values into host widgets without feedback loops
- wire host widget signals to `WidgetEventBuilder` output, which produces
  event JSON for the plugin's `on_widget_event()`

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
- Object-topic writes — `register_object_topic` + `push_owned_object`
  route canonical media (images, point clouds, annotations) into the
  session `ObjectStore` rather than the columnar engine. They forward to
  the same `ObjectStore::registerTopic` / `pushOwned` the DataSource and
  Parser object-write hosts use, so the toolbox host now requires an
  `ObjectStore&` at construction alongside the `DataEngine&`. These are
  **tail slots** appended to `PJ_toolbox_host_vtable_t` under ABI v5 (no
  version bump): existing slot offsets are unchanged, and `struct_size`
  gating lets pre-object-write plugins and hosts interoperate — the SDK
  `ToolboxHostView` returns an "older host" error when the slot is absent.

### Arrow C Data Interface ownership rules

The v4 write path, `append_arrow_stream(ctx, topic, stream,
timestamp_column, err)` for source/toolbox hosts and
`append_arrow_stream(ctx, stream, timestamp_column, err)` for the
parser host:

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
  plugin remains responsible for releasing the stream. This includes cases
  where the host inspected or partially consumed the stream before returning
  an error. `ArrowStreamHolder`'s destructor handles this automatically when
  the plugin uses the recommended rvalue-ref SDK overload.
- `timestamp_column` names the int64 column whose values are
  nanoseconds since Unix epoch. Passing an empty view means "synthesise
  a monotonic timestamp per row"; useful for streams with no natural
  time axis.
- Parser writes are already bound to one topic by the host service, so
  the parser variant does not take a topic handle. Ownership rules are
  otherwise identical.

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
| `mock_dialog.cpp` (in `dialog_protocol/examples/`, not `pj_plugins/examples/`) | Standalone dialog: QLineEdit, QSpinBox, QCheckBox, config persistence |

### Test files (`pj_plugins/tests/`)

| Test | Coverage |
|---|---|
| `data_source_library_test.cpp` | Library loading, vtable validation |
| `file_source_integration_test.cpp` | FileSourceBase end-to-end |
| `delegated_ingest_integration_test.cpp` | _Disabled (pending v3-port: uses removed `bindWriteHost`/`bindRuntimeHost`/`get_last_error`; CMake target commented out). Coverage currently provided by `data_source_library_test.cpp` + `message_parser_library_test.cpp`._ |
| `source_dialog_integration_test.cpp` | DataSource dialog + config envelope |
| `message_parser_library_test.cpp` | Parser library loading |
| `toolbox_plugin_test.cpp` | Toolbox loading, host binding, read+write flow |
| `dialog_handle_test.cpp` | Owned and borrowed handle lifecycle |
| `dialog_library_test.cpp` | Dialog library loading |
| `dialog_plugin_typed_test.cpp` | Typed event dispatch |
| `widget_data_test.cpp` | WidgetData builder |
| `widget_data_view_test.cpp` | WidgetDataView JSON parsing |
| `widget_event_builder_test.cpp` | Event JSON generation |
| `widget_event_test.cpp` | Event parsing |
| `plugin_lifecycle_test.cpp` | Plugin create/destroy lifecycle |

## Builtin-object pipeline (PR #86)

The v4 DataSource runtime host adds a tail slot `push_message_v2`
(offset 96 in `PJ_data_source_runtime_host_vtable_t`) that takes a
deferred byte-fetch callable instead of bytes:

```c
typedef struct PJ_message_data_fetcher_t {
  void* ctx;
  bool  (*fetchMessageData)(void* ctx, PJ_payload_t* out, PJ_error_t* err) PJ_NOEXCEPT;
  void  (*release)(void* ctx);
} PJ_message_data_fetcher_t;

bool (*push_message_v2)(
    void* ctx, PJ_parser_binding_handle_t handle, int64_t timestamp_ns,
    PJ_message_data_fetcher_t fetch_message_data,
    PJ_error_t* out_error) PJ_NOEXCEPT;
```

The C++ SDK exposes this through
`DataSourceRuntimeHostView::pushMessage(handle, ts, fetch_callable)`,
which wraps any callable returning `PayloadView` (preferred, zero-copy)
or `std::vector<uint8_t>` into the C ABI struct.

The host orchestrates dispatch through an `ObjectIngestPolicyResolver`
that cascades `topic > source > type > default`:

- `kEager`: invoke `fetchMessageData` now, run `parseScalars` +
  `parseObject`, persist via `ObjectStore::pushOwned`.
- `kLazyObjectsEagerScalars`: invoke once for scalars, keep the
  callable behind `ObjectStore::pushLazy` for on-pull materialisation.
- `kPureLazy`: skip the callable at ingest, register a lazy
  ObjectStore entry only.

Parsers participate via three optional virtual entry points on
`MessageParserPluginBase` — `classifySchema`, `parseScalars`,
`parseObject` — that map to the per-schema slots in the
`SchemaHandler` table. The shape that crosses both ABI boundaries (C
struct on the DataSource side, in-process variant on the parser side)
is opaque-payload-by-default: `BuiltinObject` is `std::any`, so
appending a new builtin type does not change the public type and
forward compatibility is automatic. Concrete builtins live under
`pj_base/builtin/` (`Image`, `DepthImage`, `PointCloud`,
`ImageAnnotations`, `FrameTransforms`); see `docs/builtin_type.md` for the type
catalog and `docs/image_annotations_format.md` for the canonical annotation
wire format.
