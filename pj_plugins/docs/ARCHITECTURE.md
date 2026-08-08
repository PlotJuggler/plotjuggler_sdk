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
     `PJ_service_registry_vtable_t`, `PJ_dialog_host_info_t`, the
     `pj.parser_functional.v1`/v2 extension and sink tables, and
     `pj.parser_route_claims.v1`. New fields or slots go at the tail; readers
     honor the caller-provided size, and vtable slots are read with
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

Stable family-neutral example: `"pj.descriptor_import.v1"`
(`PJ_descriptor_import_provider_v1_t`, declared in the standalone header
`pj_base/descriptor_import_protocol.h` — any plugin family can return it from
`get_plugin_extension`), paired with the optional host-side
`"pj.source_promotion.v1"` service (`PJ_source_promotion_host_vtable_t`,
same header) acquired through `bind()`'s service registry and bound per
plugin instance. C++ wrappers (`DescriptorImportProviderView`, `JoinableJob`,
`SourcePromotionHostView`) live in `pj_base/sdk/descriptor_import.hpp`.
See `docs/toolbox-guide.md` → "Descriptor import and source promotion" for
the plugin-author walkthrough.

Stable MessageParser-specific examples are `"pj.parser_functional.v1"` and
`"pj.parser_functional.v2"` (`PJ_parser_functional_v1_t` and
`PJ_parser_functional_v2_t` in `pj_base/parser_functional_protocol.h`) plus the
exact route classifier `"pj.parser_route_claims.v1"`. A handler-registering
`MessageParserPluginBase` exposes all three automatically. The host queries v2
first and falls back to v1; v2 adds one eligible object-field splice while
leaving scalar parsing unchanged. Route classification reports exact handler
coverage only, and the host synthesizes the universal wildcard scalar claim.
Hosts must query after `bind_schema` and must not cache an earlier absence
because schema-generic plugins may register their handler while binding. A
parser that still implements only legacy `parse()` does not advertise the
functional extensions merely because it was rebuilt.

### Functional parser modules

Functional parser modules use the family-independent export ABI in
`pj_base/parser_module_abi.h`, not a plugin-family vtable. The host-side
`NativeParserModule` loader opens each artifact with local, immediate symbol
resolution, resolves every operational and native metadata export, gates ABI
version 1, and copies the embedded manifest for subsequent
`ParserClaimCatalog` admission. Native module mappings have process-session
lifetime in v1 and are never unloaded.

`NativeParserModuleInstance` owns one create/bind/parse/destroy lifecycle. It
uses the frozen little-endian codecs for input and output, consumes all
module-owned descriptor views before returning, validates output route and
expected object type, and checks a splice against both the canonical object's
eligible field and the original payload bounds. A valid splice is materialized
into that field, so the returned canonical object is complete; its sidecar
retains the original offset and bytes for future zero-copy integration.
Malformed returned descriptors, ineligible/out-of-bounds returned splices,
type/route mismatch, and bad tokens are contract violations; other
module-reported per-message failures are data errors. The separate
non-thread-safe `ParserModuleStrikeTracker` quarantines a module claim on its
third contract violation, permits one same-descriptor recreation, and disables
the claim after a repeated three-strike cycle. Executor placement, generation
ownership, folder scanning, and rescan policy remain application concerns.

