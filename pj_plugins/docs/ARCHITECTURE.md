# Plugin System Architecture

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
| DataSource | `data_source_protocol.h` | `PJ_get_data_source_vtable` | 2 |
| MessageParser | `message_parser_protocol.h` | `PJ_get_message_parser_vtable` | 1 |
| Toolbox | `toolbox_protocol.h` | `PJ_get_toolbox_vtable` | 1 |
| Dialog | `dialog_protocol.h` | `PJ_get_dialog_vtable` | 1 |

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
pre-existing context pointer obtained via `dialogContext()`.

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
- `MaterializedSeries` — decompressed time series for a specific field.

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
