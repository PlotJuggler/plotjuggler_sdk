# PlotJuggler Core

[![Linux CI](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/linux-ci.yml)
[![Windows CI](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/PlotJuggler/plotjuggler_core/actions/workflows/windows-ci.yml)

C++20 foundation libraries for [PlotJuggler](https://github.com/facontidavide/PlotJuggler).

## Modules

| Module | Description | Dependencies |
|--------|-------------|--------------|
| **pj_base** | Vocabulary types: `Timestamp`, `DatasetId`, `TopicId`, type trees, `Expected<T>`, `Span<T>` | None |
| **pj_datastore** | Columnar in-memory storage engine with typed schemas, chunk-based encoding, range/latest-at queries, derived transform DAG, and Arrow IPC import | pj_base, fmt, tsl::robin_map, nanoarrow |
| **pj_plugins** | C-ABI plugin dialog protocol with host-side C++ API and optional Qt 6.8.3 dialog engine | nlohmann/json, Qt 6.8.3 (optional) |

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
pj_base/           Basic types (zero deps)
pj_datastore/      Storage engine (fmt, tsl::robin_map, nanoarrow)
pj_plugins/        Plugin dialog protocol (nlohmann/json, optional Qt)
```

## License

See LICENSE file.
