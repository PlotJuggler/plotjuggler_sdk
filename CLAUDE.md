# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation libraries that will host the full PlotJuggler 4.x application.

### Existing modules (substrate for the application)

- **pj_base** — vocabulary types, plugin SDK headers (zero external deps)
- **pj_datastore** — columnar storage engine + `ObjectStore` (for media blobs) + `DerivedEngine` (fmt, tsl::robin_map, nanoarrow)
- **pj_plugins** — C-ABI plugin protocol, host-side loaders, dialog SDK (Qt 6.8.3 optional); four plugin families: DataSource, MessageParser, Dialog, Toolbox
- **pj_media** — 2D/video visualization on top of `ObjectStore` (QRhi rendering, Qt 6.8+)
- **pj_marketplace** — extension discovery, download, install (GitHub-hosted registry)

### Planned modules (PlotJuggler 4.x application — see `PJ4_PLAN.md` and `docs/APP_IMPLEMENTATION_PLAN.md`)

- **pj_scripting** — language-agnostic scripting engine (Lua today via sol2; Python pluggable). Decoupled from the GUI; depends only on pj_base + pj_datastore.
- **pj_app_core** — headless business services (SessionManager, PlaybackEngine, WorkspaceManager, TransformRegistry, ToolboxManager, UndoManager, etc.). **Qt allowed (QObject/QTimer/QSettings/signals), no QWidget/QDialog.**
- **pj_plot_widgets** — Qwt-based plot widgets, lifted wholesale from PlotJuggler 3.x.
- **pj_media_widgets_qt** — 2D viewer widgets wrapping pj_media / pj_media_qt.
- **pj_3d_widgets** — 3D widgets for robotics data (TF2, URDF/mesh, pointcloud, markers, image+pinhole, occupancy grid) via custom QRhi scene + GLM + assimp. Architecture locked in PJ4_PLAN §5.5 and APP_IMPLEMENTATION_PLAN §2.4; full implementation scheduled post-app-v1.
- **pj_app** — main-window shell, Qt Advanced Docking, menus, wiring.

The three widget families (plot / 2D / 3D) are **independent by design** — each owns its own rendering and input world. Cross-widget coordination flows through `pj_app_core` services (global tracker, catalog, workspace).

### Dependency graph

- `pj_datastore` → `pj_base`
- `pj_plugins` → `pj_base`
- `pj_media` → `pj_datastore`, `pj_base`
- `pj_scripting` → `pj_base`, `pj_datastore` (uses `ISISOTransform` / `IMIMOTransform`)
- `pj_app_core` → `pj_datastore`, `pj_plugins`, `pj_scripting`, `pj_media`
- `pj_plot_widgets`, `pj_media_widgets_qt`, `pj_3d_widgets` → `pj_app_core` + Qt (never each other)
- `pj_app` → all the above + `pj_marketplace`

### Deprecated

- **pj_proto_app** — throwaway prototype; being replaced by the planned app modules above. Do not evolve it as the final app.

## Key Documentation

**Project-wide:**

| Document | Content |
|----------|---------|
| `docs/cpp_design_recommendations.md` | C++ style, error handling, API design guidelines |
| `PJ4_PLAN.md` | Strategic plan for the PlotJuggler 4.x application architecture |
| `docs/APP_IMPLEMENTATION_PLAN.md` | Tactical implementation plan for the 4.x application layer |
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
| `ARCHITECTURE.md` | C ABI protocols, SDK base classes, host loaders, RAII handles, dialog engine, config envelope |
| `data-source-guide.md` | SDK tutorial: FileSourceBase, StreamSourceBase, delegated ingest, dialog integration |
| `message-parser-guide.md` | SDK tutorial: parse(), schema binding, dialog integration for parsers |
| `dialog-plugin-guide.md` | SDK tutorial: WidgetData, typed events, EmbedUi, requestAccept, onTick |
| `toolbox-guide.md` | SDK tutorial: read+write access, catalog, notifyDataChanged |

**Media** (`pj_media/docs/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Goals for 2D/video visualization, data types, retention |
| `ARCHITECTURE.md` | Decoder pipeline, MediaSource abstraction, QRhi rendering, file vs streaming video |
| `TECHNICAL_NOTES.md` | FFmpeg integration details, HW acceleration, B-frame handling, scrub optimizations |
| `datatypes_2D.md` | Self-contained image/video/annotation/scene types |
| `dataset_format_comparison.md` | How pj_media compares to Rerun and Foxglove |

**Marketplace** (`pj_marketplace/documentation/`):

| Document | Content |
|----------|---------|
| `REQUIREMENTS.md` | Goals, discovery model, install flow, packaging format |
| `ARCHITECTURE.md` | Client/registry model, registry JSON, ZIP packaging, integration point |
| `plotjuggler-marketplace-spec-v1.0.0-en.md` | Versioned protocol spec |

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
