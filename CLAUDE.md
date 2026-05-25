# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation libraries for PlotJuggler storage, plugin SDKs, and host-side plugin loading.

### Modules

- **pj_base** — foundational SDK/ABI types, canonical builtin object vocabulary, plugin ABI headers,
  host-view helpers, ImageAnnotations codec, and shared utilities
- **pj_datastore** — columnar storage engine + `ObjectStore` (for media blobs) + `DerivedEngine` (fmt, tsl::robin_map, nanoarrow)
- **pj_plugins** — C++ plugin SDK base classes, plugin discovery, host-side loaders, config envelope
  helpers, and dialog protocol primitives; four plugin families: DataSource, MessageParser, Dialog, Toolbox

### Dependency graph

- `pj_datastore` → `pj_base`
- `pj_plugins` → `pj_base`

## Documentation Workflow

Coding agents should use this context hierarchy:

```text
CLAUDE.md -> relevant docs -> code
```

- Start with this file to identify the module and the relevant source-of-truth documents.
- Read the relevant docs before treating code as authoritative for intent. Code shows current implementation
  details; docs define the intended architecture, public contracts, terminology, and module boundaries.
- If docs and code disagree, treat that as a documentation consistency problem. Do not silently let stale docs survive.
- Any change to behavior, public APIs, ABI structs, SDK types, module ownership, plugin workflows,
  storage formats, or user-facing semantics must include a documentation check.
- Before any commit, verify that documentation is up to date for the change. If docs are stale and the user did
  not explicitly ask for a docs update, ask whether to update the docs before committing. Do not commit
  known-stale documentation.

## Key Documentation

**Project-wide:**

| Document | Content |
|----------|---------|
| `docs/builtin_type.md` | Canonical builtin object types used as the shim between third-party schemas and PlotJuggler internals |
| `docs/image_annotations_format.md` | Canonical `PJ.ImageAnnotations` wire format for builtin `ImageAnnotations` payloads |
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

## Build & Test

```bash
./build.sh            # RelWithDebInfo (build/)
./build.sh --debug    # Debug + ASAN (build/debug_asan)
./test.sh             # runs tests in all discovered build dirs
```

Dependencies: Conan (`conanfile.txt`).

## Pre-commit Validation

Before committing, first check whether the code changes require documentation updates. If documentation is stale
and the requested task did not include updating it, ask the user whether to update the docs before committing.

Before committing, always run:

```bash
./build.sh --debug && ./test.sh
```

Code formatting and linting are enforced via pre-commit hooks (clang-format v17).

## Release Versioning

In **every PR**, proactively raise whether it warrants a new Conan release, and
propose the version bump rather than waiting to be asked. Pre-1.0 versioning
convention (`0.MINOR.PATCH`):

- **MINOR** bump (`0.X.0`) — any API or ABI **break**: removing/reordering ABI
  vtable slots, changing existing struct layouts or function signatures, or any
  source-incompatible SDK change.
- **PATCH** bump (`0.x.Y`) — **backward-compatible** changes: tail-appended ABI
  slots (gated by `struct_size`), additive SDK helpers, bug fixes, docs.

The version is declared in two places that **must stay in sync**: `version` in
`conanfile.py` and `PJ_PACKAGE_VERSION` in the root `CMakeLists.txt` (also update
the example tag in the `conanfile.py` docstring). Tagging and pushing the release
is a separate, explicitly-authorized step — never tag or push a release without
the user's go-ahead.

## Instructions Glossary

- **"Read all documentation"** means: find and read every `.md` file in the entire project tree (all subdirectories). Use `find . -name "*.md"` or equivalent. This includes docs in `docs/`, `pj_datastore/docs/`, `pj_plugins/docs/`, and any other location.

- **"Update the documentation"** means: based on what you learned during this session, correct any documentation that is outdated or inaccurate, and clarify any ambiguity that caused confusion or errors. If a doc says one thing but the code does another, fix the doc to match reality. If missing information led to a bug, add it.

- **"Check documentation"** means: review the docs listed above that are related to the changed module or API.
  Confirm they still describe the current intent and behavior. If not, update them or ask the user before
  committing.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit
- **Naming:** `CamelCase` classes, `camelBack` functions, `lower_case` variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** flat `PJ` namespace; `PJ::encoding` and `PJ::arrow_import` for internals
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants
- **Warnings:** `-Wall -Wextra -Werror` on all targets; pre-commit hooks enforce clang-format v17
