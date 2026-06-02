# CLAUDE.md

## Project Overview

PlotJuggler SDK — C++20 foundation libraries that make up the PlotJuggler plugin SDK and host-side
plugin loading. **Read-only submodule** inside PJ4: consumed as-is; changes happen in this repo,
not in the PJ4 superproject. This file is the single navigation node for the whole submodule — the
two modules below have no own CLAUDE.md.

> The columnar storage engine (`pj_datastore`) used to live here. It now lives in the PlotJuggler
> application repo as a top-level module: plugins reach storage only through the C ABI defined in
> `pj_base` (the host-side write implementations are not part of the SDK), so the engine does not
> belong in the plugin SDK.

### Modules

- **pj_base** — vocabulary types (`Timestamp`, `DatasetId`, `Expected<T>`, `Span<T>`, type trees),
  the canonical builtin object vocabulary (`pj_base/builtin/`: 15 struct headers — Image, DepthImage,
  PointCloud, CompressedPointCloud, OccupancyGrid(+Update), Mesh3D, VideoFrame, AssetVideo,
  SceneEntities, RobotDescription, CameraInfo, Log, ImageAnnotations, FrameTransforms) and their 14
  wire codecs (RobotDescription carries source text as-is — no codec), the C-ABI protocol headers for
  DataSource/MessageParser/Toolbox + the C++ SDK base classes / host-view helpers built on them.
- **pj_plugins** — host-side loaders + RAII handles + plugin discovery/catalog for four plugin
  families (DataSource, MessageParser, Dialog, Toolbox), config-envelope helpers, and the **dialog
  C ABI** (`pj_plugins/dialog_protocol/`). Note the split: the DataSource/MessageParser/Toolbox C-ABI
  protocol headers live in `pj_base`; the **Dialog** protocol header lives here, not in `pj_base`.

### Dependency graph

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

The version is a **plugin-compatibility contract** (plugins pin this SDK by Conan range), not
decoration. In every PR, proactively raise whether a release is warranted and propose the bump.
The bump is decided by **plugin impact**, semver-style:

- **MAJOR** (`X.0.0`) — an **ABI or API break**: an existing plugin must be recompiled or its
  source changed to keep working. Removing/reordering ABI vtable slots, changing a struct layout or
  an existing function signature, bumping a `PJ_*_PROTOCOL_VERSION`, or changing a canonical builtin
  object schema / `proto` wire format. **`abi/baseline.abi` changes only on a MAJOR.**
- **MINOR** (`x.Y.0`) — a **backward-compatible API addition**: a new capability is added, but every
  already-built plugin keeps working **with no recompile**. New entry points are tail-appended and
  the host only calls slots an old plugin actually provides (gated by `struct_size`), so an old
  `.so` loaded into a newer host is unaffected — it simply ignores what it does not use. `abidiff`
  against the baseline must show **additions only**.
- **PATCH** (`x.y.Z`) — backward-compatible **bug fixes** to installed headers/behavior. Changes
  invisible to consumers (docs, CLAUDE.md, comments, tests, internal `.cpp` that does not alter an
  installed header) take **no bump**.

**Plugin compatibility range.** A plugin built and tested on `X.Y.Z` works on every later
MINOR/PATCH up to the next MAJOR, so it pins `plotjuggler_sdk/[>=X.Y.Z <(X+1).0.0]` — e.g. built on
`1.4.2` → `[>=1.4.2 <2.0.0]`. The lower bound is the version that introduced the newest feature the
plugin actually uses (a plugin that does not adopt `0.6`'s additions stays at `>=0.5.2`); the upper
bound is the next MAJOR. Write the range **explicitly** — do not rely on caret/tilde shorthand.

**While pre-1.0 (currently `0.y.z`).** Same rules, with the major being `0`: there are **no breaking
changes within `0.x`** — the next ABI/API break ships as `1.0.0`. So a plugin built on `0.Y.Z` pins
`[>=0.Y.Z <1.0.0]`. (Deliberately stricter than the usual "0.x may break" convention, because
plugins pin against this SDK.)

**Mechanics.** The version lives in two places that must stay in sync — `version` in `conanfile.py`
and `PJ_PACKAGE_VERSION` in the root `CMakeLists.txt` (also update the example tag in the
`conanfile.py` docstring). A non-MAJOR PR must not alter `abi/baseline.abi` beyond additions (verify
with `abidiff`). Tagging and pushing a release is a separate, explicitly-authorized step — never tag
or push a release without the user's go-ahead.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit.
- **Naming:** `CamelCase` classes, `camelBack` functions, `lower_case` variables, `lower_case_`
  members, `kCamelCase` constants.
- **Namespaces:** flat `PJ`; `PJ::encoding` and `PJ::arrow_import` for internals.
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants.
- **Warnings:** `-Wall -Wextra -Werror` on all targets.

## Instructions Glossary

- **"Read all documentation"** — read every `.md` in the tree (`find . -name '*.md'`), including
  `docs/` and `pj_plugins/docs/`.
- **"Update the documentation"** — correct any doc made outdated/inaccurate this session; if a doc
  disagrees with code, fix the doc to match reality; add info whose absence caused a bug.
- **"Check documentation"** — review the docs related to the changed module/API; confirm they still
  describe current intent and behavior, else update or ask before committing.
