# Toolbox Plugin Protocol

## Context

Toolbox is the third and final plugin family. Unlike DataSource (write-only,
streaming lifecycle) and MessageParser (headless, request/response), Toolbox
plugins are **stateful interactive tools** with full read+write access to host
data. They are long-lived, UI-driven, and may create new data sources or
perform destructive updates.

The **data plane** is already done: `PJ_toolbox_host_t` in `pj_base`, its C++
wrapper `ToolboxHostView`, and the datastore-backed `DatastoreToolboxHost` in
`pj_datastore` — all tested. What remains is the **control plane**: the ABI,
C++ SDK, host-side loader, and runtime host.

### Legacy behavior to preserve

Three legacy Toolbox plugins exist (FFT, Quaternion, Lua Editor). They share:
- `init(PlotDataMapRef&, TransformsMap&)` — receive data + transform map
- `providedWidget()` → `(QWidget*, FIXED|FLOATING)` — persistent UI panel
- `onShowWidget()` → bool — called when toolbox is activated
- Qt signals: `plotCreated()`, `importData()`, `closed()` — notify host of changes
- `xmlSaveState` / `xmlLoadState` — persistence (only Lua uses it meaningfully)

Key difference from DataSource: no streaming lifecycle. Toolbox is activated,
the user interacts with its persistent dialog, and the plugin reads/writes data
on demand.

## Design

### Toolbox lifecycle

```
create → bind_toolbox_host → bind_runtime_host → load_config
  → [show dialog] → user interacts → plugin reads/writes via toolbox host
  → plugin calls notify_data_changed() on runtime host
  → save_config → destroy
```

No state machine. The toolbox is either alive or destroyed. Activation and
deactivation are dialog visibility concerns handled by the host.

### Capability flags

Minimal for v1:

- `HAS_DIALOG` — plugin provides a persistent configuration/interaction UI

Read, write, create-source, and destructive-update capabilities are intrinsic
to all Toolbox plugins (per requirements §5 permission table). No flags needed.

### ABI structure

Following the exact same pattern as DataSource and MessageParser:

1. **C ABI header** (`toolbox_protocol.h`) — plugin vtable + runtime host vtable
2. **C++ SDK base class** (`toolbox_plugin_base.hpp`) — virtual overrides + trampolines
3. **Host-side loader** (`toolbox_library.hpp/.cpp`) — dlopen + vtable validation
4. **Host-side handle** (`toolbox_handle.hpp`) — RAII instance wrapper

## Changes

### 1. C ABI: `pj_base/include/pj_base/toolbox_protocol.h`

New file. Defines:

**`PJ_toolbox_runtime_host_vtable_t`** (plugin calls these on the host):
- `get_last_error(ctx)` → `const char*`
- `report_message(ctx, level, message)` — diagnostic messages to host log
- `notify_data_changed(ctx)` — tells host to refresh UI after toolbox writes

**`PJ_toolbox_runtime_host_t`** — fat pointer `{ctx, vtable}`

**`PJ_toolbox_vtable_t`** (host calls these on the plugin):
- `protocol_version`, `struct_size`
- `create()` → `void*`, `destroy(ctx)`
- `manifest_json` — static string literal (`name`, `version` required)
- `capabilities(ctx)` → `uint64_t`
- `bind_toolbox_host(ctx, PJ_toolbox_host_t)` — data-plane binding
- `bind_runtime_host(ctx, PJ_toolbox_runtime_host_t)` — control-plane binding
- `save_config(ctx)` → `const char*`, `load_config(ctx, json)` → `bool`
- `get_dialog_context(ctx)` → `void*` (owned by plugin, not created/destroyed by host)
- `get_last_error(ctx)` → `const char*`

**Capability flags:**
- `PJ_TOOLBOX_CAPABILITY_HAS_DIALOG = 1 << 0`

**Message levels:** `PJ_toolbox_message_level_t` (INFO, WARNING, ERROR) —
own enum to keep families independent.

**Entry point:** `PJ_get_toolbox_vtable` → `const PJ_toolbox_vtable_t*`

### 2. C++ SDK: `pj_base/include/pj_base/sdk/toolbox_plugin_base.hpp`

New file. Provides:

**`ToolboxRuntimeHostView`** — C++ wrapper over `PJ_toolbox_runtime_host_t`:
- `reportMessage(level, message)`
- `notifyDataChanged()`
- `lastError()`