Module authors use the standalone C++17 headers under
`pj_base/include/pj_base/parser_module/` through the zero-linkage
`plotjuggler_sdk::parser_module` target. The readers compile ROS 2 concatenated
`.msg` bundles or protobuf `FileDescriptorSet` field paths at bind time; the
CDR locator supports fixed arrays, bounded and unbounded sequences, bounded
strings, and string arrays/sequences, while cyclic or over-depth schemas fail at
bind. The typed `ObjectWriter` emits Image, PointCloud, DepthImage,
OccupancyGrid, CompressedPointCloud, Mesh3D, VideoFrame, OccupancyGridUpdate,
and VoxelGrid descriptors, including each type's eligible single-splice form.
Module parse callbacks receive the per-message `pj::Timestamp` alongside the
payload. `BindingInfo` views expire when `bind()` returns; `owningCopy()` is the
fallible retention path. Bulk storage uses nothrow allocation, and protobuf
matching is bounded, so allocation failure is returned as data error rather
than escaping as a trap. `PJ_FUNCTIONAL_PARSER` supplies the complete native
export set, uses synchronized index+generation instance tokens to reject stale
handles, and catches user exceptions at the C boundary.
`pj_add_parser_module(... TARGETS native wasm)` builds hidden-visibility native
and wasi-sdk 27 reactor artifacts from one source. It embeds the JSON manifest
behind the native metadata exports and in the wasm custom section. Wasm
reactors omit those two metadata exports and carry the exact JSON bytes in the
`pj_parser_module_manifest` custom section. The shared host codec appends and
reads that section; the conditional wasi-sdk 27 compile gate builds the same
toy module with C++17 and exceptions disabled, then statically audits the
reactor model and every operational export signature without executing wasm.

When `PJ_WASMER_ROOT` selects the pinned Wasmer 7.0.1 C API, the optional
`WasmParserModule` loader applies that static audit before compilation and
requires exported memory plus an empty import set. The fixture supplies its
unreachable WASI I/O fallbacks internally, so no fd, path, socket, clock,
random, environment, or scheduler capability enters the frozen v1 allow-list.
An engine-owned compiled module is instantiated in one independent store per
bound instance. Store calls may migrate between threads sequentially, but the
host must serialize calls on an instance. The runtime reacquires linear memory
after every guest call, validates every returned range, and resolves splices
against the original host payload. Per-call Wasmer instruction metering is the
enforceable execution deadline; the pinned archive exposes no public interrupt
or epoch API. Artifacts must declare a bounded memory maximum, and a separate
session tracker admits module count, file size, total claims, active instances,
and aggregate per-instance declared memory. Guest traps and metering exhaustion
join malformed descriptors and bad offsets in the contract-violation strike
path; module-reported parse errors remain strike-free data errors.

### Wasmer pin rationale (7.0.1, evaluated against 7.2.1 on 2026-08-09)

The 7.0.1 pin was re-evaluated symbol-by-symbol against the 7.2.x line:

- 7.2.x adds nothing the loader needs: `wasm_module_share/obtain` are still
  absent from the static archive, the exported metering symbol set is
  identical, and the "interruptable computation" work remains internal Rust
  surface with no public C interrupt/epoch API. The only C-API additions are
  `wasmer_features_*` toggles the loader does not require.
- The WASI-syscall CVEs fixed in 7.2.0 (unbounded host allocation in
  `getcwd`/`random_get`, `poll_oneoff`, `sock_recv`/`sock_recv_from`) are
  structurally unreachable here: the frozen empty import allow-list rejects
  any module importing those syscalls before instantiation. **Re-evaluate the
  pin before ever widening the import allow-list** — no release currently
  combines those fixes with x86_64-darwin support.
- 7.2.0 dropped the x86_64-darwin target, so moving the pin would end wasm
  parser-module support on Intel macOS while the SDK still ships x86_64
  macOS artifacts.

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
  `id`, `name`, `version`, the optional compatibility fields
  `min_sdk_required` and `min_plotjuggler_version`, family-specific fields,
  and optional metadata directly from the embedded manifest. The former is a
  concrete SemVer SDK-contract floor and the latter retains its distinct
  application-release meaning; see `REQUIREMENTS.md` section 2.1 for the full
  compatibility vocabulary and manifest rules. Broken or incompatible
  candidates are reported as diagnostics while discovery continues.
- **No more RTLD_DEEPBIND.** The loader uses `RTLD_NOW | RTLD_LOCAL`
  only (DEEPBIND was a documented ASAN/allocator-interposition trap).
  Plugin-local symbol isolation is left to `-fvisibility=hidden`.
