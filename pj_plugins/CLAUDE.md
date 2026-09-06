# pj_plugins — plugin ABI, SDK base classes, and host-side loaders

Before adding a helper, use [Existing SDK utilities](../docs/sdk-utilities.md).
Provider work also follows [the provider contract](../docs/provider-guide.md).

`pj_plugins` owns host loaders, discovery, parser routing and the MessageParser
and Dialog authoring APIs. The `plotjuggler_sdk::plugin_sdk` umbrella combines
those headers with `pj_base`. Compiled providers also link `source`.

Host libraries depend on `pj_base`, never Qt. Datastore bridges and duplicate
plugin-resolution policy belong to the application. Follow the root reading
order, then read this file or [pj_base/CLAUDE.md](../pj_base/CLAUDE.md) as relevant.

## Layout
- `include/pj_plugins/host/` — DataSource/MessageParser/Toolbox loaders and RAII
  handles. `plugin_catalog` scans embedded manifests (`scanPluginDsos` /
  `inspectPluginDso`). `ParserClaimCatalog` and `ParserRouteResolver` handle
  parser claim admission and per-route resolution. Native parser-module
  loading/execution uses `NativeParserModule`, `NativeParserModuleInstance`
  and `ParserModuleStrikeTracker`. Also contains `ServiceRegistryBuilder`
  and `ConfigEnvelope`.
  The DSO catalog that resolves duplicates and composes loaded plugin families
  is **host policy**. It lives in the app (`pj_runtime`, `PluginRuntimeCatalog`).
- `include/pj_plugins/sdk/` — SDK pieces that live here, not in `pj_base`:
  `MessageParserPluginBase`, ingest/timestamp/array policies, streaming source
  and dialog helpers, endpoint composition, and parser trampolines.
- `include/pj_plugins/testing/` — `ToolboxTestStore` (fake Arrow host) and `DelegatedIngestFixture`
  (toolbox/data-source hosts recording attachment, payload ownership, completion, Stop and discard).
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
  parser/dialog authoring and shared plugin policies live under
  `pj_plugins/sdk/`. Functional parser-module authoring headers and the native
  CMake helper live under `pj_base`; claim resolution and native module
  loading/runtime live here.
- **Handles keep the DSO mapped.** Every handle holds a `shared_ptr<void>`
  library token, exposed via `libraryOwner()`. Destroying or hot-reloading the
  loader therefore cannot `dlclose` a live plugin. A lazy ObjectStore payload
  anchor can capture the token to stay safe after the handle dies; its `release`
  function is plugin code. Dialog handles provide non-owning `borrowed()` handles
  for source/toolbox embedded dialogs. These must not outlive the owning handle.
- **Native parser modules never unload in v1.** `NativeParserModule` resolves
  the complete per-handle export set and retains every opened DSO for the
  process session, including rejected artifacts. Instance wrappers still call
  `pj_module_destroy`. Only the code mapping has session lifetime.

## Read deeper
| For | Read |
|---|---|
| Existing helpers and provider/testing APIs | [SDK utilities](../docs/sdk-utilities.md) |
| Source-provider lifecycle and delegated-ingest tests | [Provider contract](../docs/provider-guide.md) |
| Family roles, capabilities, permission matrix, config contract | `docs/REQUIREMENTS.md` |
| ABI rules, three-level design, loaders, RAII, data-host bridge | `docs/ARCHITECTURE.md` |
| Writing each family | `docs/data-source-guide.md`, `docs/message-parser-guide.md`, `docs/toolbox-guide.md`, `docs/dialog-plugin-guide.md` |
| Host loader + factory pattern | `include/pj_plugins/host/data_source_library.hpp`, `…/data_source_handle.hpp` |
| Discovery from embedded manifests | `include/pj_plugins/host/plugin_catalog.hpp` (the duplicate-resolution catalog is host-side in `pj_runtime`) |
| Parser claim admission and route selection | `include/pj_plugins/host/parser_claim_catalog.hpp`, `parser_route_resolver.hpp` |
| Native functional parser modules | `include/pj_plugins/host/native_parser_module.hpp`, `parser_module_runtime.hpp` |
| Authoring native functional parser modules | `../pj_base/include/pj_base/parser_module/README.md`, `module.hpp`, `../.claude/skills/plotjuggler-plugin/references/parser-module.md` |
| Service wiring into `bind()` | `include/pj_plugins/host/service_registry_builder.hpp` |
| Builtin-object ingest policy | `include/pj_plugins/sdk/object_ingest_policy.hpp` |
