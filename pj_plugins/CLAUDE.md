# pj_plugins — plugin ABI, SDK base classes, and host-side loaders

The runtime-extension layer of `plotjuggler_sdk`: the stable C ABI, the C++ SDK
plugin authors subclass, and the host-side loaders/RAII handles that `dlopen`
plugin DSOs. Owns **four plugin families** — DataSource, MessageParser, Toolbox,
Dialog. Plugins depend only on `pj_base`; this module (the host side) links
`pj_base` and is consumed by the app. It does **not** own the data-plane bridge
(that is `pj_datastore`'s `DatastoreSourceWriteHost` / `…ParserWriteHost` /
`…ToolboxHost`, which now lives in the PlotJuggler application repo, not in this
SDK) and links **no Qt** — dialogs are toolkit-neutral (the GUI host supplies the
renderer). The submodule's read-path is `plotjuggler_sdk/CLAUDE.md` → this file
→ `docs/` → headers → code (the PJ4 per-module-CLAUDE contract does not govern
submodule-internal modules; `pj_base` carries none).

## Layout
- `include/pj_plugins/host/` — host loaders + RAII handles for DataSource /
  MessageParser / Toolbox, the embedded-manifest `plugin_catalog` scanner
  (`scanPluginDsos` / `inspectPluginDso`), parser claim admission + per-route
  resolution (`ParserClaimCatalog`, `ParserRouteResolver`), native and Wasmer
  functional parser-module loading/execution (`NativeParserModule`,
  `WasmParserModule`, their instance wrappers, `ParserModuleStrikeTracker`),
  `ServiceRegistryBuilder`, `ConfigEnvelope`. The DSO duplicate-resolution
  catalog that composes loaded plugin families into a set is **host policy** and
  lives in the app (`pj_runtime`, `PluginRuntimeCatalog`), not here.
- `include/pj_plugins/sdk/` — SDK pieces that live here, not in `pj_base`:
  `MessageParserPluginBase`, `ObjectIngestPolicyResolver`, parser trampolines.
- `include/pj_plugins/testing/` — `ToolboxTestStore` (fake Arrow host for tests).
- `dialog_protocol/` — **nested module** (own CMake): the Dialog C ABI, C++
  dialog SDK, and host dialog loader/handle. See `dialog_protocol/CLAUDE.md`.
- `src/` — loader/catalog `.cpp`; `src/detail/` vtable validation + dlopen.
- `examples/` — mock plugins exercised by tests (`mock_data_source`, …).
- `tests/` — host-side loader + lifecycle tests.

## Gotchas
- **Protocol v4 under boot-ABI v5.** All four family vtables are
  `PROTOCOL_VERSION == 4`; the DSO-level `pj_plugin_abi_version` symbol is
  `PJ_ABI_VERSION == 5`. New slots are tail-appended and read via
  `PJ_HAS_TAIL_SLOT` — never grow `*_MIN_VTABLE_SIZE`. See `docs/ARCHITECTURE.md` §0a.
- **The SDK is split across two modules.** `DataSourcePluginBase` /
  `ToolboxPluginBase` / `data_source_patterns.hpp` live in **`pj_base/sdk/`**;
  only `MessageParserPluginBase` + `object_ingest_policy.hpp` live here under
  `pj_plugins/sdk/`. (The `docs/ARCHITECTURE.md` §2 diagram is stale on this.)
- **Handles keep the DSO mapped.** Every handle holds a `shared_ptr<void>`
  library token (exposed via `libraryOwner()`), so destroying/hot-reloading the
  loader cannot `dlclose` a live plugin — and a lazy ObjectStore payload anchor,
  whose `release` fn is plugin code, can capture that token to stay safe past the
  handle's own lifetime. Dialog handles add a non-owning `borrowed()` form for
  source/toolbox embedded dialogs — those must not outlive the owning handle.
- **Native parser modules never unload in v1.** `NativeParserModule` resolves
  the complete per-handle export set and retains every opened DSO for the
  process session, including rejected artifacts. Instance wrappers still call
  `pj_module_destroy`; only the code mapping has session lifetime.
- **Wasm parser modules have an empty import allow-list in v1.** The loader
  admits reactors with the exact operational exports, `_initialize`, exported
  memory with a bounded declared maximum, no start function, and no imports.
  One engine-owned compiled Wasmer module creates independent stores per
  instance. Sequential cross-thread use is supported, but overlapping calls on
  one instance are forbidden and must be serialized by the application host.
  Wasmer metering is reset for every ABI call; exhaustion is a contract strike.
  The pinned static archive has no public interrupt/epoch API, and native stack
  depth uses Wasmer's guarded default.

## Read deeper
| For | Read |
|---|---|
| Family roles, capabilities, permission matrix, config contract | `docs/REQUIREMENTS.md` |
| ABI rules, three-level design, loaders, RAII, data-host bridge | `docs/ARCHITECTURE.md` |
| Writing each family | `docs/data-source-guide.md`, `docs/message-parser-guide.md`, `docs/toolbox-guide.md`, `docs/dialog-plugin-guide.md` |
| Host loader + factory pattern | `include/pj_plugins/host/data_source_library.hpp`, `…/data_source_handle.hpp` |
| Discovery from embedded manifests | `include/pj_plugins/host/plugin_catalog.hpp` (the duplicate-resolution catalog is host-side in `pj_runtime`) |
| Parser claim admission and route selection | `include/pj_plugins/host/parser_claim_catalog.hpp`, `parser_route_resolver.hpp` |
| Native and wasm functional parser modules | `include/pj_plugins/host/native_parser_module.hpp`, `parser_module_runtime.hpp`, `wasm_parser_module.hpp`, `wasm_parser_module_runtime.hpp` |
| Authoring functional parser modules | `../pj_base/include/pj_base/parser_module/README.md`, `module.hpp` |
| Service wiring into `bind()` | `include/pj_plugins/host/service_registry_builder.hpp` |
| Builtin-object ingest policy | `include/pj_plugins/sdk/object_ingest_policy.hpp` |