- **Declined loader alternatives.** Admission does not use
  `RTLD_NODELETE` as its lifetime contract, `RTLD_DEEPBIND`, `dlmopen`, or a
  manifest-format change. Shared handle ownership controls lifetime, while
  candidate-file provenance is checked directly at each boot-level symbol.

Structural shape inherited from the pre-v4 design work (carries the
service registry, error out-params, and typed borrowed-dialog patterns):

- **Service registry as the sole binding mechanism.** Plugin vtables expose
  a single `bind(ctx, registry, err)` slot. The host registers all services
  (write hosts, runtime hosts, colormap, settings, etc.) under canonical
  reverse-DNS-style names (e.g. `"pj.source_write.v1"`,
  `"pj.runtime.v1"`, `"pj.parser_runtime.v1"`, `"pj.toolbox_runtime.v1"`, `"pj.colormap.v1"`,
  `"pj.settings.v1"`, `"pj.data_processors.v1"`, `"pj.source_promotion.v1"`).
  Plugins acquire only the services they use. `"pj.source_promotion.v1"`
  (optional, bound per plugin instance) lets a descriptor-import provider hand
  a materialized artifact to the host for promotion to a stock file-backed
  source — see "Plugin extension query" above and
  `pj_base/descriptor_import_protocol.h`. `"pj.data_processors.v1"` (optional) lets a toolbox create
  catalog-resident transform nodes in the host by data — a script plus
  input/output names and a params JSON blob; nothing executable crosses the
  boundary (the host owns execution). The script payload is **binary-safe**
  (`PJ_string_view_t {data,size}`), so the native "door" is WASM bytes through
  this same data-only surface (a future host-owned WASM/Python backend is purely
  additive and survives plugin unload) — deliberately *not* a C++ kernel vtable
  that would dangle on unload. The plugin sees a Qt-free
  `sdk::DataProcessorsHostView` (`createTransform`/`remove`/`list`/`recipeOf`).
  `"pj.settings.v1"` (optional) is a QSettings-like key/value store any plugin
  family can use for persistent state — the plugin sees a Qt-free
  `sdk::SettingsView` (`setValue(key, v)` returns a `Status`; reads return an
  `Expected`, e.g. `if (auto v = settings.value(key)) v->toInt(42)`, so a host
  backend fault surfaces instead of silently masquerading as a missing key); the
  host backs it (QSettings in the GUI app, JSON in a headless host) and
  namespaces keys per plugin.
  `"pj.parser_runtime.v1"` (optional) gives message parsers a non-fatal,
  non-blocking diagnostics channel. Parsers report severity, a machine-stable
  code, representative text, and an occurrence count; the host aggregates by
  parser identity and bound schema/type without changing parse success.
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
  optional static `manifest_json` tail slot for metadata-only discovery. Their
  optional runtime `set_host_info` tail slot delivers host versions and dialog
  capabilities after create/borrow and before first UI/config/widget-data use.

Service traits (`pj_base/sdk/service_traits.hpp`,
`sdk/toolbox_plugin_base.hpp`) map canonical names to their ABI type and
C++ view. `PJ::ServiceRegistryBuilder` (`pj_plugins/host/`) is the
host-side assembler that populates a `PJ_service_registry_t` from
registered services.

## 1. Three-Level Design

Every plugin family follows the same three-level pattern:

