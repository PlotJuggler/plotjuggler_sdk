# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation libraries for PlotJuggler storage, plugin SDKs, and host-side
plugin loading. **Read-only submodule** inside PJ4: consumed as-is; changes happen in this repo,
not in the PJ4 superproject. This file is the single navigation node for the whole submodule — the
three modules below have no own CLAUDE.md.

### Modules

- **pj_base** — vocabulary types (`Timestamp`, `DatasetId`, `Expected<T>`, `Span<T>`, type trees),
  the canonical builtin object vocabulary (`pj_base/builtin/`: 15 struct headers — Image, DepthImage,
  PointCloud, CompressedPointCloud, OccupancyGrid(+Update), Mesh3D, VideoFrame, AssetVideo,
  SceneEntities, RobotDescription, CameraInfo, Log, ImageAnnotations, FrameTransforms) and their 14
  wire codecs (RobotDescription carries source text as-is — no codec), the C-ABI protocol headers for
  DataSource/MessageParser/Toolbox + the C++ SDK base classes / host-view helpers built on them.
- **pj_datastore** — columnar storage engine (`DataEngine`) + `ObjectStore` (media/opaque blobs) +
  `DerivedEngine` (fmt, tsl::robin_map, nanoarrow). Plugin-data host implementations live here.
- **pj_plugins** — host-side loaders + RAII handles + plugin discovery/catalog for four plugin
  families (DataSource, MessageParser, Dialog, Toolbox), config-envelope helpers, and the **dialog
  C ABI** (`pj_plugins/dialog_protocol/`). Note the split: the DataSource/MessageParser/Toolbox C-ABI
  protocol headers live in `pj_base`; the **Dialog** protocol header lives here, not in `pj_base`.

### Dependency graph

- `pj_datastore` → `pj_base` (+ fmt, nanoarrow)
- `pj_plugins` → `pj_base` (+ nlohmann/json)

## Read path

```text
this CLAUDE.md -> relevant docs -> headers -> code
```

Start here to pick the module + source-of-truth doc. Read the doc before treating code as
authoritative for *intent*: code shows current implementation; docs define intended architecture,
public contracts, terminology, and module boundaries. If docs and code disagree, that is a
documentation bug — do not silently let stale docs survive. Any change to behavior, public APIs,
ABI structs, SDK types, module ownership, plugin workflows, or storage formats must include a
documentation check before commit.

## Key Documentation

**Project-wide** (`docs/`):

| Document | Content |
|----------|---------|
| `docs/builtin_type.md` | Canonical builtin object types — the shim between third-party schemas and PJ internals; lists every builtin + its codec |
| `docs/image_annotations_format.md` | Canonical `PJ.ImageAnnotations` wire format |
| `docs/dialog-sdk-reference.md` | Quick reference for `WidgetData` setters + `DialogPluginTyped` event handlers |
| `docs/cpp_design_recommendations.md` | C++ style, error handling, API design guidelines |
| `docs/toolbox-porting-gap-analysis.md` | Historical PJ3→PJ4 toolbox SDK gap analysis (most gaps now closed; read as context, not current reference) |
| `V4_STORE.md` | ObjectStore plugin ABI: services, ownership rules, lazy fetch |

**Datastore** (`pj_datastore/docs/`): `REQUIREMENTS.md` (data model, ingest contract, schema
evolution, query) · `ARCHITECTURE.md` (domain model, layers, encoding, DerivedEngine) ·
`USER_GUIDE.md` (plugin-author write/read patterns, ValueRef, TypedNull) ·
`OBJECT_STORE_DESIGN.md` (lazy-fetch blobs, retention).

**Plugin system** (`pj_plugins/docs/`): `REQUIREMENTS.md` (families, capability system, config
contract) · `ARCHITECTURE.md` (C ABI protocols, SDK base classes, host loaders, dialog protocol) ·
`data-source-guide.md` · `message-parser-guide.md` · `dialog-plugin-guide.md` · `toolbox-guide.md`.

## Build & Test

```bash
./build.sh            # RelWithDebInfo (build/)
./build.sh --debug    # Debug + ASAN (build/debug_asan)
./test.sh             # runs tests in all discovered build dirs
```

Dependencies come from Conan (`conanfile.py`). Before committing always run
`./build.sh --debug && ./test.sh`. Formatting/linting (clang-format, pinned to v22.1.0 in `.pre-commit-config.yaml`) is enforced by
pre-commit hooks. Verify docs match reality before any commit that changes behavior, public APIs, ABI structs,
SDK types, or storage formats; if stale and not asked to update, ask before committing.

## Release Versioning

In every PR, proactively raise whether a new Conan release is warranted and propose the bump.
Pre-1.0 (`0.MINOR.PATCH`): **MINOR** = any API/ABI break (removing/reordering ABI vtable slots,
changing struct layouts or signatures, source-incompatible SDK change); **PATCH** =
backward-compatible (tail-appended ABI slots gated by `struct_size`, additive helpers, fixes, docs).
Version is declared in `version` (`conanfile.py`) and `PJ_PACKAGE_VERSION` (root `CMakeLists.txt`) —
keep them in sync, and update the example tag in the `conanfile.py` docstring. Tagging/pushing a
release is a separate, explicitly-authorized step.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit.
- **Naming:** `CamelCase` classes, `camelBack` functions, `lower_case` variables, `lower_case_`
  members, `kCamelCase` constants.
- **Namespaces:** flat `PJ`; `PJ::encoding` and `PJ::arrow_import` for internals.
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants.
- **Warnings:** `-Wall -Wextra -Werror` on all targets.

## Instructions Glossary

- **"Read all documentation"** — read every `.md` in the tree (`find . -name '*.md'`), including
  `docs/`, `pj_datastore/docs/`, `pj_plugins/docs/`.
- **"Update the documentation"** — correct any doc made outdated/inaccurate this session; if a doc
  disagrees with code, fix the doc to match reality; add info whose absence caused a bug.
- **"Check documentation"** — review the docs related to the changed module/API; confirm they still
  describe current intent and behavior, else update or ask before committing.

