# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation libraries for PlotJuggler storage, plugin SDKs, and host-side plugin loading.

### Modules

- **pj_base** — vocabulary types, plugin ABI headers, plugin SDK headers (zero external deps)
- **pj_datastore** — columnar storage engine + `ObjectStore` (for media blobs) + `DerivedEngine` (fmt, tsl::robin_map, nanoarrow)
- **pj_plugins** — C-ABI plugin protocol, C++ SDK base classes, plugin discovery, host-side loaders, config envelope helpers, and dialog protocol primitives; four plugin families: DataSource, MessageParser, Dialog, Toolbox
- **pj_scene_protocol** — canonical schema + Foxglove `ImageAnnotations` Protobuf codec (writer + reader); SDK boundary for plugin authors producing or consuming 2D markers / scene primitives. `pj_base`-only deps.

### Dependency graph

- `pj_datastore` → `pj_base`
- `pj_plugins` → `pj_base`
- `pj_scene_protocol` → `pj_base`

## Key Documentation

**Project-wide:**

| Document | Content |
|----------|---------|
| `docs/cpp_design_recommendations.md` | C++ style, error handling, API design guidelines |
| `docs/toolbox-porting-gap-analysis.md` | SDK gaps identified when porting PJ3 toolboxes (being addressed) |

**Datastore** (`pj_datastore/docs/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Goals, data model, ingest contract, schema evolution (dynamic columns, variable-length sequences), query, derived series |
| `ARCHITECTURE.md` | Internals: domain model, layers, data flow, encoding (constant, FoR, dictionary, packed bool), DerivedEngine |
| `USER_GUIDE.md` | Plugin author's guide: write patterns (named vs bound vs Arrow IPC), read API, pitfalls, ValueRef, TypedNull |
| `OBJECT_STORE_DESIGN.md` | ObjectStore: lazy-fetch media blobs, retention, concurrent access |

**Plugin system** (`pj_plugins/docs/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Plugin families (DataSource, MessageParser, Dialog, Toolbox), interaction model, capability system, config contract |
| `ARCHITECTURE.md` | C ABI protocols, SDK base classes, host loaders, RAII handles, dialog protocol, config envelope |
| `data-source-guide.md` | SDK tutorial: FileSourceBase, StreamSourceBase, delegated ingest, dialog integration |
| `message-parser-guide.md` | SDK tutorial: parse(), schema binding, dialog integration for parsers |
| `dialog-plugin-guide.md` | SDK tutorial: WidgetData, typed events, EmbedUi, requestAccept, onTick |
| `toolbox-guide.md` | SDK tutorial: read+write access, catalog, notifyDataChanged |

**Scene protocol** (`pj_scene_protocol/docs/`):

| Document | Content |
|----------|---------|
| `ARCHITECTURE.md` | Wire format spec (`foxglove.ImageAnnotations` Protobuf), type catalog, encoding rules, design rationale (single canonical decoder, loader-side conversion) |
| `USER_GUIDE.md` | Producer recipe (loader writing markers) and consumer recipe (sink/viewer decoding), common pitfalls, pointer to PJ4 reference adapters |

## Build & Test

```bash
./build.sh            # RelWithDebInfo (build/)
./build.sh --debug    # Debug + ASAN (build/debug_asan)
./test.sh             # runs tests in all discovered build dirs
```

Dependencies: Conan (`conanfile.txt`).

## Pre-commit Validation

Before committing, always run:

```bash
./build.sh --debug && ./test.sh && ./run_clang_tidy.sh
```

## Instructions Glossary

- **"Read all documentation"** means: find and read every `.md` file in the entire project tree (all subdirectories). Use `find . -name "*.md"` or equivalent. This includes docs in `pj_base/`, `pj_datastore/docs/`, `pj_plugins/docs/`, `pj_scene_protocol/docs/`, and any other location.

- **"Update the documentation"** means: based on what you learned during this session, correct any documentation that is outdated or inaccurate, and clarify any ambiguity that caused confusion or errors. If a doc says one thing but the code does another, fix the doc to match reality. If missing information led to a bug, add it.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit
- **Naming:** `CamelCase` classes, `camelBack` functions, `lower_case` variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** flat `PJ` namespace; `PJ::encoding` and `PJ::arrow_import` for internals
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants
- **Warnings:** `-Wall -Wextra -Werror` on all targets; pre-commit hooks enforce clang-format v17