```
C ABI protocol  →  C++ SDK base class  →  Host loader + RAII handle
   (pj_base)      (pj_base/pj_plugins)       (pj_plugins)
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
    parser_functional_protocol.h  ← C ABI: pj.parser_functional.v1/v2 sinks
    parser_route_claims_protocol.h ← C ABI: exact parser route classification
    parser_module_abi.h           ← native/wasm module exports + byte codecs
    parser_module_manifest.hpp    ← wasm manifest custom-section embed/read codec
    parser_module/                ← standalone C++17 header-only module authoring kit
      module.hpp                  ← umbrella API + PJ_FUNCTIONAL_PARSER exports
      cdr_reader.hpp              ← bounded XCDR1 reader
      cdr_field_locator.hpp       ← ROS 2 .msg field-path compiler/cache
      proto_reader.hpp            ← bounded protobuf wire reader
      proto_field_locator.hpp     ← FileDescriptorSet field-path compiler
      object_writer.hpp           ← nine splice-eligible canonical object builders
    toolbox_protocol.h            ← C ABI
    plugin_data_api.h             ← shared data-plane ABI (write hosts)
    descriptor_import_protocol.h  ← C ABI: pj.descriptor_import.v1 extension +
                                     pj.source_promotion.v1 host service
                                     (family-neutral)
    builtin/
      builtin_object_codec.hpp    ← type-erased canonical-wire dispatcher
      robot_description_codec.hpp ← canonical RobotDescription codec
    sdk/
      data_source_plugin_base.hpp   ← C++ SDK
      data_source_patterns.hpp      ← FileSourceBase, StreamSourceBase
      toolbox_plugin_base.hpp
      plugin_data_api.hpp           ← C++ wrappers for data hosts
      descriptor_import.hpp         ← C++ wrappers: DescriptorImportProviderView,
                                       JoinableJob, SourcePromotionHostView

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
    plugin_catalog.hpp              ← embedded-manifest DSO scanner (scanPluginDsos / inspectPluginDso)
    parser_claim_catalog.hpp        ← parser claims, manifest admission, plugin-claim synthesis
    parser_route_resolver.hpp       ← ordered per-route selection + probe cache
    native_parser_module.hpp        ← session-lifetime native module loader
    parser_module_runtime.hpp       ← module instances, outputs, fault tracking
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
    parser_claim_catalog.cpp
    parser_route_resolver.cpp
    native_parser_module.cpp
    parser_module_runtime.cpp
    toolbox_library.cpp

cmake/
  PjParserModule.cmake             ← pj_add_parser_module native/wasm target helper
  parser_module_wasi_no_io_stubs.cpp ← closes the v1 empty wasm import set

(PlotJuggler application repo — not part of this SDK submodule)
pj_datastore/
  include/pj_datastore/
    plugin_data_host.hpp            ← DatastoreSourceWriteHost,
                                       DatastoreParserWriteHost,
                                       DatastoreToolboxHost
```

**Dependency direction:** Installed plugin authors consume
`plotjuggler_sdk::plugin_sdk`, which combines `pj_base` with the MessageParser
and dialog authoring headers from `pj_plugins`. Functional parser modules depend
only on the zero-linkage `plotjuggler_sdk::parser_module` header target. Host
libraries in `pj_plugins` depend on `pj_base`. `pj_datastore` — now a module in
the PlotJuggler application repo, not part of this SDK — provides the concrete
data-host implementations that bridge plugin writes to the columnar storage
engine.

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
1. Lexically normalizes the candidate to an absolute filesystem path, passes
   that exact path to `dlopen` (or `LoadLibraryExW` on Windows), and records
   both that spelling and its best-effort `weakly_canonical()` spelling in the
   library object for later symbol resolution.
2. Resolves the ABI marker and entry point, then verifies that each symbol's
   defining object is the candidate DSO itself rather than a dependency. On
   POSIX, an exact byte match between `dladdr().dli_fname` and either recorded
   path succeeds without re-reading the filesystem; `equivalent()` is only the
   fallback for genuinely different path spellings. On Windows, the defining
   `HMODULE` is recovered from the resolved address with
   `GetModuleHandleExW(... FROM_ADDRESS ...)` and compared to the candidate
   handle, which also rejects forwarded PE exports.
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
DataSource with an embedded dialog). These deferred lookups use the recorded
load-time paths, so they remain valid after the candidate file is removed, the
process working directory changes, or dyld reports a symlink-resolved filename.

Native functional parser modules use the same absolute-path normalization,
package-scoped platform open, and defining-module provenance checks for every
required ABI export. Their narrow path API is explicitly UTF-8 on Windows.

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

