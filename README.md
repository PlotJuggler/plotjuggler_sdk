# PlotJuggler Core

[![Linux CI](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/linux-ci.yml)
[![Windows CI](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/windows-ci.yml)

C++20 foundation libraries for [PlotJuggler](https://github.com/facontidavide/PlotJuggler).

## Modules

### Existing (substrate for the application)

| Module | Description | Dependencies |
|--------|-------------|--------------|
| **pj_base** | Vocabulary types: `Timestamp`, `DatasetId`, `TopicId`, type trees, `Expected<T>`, `Span<T>` | None |
| **pj_datastore** | Columnar in-memory storage engine + `ObjectStore` (for media blobs) + `DerivedEngine`; typed schemas, chunk-based encoding, range/latest-at queries, derived transform DAG, Arrow IPC import | pj_base, fmt, tsl::robin_map, nanoarrow |
| **pj_plugins** | C-ABI plugin protocol (DataSource, MessageParser, Dialog, Toolbox families) with host-side C++ API and optional Qt 6.8.3 dialog engine | nlohmann/json, Qt 6.8.3 (optional) |
| **pj_media** | 2D/video visualization on top of `ObjectStore`: images, video, depth, annotations, 2D scene primitives. QRhi GPU rendering | pj_datastore, FFmpeg, turbojpeg, libpng, Qt 6.8+ (pj_media_qt only) |
| **pj_marketplace** | Extension discovery, download, install — GitHub-hosted registry + Qt client | nlohmann/json, Qt 6.8+ |

### Planned (PlotJuggler 4.x application — see `PJ4_PLAN.md`)

| Module | Description |
|--------|-------------|
| **pj_scripting** | Language-agnostic scripting engine (Lua today, Python pluggable) |
| **pj_app_core** | Headless business services (sessions, playback, workspace, transforms, toolboxes, undo). Qt allowed, no QWidget |
| **pj_plot_widgets** | Qwt-based plot widgets, lifted wholesale from PlotJuggler 3.x |
| **pj_media_widgets_qt** | 2D viewer widgets wrapping pj_media/pj_media_qt |
| **pj_3d_widgets** | 3D widgets for robotics (TF2, URDF/mesh, pointcloud, markers, image+pinhole, occupancy grid) via custom QRhi + GLM + assimp. Implementation post-v1 |
| **pj_app** | Main-window shell, Qt Advanced Docking, menus |

The three widget families (plot / 2D / 3D) are independent by design; each owns its own rendering and input world. Cross-widget coordination flows through `pj_app_core` services.

### Deprecated

- **pj_proto_app** — throwaway prototype kept only as a reference; replaced by the planned app modules above.

## Getting Started

**Prerequisites:** C++20 compiler (GCC 11+), [Conan 2](https://conan.io/), CMake 3.22+.

### LLVM tooling (clang-format, clangd, clang-tidy)

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 22
sudo apt install clangd-22 clang-format-22 clang-tidy-22
```

### Build and test

```bash
git clone <this repo>
cd plotjuggler_core

./build.sh              # RelWithDebInfo (build/)
./build.sh --debug      # Debug + ASAN (build/debug_asan)
./test.sh               # runs tests in all discovered build dirs
./run_clang_tidy.sh     # clang-tidy via clangd-22
```

### Qt dialog engine (optional)

The Qt dialog engine is auto-detected at configure time. Install Qt 6.8.3 first:

```bash
./install_qt6.sh
export CMAKE_PREFIX_PATH=$(pwd)/.qt/6.8.3/gcc_64
./build.sh
```

## Project Layout

```
pj_base/                   Vocabulary types (zero deps)
pj_datastore/              Columnar engine + ObjectStore + DerivedEngine
pj_plugins/                C-ABI plugin protocol + dialog engine
pj_media/                  2D/video visualization (QRhi)
pj_marketplace/            Extension discovery + install
docs/                      Project-wide docs (PJ4_PLAN.md, design guides)
pj_proto_app/              Deprecated prototype
```

The PlotJuggler 4.x application modules (`pj_scripting/`, `pj_app_core/`, `pj_plot_widgets/`, `pj_media_widgets_qt/`, `pj_3d_widgets/`, `pj_app/`) are planned as siblings in this repository; see `PJ4_PLAN.md`.

## License

See LICENSE file.
