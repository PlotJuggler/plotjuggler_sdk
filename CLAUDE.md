# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation in four modules:

- **pj_base** — vocabulary types, plugin SDK headers (zero external deps)
- **pj_datastore** — columnar storage engine (fmt, tsl::robin_map, nanoarrow)
- **pj_plugins** — plugin protocol, host-side loaders, dialog SDK (Qt 6.8.3 optional)
- **pj_proto_app** — prototype Qt application for testing plugins (Qt 6.8.3 required)

Dependency graph: `pj_datastore` → `pj_base`, `pj_plugins` → `pj_base` (independent of each other). `pj_proto_app` → all modules.

## Key Documentation

**Project-wide:**

| Document | Content |
|----------|---------|
| `docs/cpp_design_recommendations.md` | C++ style, error handling, API design guidelines |

**Datastore** (`pj_datastore/docs/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Goals, data model, ingest contract, schema evolution (dynamic columns, variable-length sequences), query, derived series |
| `ARCHITECTURE.md` | Internals: domain model, layers, data flow, encoding (constant, FoR, dictionary, packed bool), DerivedEngine |
| `USER_GUIDE.md` | Plugin author's guide: write patterns (named vs bound vs Arrow IPC), read API, pitfalls, ValueRef, TypedNull |

**Plugin system** (`pj_plugins/docs/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Plugin families (DataSource, MessageParser, Dialog, Toolbox), interaction model, capability system, config contract |
| `ARCHITECTURE.md` | C ABI protocols, SDK base classes, host loaders, RAII handles, dialog engine, config envelope |
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

Dependencies: Conan (`conanfile.txt`). Qt 6.8.3 optional (`./install_qt6.sh`).

## Pre-commit Validation

Before committing, always run:

```bash
./build.sh --debug && ./test.sh && ./run_clang_tidy.sh
```

## Instructions Glossary

- **"Read all documentation"** means: find and read every `.md` file in the entire project tree (all subdirectories). Use `find . -name "*.md"` or equivalent. This includes docs in `pj_base/`, `pj_datastore/docs/`, `pj_plugins/docs/`, and any other location.

- **"Update the documentation"** means: based on what you learned during this session, correct any documentation that is outdated or inaccurate, and clarify any ambiguity that caused confusion or errors. If a doc says one thing but the code does another, fix the doc to match reality. If missing information led to a bug, add it.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit
- **Naming:** `CamelCase` classes, `camelBack` functions, `lower_case` variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** flat `PJ` namespace; `PJ::encoding` and `PJ::arrow_import` for internals
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants
- **Warnings:** `-Wall -Wextra -Werror` on all targets; pre-commit hooks enforce clang-format v17