### 5.2 Parser claim catalog and route resolver

`ParserClaimCatalog` is the host-side parser dispatch catalog. It admits
transactional claim batches from module manifests or synthesized parser-plugin
coverage, validates stable `(provider_id, claim_id)` identities, and attaches
host-supplied provenance and provider generations. The SDK-owned encoding
registry and normalization helpers keep matching case-sensitive and canonical.

`ParserRouteResolver` independently selects scalar and object providers. It
applies fail-closed per-route pins, exact-before-wildcard specificity,
provenance, bounded priority, and stable identity ordering before invoking a
caller-supplied probe. Probe decisions and retained opaque leases are cached by
provider generation plus binding identity/config digests. Catalog, pin, and
provider-config mutation paths explicitly invalidate that cache. The resolver
contains no loader or executor: the embedding host runs the callback on its
parser-control executor and owns the concrete provider instance type.

## 6. RAII Handles

Each family has a move-only RAII handle:

- Constructor calls `vt->create()` to allocate the plugin instance.
- Destructor calls `vt->destroy(ctx)`.
- Handles created by a loader retain a shared DSO owner; destroying or
  hot-reloading the loader/catalog entry cannot `dlclose` the plugin while
  live handles still call its vtable. `DataSourceHandle` exposes this token via
  `libraryOwner()` so it can be captured anywhere plugin code may outlive the
  handle — e.g. a lazy `ObjectStore` payload anchor whose `release` fn lives in
  the plugin `.so` — keeping the DSO mapped until that captor is gone too.
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
1. After create/borrow, call DialogHandle::setHostInfo()
2. Read widget_data() from plugin → JSON
3. Parse JSON into WidgetDataView
4. Apply WidgetDataView to host widgets
5. Wait for user interaction or tick timer
6. On widget signal → build event JSON → call on_widget_event()
7. If returns true → goto 2 (re-read widget_data)
8. On tick timer → call on_tick()
9. If returns true → goto 2
10. On accept → call on_accepted(final_state_json)
11. On reject → call on_rejected()
```

`DialogHandle::setHostInfo()` is the only runtime layer that inspects the
optional vtable slot: it gates the read with `PJ_HAS_TAIL_SLOT`, and concrete
dialog/panel engines do not access the field directly. Its tri-state result
distinguishes an absent pre-0.21 slot (`Unsupported`, with no plugin call) from
a plugin accepting or rejecting the information (`Accepted`/`Rejected`). A
rejected call may populate `PJ_error_t`, but the C contract does not require it.

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

The v4 DataSource runtime host adds a tail slot `push_message`
(offset 88 in `PJ_data_source_runtime_host_vtable_t`) that takes a
deferred byte-fetch callable instead of bytes:

```c
typedef struct PJ_message_data_fetcher_t {
  void* ctx;
  bool  (*fetchMessageData)(void* ctx, PJ_payload_t* out, PJ_error_t* err) PJ_NOEXCEPT;
  void  (*release)(void* ctx);
} PJ_message_data_fetcher_t;