**`ToolboxPluginBase`** — abstract base class:
- Pure virtual: `capabilities()`
- Virtual with defaults: `bindToolboxHost()`, `bindRuntimeHost()`,
  `saveConfig()`, `loadConfig()`, `dialogContext()`, `lastError()`
- Protected: `toolboxHost()` → `ToolboxHostView`, `runtimeHost()` → `ToolboxRuntimeHostView`
- Static: `vtableWithCreate(create_fn, manifest)` — generates vtable with trampolines
- Trampolines in `detail/toolbox_trampolines.hpp`

**`PJ_TOOLBOX_PLUGIN(ClassName, manifest)`** macro — exports `PJ_get_toolbox_vtable`.

### 3. Host-side loader: `pj_plugins/include/pj_plugins/host/toolbox_library.hpp` + `pj_plugins/src/toolbox_library.cpp`

New files. Same pattern as `DataSourceLibrary`:
- `ToolboxLibrary::load(path)` → `Expected<ToolboxLibrary>`
- dlopen, dlsym `PJ_get_toolbox_vtable`, validate version/size
- `vtable()` accessor
- `createHandle()` → `ToolboxHandle`

### 4. Host-side handle: `pj_plugins/include/pj_plugins/host/toolbox_handle.hpp`

New file. Same RAII pattern as `DataSourceHandle`:
- Constructor calls `vt_->create()`
- Destructor calls `vt_->destroy(ctx_)`
- Move-only, no copy
- Methods: `manifest()`, `capabilities()`, `bindToolboxHost()`,
  `bindRuntimeHost()`, `saveConfig()`, `loadConfig()`, `dialogContext()`,
  `lastError()`

### 5. Mock plugin: `pj_plugins/examples/mock_toolbox.cpp`

New file. Minimal toolbox that:
- Reports `HAS_DIALOG` capability
- On dialog `on_accept`: reads catalog, picks a field, reads its series,
  writes transformed data into a new data source, calls `notifyDataChanged()`
- Exercises: bind toolbox host, bind runtime host, save/load config, dialog

### 6. Tests: `pj_plugins/tests/toolbox_plugin_test.cpp`

New file. Test cases:
- Load mock toolbox library, validate vtable
- Create handle, bind hosts, save/load config round-trip
- Exercise read→transform→write flow through mock plugin
- Verify `notifyDataChanged()` callback fires
- Verify dialog context is non-null when HAS_DIALOG
- No exceptions cross ABI boundary

### 7. CMake updates: `pj_plugins/CMakeLists.txt`

- Add `toolbox_library.cpp` to `pj_plugins` library sources
- Add `mock_toolbox.cpp` as a shared library target (same pattern as mock data source)
- Add `toolbox_plugin_test.cpp` as a test executable

## Files to create

1. `pj_base/include/pj_base/toolbox_protocol.h` — C ABI
2. `pj_base/include/pj_base/sdk/toolbox_plugin_base.hpp` — C++ SDK base class
3. `pj_base/include/pj_base/sdk/detail/toolbox_trampolines.hpp` — ABI trampolines
4. `pj_plugins/include/pj_plugins/host/toolbox_library.hpp` — host-side loader
5. `pj_plugins/src/toolbox_library.cpp` — loader implementation
6. `pj_plugins/include/pj_plugins/host/toolbox_handle.hpp` — RAII handle
7. `pj_plugins/examples/mock_toolbox.cpp` — mock plugin
8. `pj_plugins/tests/toolbox_plugin_test.cpp` — tests

## Files to modify

1. `pj_plugins/CMakeLists.txt` — new targets

## Reference files (patterns to follow)

- `pj_base/include/pj_base/data_source_protocol.h` — C ABI pattern
- `pj_base/include/pj_base/sdk/data_source_plugin_base.hpp` — SDK base class pattern
- `pj_base/include/pj_base/sdk/detail/data_source_trampolines.hpp` — trampoline pattern
- `pj_plugins/include/pj_plugins/host/data_source_handle.hpp` — RAII handle pattern
- `pj_plugins/src/data_source_library.cpp` — loader pattern
- `pj_plugins/examples/mock_data_source.cpp` — mock plugin pattern
- `pj_base/include/pj_base/plugin_data_api.h` lines 189-212 — existing `PJ_toolbox_host_t`
- `pj_base/include/pj_base/sdk/plugin_data_api.hpp` lines 441-546 — `ToolboxHostView`

## Verification

```bash
./build.sh --debug && ./test.sh
```
