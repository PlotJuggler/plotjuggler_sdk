# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation in five modules:

- **pj_base** — vocabulary types, plugin SDK headers (zero external deps)
- **pj_datastore** — columnar storage engine (Abseil, nanoarrow)
- **pj_plugins** — plugin protocol, host-side loaders, dialog SDK (Qt 6.8.3 optional)
- **pj_ported_plugins** — 11 ported plugins from old PlotJuggler (CSV, Parquet, ULog, MCAP, JSON, Protobuf, ROS, ZMQ, MQTT, Foxglove Bridge, PJ Bridge)
- **pj_proto_app** — prototype Qt application for testing plugins (Qt 6.8.3 required)

Dependency graph: `pj_datastore` → `pj_base`, `pj_plugins` → `pj_base` (independent of each other). `pj_ported_plugins` → `pj_base` + various Conan deps. `pj_proto_app` → all modules.

## Key Documentation

| Document | When to read |
|----------|-------------|
| `docs/cpp_design_recommendations.md` | C++ style, error handling, API design guidelines |
| `pj_datastore/docs/REQUIREMENTS.md` | Datastore goals, use cases, functional/non-functional requirements |
| `pj_datastore/docs/ARCHITECTURE.md` | Datastore internals: domain model, layers, data flow, encoding, DerivedEngine |
| `pj_datastore/docs/USER_GUIDE.md` | How to read/write data via plugin API — pitfalls, examples, ValueRef |
| `pj_plugins/docs/REQUIREMENTS.md` | Plugin system goals, families, interaction model, capability system |
| `pj_plugins/docs/ARCHITECTURE.md` | Plugin internals: ABI layers, host loaders, dialog engine, config envelope |
| `pj_plugins/docs/data-source-guide.md` | How to write a DataSource plugin (SDK tutorial) |
| `pj_plugins/docs/message-parser-guide.md` | How to write a MessageParser plugin (SDK tutorial) |
| `pj_plugins/docs/dialog-plugin-guide.md` | How to write a Dialog plugin (WidgetData, events, EmbedUi, requestAccept) |
| `pj_plugins/docs/toolbox-guide.md` | How to write a Toolbox plugin (SDK tutorial) |
| `pj_ported_plugins/porting_guide.md` | **READ FIRST** for plugin work: SDK patterns, datastore pitfalls, dialog SDK, lessons learned |

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

- **"Read all documentation"** means: find and read every `.md` file in the entire project tree (all subdirectories). Use `find . -name "*.md"` or equivalent. This includes docs in `pj_base/`, `pj_datastore/docs/`, `pj_plugins/docs/`, `pj_ported_plugins/`, and any other location.

- **"Update the documentation"** means: based on what you learned during this session, correct any documentation that is outdated or inaccurate, and clarify any ambiguity that caused confusion or errors. If a doc says one thing but the code does another, fix the doc to match reality. If missing information led to a bug, add it.

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit
- **Naming:** `CamelCase` classes, `lower_case` functions/variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** flat `PJ` namespace; `PJ::encoding` and `PJ::arrow_import` for internals
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants
- **Warnings:** `-Wall -Wextra -Werror` on all targets; pre-commit hooks enforce clang-format v17