bool (*push_message)(
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

Parsers participate through `classifySchema`, `parseScalars`, and
`parseObject`, backed by the per-schema `SchemaHandler` table. Those C++
methods and their `ScalarRecord` / `ObjectRecord` / `std::any` values stay
inside the plugin DSO. The base exposes both `pj.parser_functional.v1` and v2;
`MessageParserHandle` negotiates v2 first and falls back to v1:

- `parse_scalars` calls one caller-owned sink exactly once. Field/name/string
  views are borrowed only for that callback; the host copies anything it
  retains.
- `parse_object` accepts `PJ_payload_t`, taking one ownership-anchor reference
  and releasing it on every path. That preserves the existing zero-copy input
  into parsers that propagate `PayloadView::anchor`. It serializes the plugin's
  concrete builtin to its canonical `PJ.*` protobuf wire contract and calls one
  object sink with `(BuiltinObjectType, optional timestamp, bytes)`. The v2 sink
  may instead carry one eligible bulk field as a payload-relative splice.
- `MessageParserHandle::parseObjectFunctional` decodes those bytes before the
  callback returns, reconstructs a v2 splice into the canonical object, and
  rejects a type different from the binding's expected object type. The result
  is an entirely host-owned `ObjectRecord`; its destructor and `std::any`
  manager contain no plugin function pointer, so the value remains safe after
  the parser and DSO lease are destroyed.
- Provider exceptions, consumer exceptions, malformed/undersized sinks,
  unknown object tags, missing calls, and duplicate calls fail closed through
  `PJ_error_t`. The built-in extension table and trampolines have DSO-local
  symbol visibility so ELF `STB_GNU_UNIQUE`/interposition cannot make one
  plugin borrow another plugin's table.

This deliberately pays one canonical serialization at the DSO boundary and a
host-side decode. It avoids the more fragile alternative of sharing STL object
layouts, allocators, destructors, or release callbacks for each concrete C++
type. Large input buffers need not be copied before parsing when the host has a
`PayloadView` anchor. Benchmark the serialize/decode cost for image and point
cloud workloads before removing the deprecated fallback; a future optimization
must preserve the same C ownership boundary rather than reintroduce C++ object
sharing.

`serializeBuiltinObject` / `deserializeBuiltinObject` dispatch every stable
builtin type, including the newly canonicalized `RobotDescription`. A
zero-length canonical payload is accepted as the valid proto3 default message
when the separate type tag is known. Concrete builtins and codecs live under
`pj_base/builtin/`; see `docs/builtin_type.md` for the catalog.

Pre-0.21 parsers expose no functional extension, and handler-based 0.21 parsers
may expose v1 without v2. The host keeps a clearly isolated, deprecated
direct-C++ bridge for binaries with neither extension. New plugins and new host
code use v2-first negotiation; SDK 1.0 can remove the bridge and the frozen
`MessageParserPluginBase` layout constraint.

## Per-topic pause (demand-driven subscription)

`kCapabilityPerTopicPause` (`1 << 6`) lets a multi-topic streaming source
expose its full topic universe cheaply while only transmitting data for
topics the host is actually displaying, instead of an all-or-nothing
whole-source pause. Two independent additions, both `struct_size`/
`PJ_HAS_TAIL_SLOT`-gated so an old plugin or an old host degrades cleanly:

- **Plugin → host advertise.** A second tail slot on the runtime host,
  `notify_available_topics(ctx, topics, count, out_error)` (offset 96,
  growing `sizeof(PJ_data_source_runtime_host_vtable_t)` 96 → 104), carrying
  `PJ_available_topic_t{topic_name, parser_encoding, type_name, schema}` —
  the same parser-identifying fields as `PJ_parser_binding_request_t` minus
  `parser_config_json` (not yet known pre-subscription), so the host can
  a-priori `classify_schema` every topic with no data flowing. Every call
  carries the **full current set** (declarative, not a delta), entered from
  the plugin's poll/stream thread. `DataSourceRuntimeHostView::notifyAvailableTopics`
  returns an error on a host that lacks the slot, so a new plugin can detect
  an old host and fall back to subscribing its preselected set at `start()`.
- **Host → plugin control.** The first stable (`pj.<name>.v1`) instance of
  the §0a CLAP-style `get_plugin_extension` mechanism:
  `"pj.topic_subscription.v1"` → `PJ_topic_subscription_v1_t::set_active_topics(ctx,
  names, count, out_error)`. Also declarative-full-set, entered from the
  host's GUI thread; the plugin's expected implementation is a mutex-protected
  latest-wins slot drained on its own poll thread (not a command queue — the
  host may call it faster than the plugin can act, and only the most recent
  call matters). `DataSourceHandle::setActiveTopics` is a no-op when the
  plugin does not expose the extension.

See `docs/data-source-guide.md` → "Per-topic pause (demand-driven
subscription)" for the plugin-author walkthrough.
