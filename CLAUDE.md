# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlotJuggler Data Engine — a high-performance, columnar in-memory storage and retrieval engine for PlotJuggler (data visualization tool). Written in C++20, it provides typed schemas, chunk-based columnar storage with per-column encoding, and Arrow-compatible data import.

## Build Commands

Dependencies are managed with Conan (`data/conanfile.txt`). Build from the `data/` directory:

```bash
# Install dependencies
cd data
conan install . --output-folder=build --build=missing

# Configure and build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run all tests
cd build && ctest

# Run a single test
cd build && ./types_test          # or any test name: chunk_test, query_test, etc.

# Run benchmarks
cd build && ./read_benchmark
cd build && ./ingest_benchmark
```

**CMake options:**
- `-DPJ_ASSERT_THROWS=ON` — use exceptions instead of `assert()` for `PJ_ASSERT`
- `-DPJ_ENABLE_SANITIZERS=ON` — enable ASAN/UBSan (Debug builds only; use GCC 13+ or Clang)
- `-DPJ_BUILD_PARQUET_IMPORT_EXAMPLE=ON` — build Parquet import example (requires full Arrow C++)

## Architecture

```
data/
├── base/          # Vocabulary types (zero dependencies)
│   └── include/PJ/base/   types.hpp, type_tree.hpp, dataset.hpp, span.hpp, expected.hpp, assert.hpp
├── engine/        # Storage engine (depends on Abseil, nanoarrow)
│   ├── include/PJ/engine/ — public headers
│   └── src/               — implementations
├── tests/         # GoogleTest suite (13 test executables)
├── benchmarks/    # Google Benchmark files
└── examples/      # Optional examples (Parquet import)
```

**4-layer design:**

1. **API layer** — `DataEngine`, `DataWriter`, `DataReader` (engine.hpp, writer.hpp, reader.hpp)
2. **Logical layer** — `TypeRegistry`, `TypeTreeNode`, schemas, datasets, time domains
3. **Storage layer** — `TopicStorage` → `TopicChunk` → `TypedColumnBuffer` → `RawBuffer`
4. **Encoding layer** — Delta, Dictionary, PackedBool, FrameOfReference, Constant (encoding.hpp)

**Key data flow:** DataWriter builds rows into a `TopicChunkBuilder`. When chunk size is reached, the chunk is sealed (columns encoded, stats computed) and pushed to `TopicStorage`. DataReader queries sealed chunks via `RangeCursor` or `LatestAtResult`.

**Two static libraries:**
- `plotjuggler_base` — vocabulary types, type tree (no external deps)
- `plotjuggler_engine` — full engine (links base + Abseil + nanoarrow)

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char column limit, attached braces
- **Naming:** `CamelCase` classes, `lower_case` functions/variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** `PJ::base`, `PJ::engine`
- **Error handling:** `absl::StatusOr<T>` for fallible operations, `PJ_ASSERT(cond, msg)` for invariants
- **Modern C++:** `[[nodiscard]]`, `noexcept`, `constexpr`, `explicit` constructors, `const` by default
- **Pre-commit hooks:** clang-format (v17) enforced via `.pre-commit-config.yaml`

## Dependencies

Abseil (containers, status, strings), nanoarrow + nanoarrow_ipc (Arrow C), GoogleTest, Google Benchmark. Optional: full Apache Arrow C++ with Parquet.

## Key Design Decisions

- Logical schema (TypeTree) is independent from physical encoding
- Sealed chunks are immutable; only builders are mutable
- Arrow-compatible memory layouts (validity bitmaps, string offsets)
- Strong domain types (`DatasetId`, `TopicId`, `FieldId`, `ChunkId`, `Timestamp`) — not bare integers
- See `docs/cpp_design_recommendations.md` for API design philosophy
