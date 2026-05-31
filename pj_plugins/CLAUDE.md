# pj_plugins — plugin ABI, SDK base classes, and host-side loaders

The runtime-extension layer of `plotjuggler_core`: the stable C ABI, the C++ SDK
plugin authors subclass, and the host-side loaders/RAII handles that `dlopen`
plugin DSOs. Owns **four plugin families** — DataSource, MessageParser, Toolbox,
Dialog. Plugins depend only on `pj_base`; this module (the host side) links
`pj_base` and is consumed by the app. It does **not** own the data-plane bridge
(that is `pj_datastore`'s `DatastoreSourceWriteHost` / `…ParserWriteHost` /
`…ToolboxHost`) and links **no Qt** — dialogs are toolkit-neutral (the GUI host
supplies the renderer). The submodule's read-path is `plotjuggler_core/CLAUDE.md`
→ this file → `docs/` → headers → code (the PJ4 per-module-CLAUDE contract does
not govern submodule-internal modules; `pj_base`/`pj_datastore` carry none).

## Layout
- `include/pj_plugins/host/` — host loaders + RAII handles for DataSource /
  MessageParser / Toolbox, the `PluginRuntimeCatalog`, embedded-manifest
  `plugin_catalog` scanner, `ServiceRegistryBuilder`, `ConfigEnvelope`.
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
  library token, so destroying/hot-reloading the loader cannot `dlclose` a live
  plugin. Dialog handles add a non-owning `borrowed()` form for source/toolbox
  embedded dialogs — those must not outlive the owning handle.

## Read deeper
| For | Read |
|---|---|
| Family roles, capabilities, permission matrix, config contract | `docs/REQUIREMENTS.md` |
| ABI rules, three-level design, loaders, RAII, data-host bridge | `docs/ARCHITECTURE.md` |
| Writing each family | `docs/data-source-guide.md`, `docs/message-parser-guide.md`, `docs/toolbox-guide.md`, `docs/dialog-plugin-guide.md` |
| Host loader + factory pattern | `include/pj_plugins/host/data_source_library.hpp`, `…/data_source_handle.hpp` |
| Discovery from embedded manifests | `include/pj_plugins/host/plugin_catalog.hpp`, `…/plugin_runtime_catalog.hpp` |
| Service wiring into `bind()` | `include/pj_plugins/host/service_registry_builder.hpp` |
| Builtin-object ingest policy | `include/pj_plugins/sdk/object_ingest_policy.hpp` |
