# PlotJuggler Data Engine Architecture Overview

Last updated: 2026-03-03

## 1. Purpose and scope

This document describes the architecture of the software currently implemented in this repository, focused on the `pj_datastore/` engine.

It is a practical reference for:

- understanding how data moves through the system
- understanding module boundaries and responsibilities
- evaluating correctness, performance, and implementation gaps

This is not a forward-looking wishlist. It describes what is implemented today, plus clearly marked gaps.

---

## 2. High-level architecture

The system is split into two C++ libraries:

- `pj_base` in `pj_base/`
- `pj_datastore` in `pj_datastore/`

The architecture follows a layered pattern:

1. Logical layer: type trees, schema registry, dataset/topic metadata
2. Storage layer: chunked columnar storage, retention, metadata
3. Encoding layer: per-column encodings chosen at chunk seal time
4. Query layer: range cursor and latest-at APIs
5. Derived layer: transform DAG (SISO and MIMO) with incremental scheduling
6. Import adapter: Arrow IPC import via nanoarrow into writer APIs

---

## 3. Repository and build structure

Primary build file: `CMakeLists.txt (top-level)`

Main targets:

- `pj_base`
- `pj_datastore`
- test executables in `pj_datastore/tests/`
- benchmark executables in `pj_datastore/benchmarks/`

Key dependencies from `conanfile.txt (top-level)`:

- `abseil`
- `gtest`
- `benchmark`
- `arrow`
- `nanoarrow` (with IPC)

Important note:

- Engine core exposes its own APIs and does not expose Arrow types in its public writer/reader interface.
- Arrow is used in the import adapter (`arrow_import`) and linked privately.

---

## 4. Core domain model

The data model hierarchy is:

- Dataset
- Topic
- Chunk
- Column

Core IDs and timestamp alias are defined in `pj_base/include/pj_base/types.hpp`:

- `DatasetId`, `TopicId`, `FieldId`, `ChunkId`, `SchemaId`, `TimeDomainId`, `NodeId`
- `Timestamp = int64_t` (nanoseconds)

### Dataset

Defined in `pj_base/include/pj_base/dataset.hpp`.

Contains:

- dataset identity and source metadata
- associated time domain
- list of topic IDs

### Topic

Defined through `TopicDescriptor` and `TopicStorage` in `pj_datastore/include/pj_datastore/topic_storage.hpp`.

Contains:

- name
- schema ID
- dataset ID
- chunk size target
- committed sealed chunks

### Schema/type tree

Defined in `pj_base/include/pj_base/type_tree.hpp` and managed by `TypeRegistry`:

- primitives, structs, arrays, enums
- semantic tags on type nodes
- flatten helpers for leaf path extraction
- additive-only schema evolution in `TypeRegistry::evolveSchema()`

### Chunk and columns

Chunk types are defined in `pj_datastore/include/pj_datastore/chunk.hpp`:

- `TopicChunkBuilder` for mutable ingestion
- `TopicChunk` for sealed immutable storage
- per-chunk `ChunkStats` and per-column `ColumnStats`

---

## 5. Module responsibilities

## 5.1 `DataEngine`

File: `pj_datastore/include/pj_datastore/engine.hpp`, implementation in `pj_datastore/src/engine.cpp`.

Responsibilities:

- owns datasets, topics, time domains, schema registry
- creates writers/readers
- commits flushed chunks into topic storage
- enforces retention window eviction

Important behavior:

- `commitChunks()` appends chunks and returns deduplicated changed topic IDs
- `enforceRetention()` evicts by per-topic latest timestamp minus retention window

## 5.2 `DataWriter`

File: `pj_datastore/include/pj_datastore/writer.hpp`, implementation in `pj_datastore/src/writer.cpp`.

Responsibilities:

- schema registration and topic registration
- row-at-a-time ingest:
  - `beginRow()`
  - `set_*()` / `setNull()`
  - `finishRow()`
- bulk columnar ingest:
  - `appendColumns()`
- scalar convenience ingest:
  - `registerScalarSeries()`
  - `appendScalar()`
- dynamic column addition:
  - `ensureColumn(topic_id, field_path, PrimitiveType)` — adds a single typed column by path; idempotent; works on typed and schemaless topics; no schema required
  - `expandArray(topic_id, path, N, element_type=kFloat64)` — adds N indexed element columns; typed topics use schema for element type; schemaless topics use `element_type`
- flush and auto-seal behavior

Important behavior:

- timestamps must be monotonic per topic
- missing row fields are auto-filled as null at row finalization
- bulk ingest can span chunk boundaries automatically
- `ensureColumn` returns error on type mismatch for an existing path; safe no-op on same-type re-call even mid-row
- columns added via `ensureColumn` on typed topics are NOT reflected in `getTypeTree()` — physical layout and schema tree can diverge intentionally

## 5.3 `TopicChunkBuilder` and `TopicChunk`

File: `pj_datastore/src/chunk.cpp`.

Responsibilities:

- maintain mutable in-memory column buffers during ingest
- update stats (min/max/nulls/runs/is_constant)
- seal into immutable chunk with chosen encodings
- provide decode helpers for readers and derived engine

## 5.4 `TopicStorage`

File: `pj_datastore/src/topic_storage.cpp`.

Responsibilities:

- hold committed sealed chunks per topic
- reject out-of-order chunk appends
- evict expired chunks
- provide aggregated metadata snapshots

## 5.5 `DataReader` and query layer

Files:

- `pj_datastore/src/reader.cpp`
- `pj_datastore/src/query.cpp`

Responsibilities:

- list datasets/topics
- retrieve metadata and type tree
- execute range queries and latest-at queries

Current query behavior:

- `RangeCursor` scans chunk bounds and rows to produce matching rows
- `latestAt` reverse-scans chunks and rows

## 5.6 `DerivedEngine`

File: `pj_datastore/src/derived_engine.cpp`.

Responsibilities:

- register SISO and MIMO transform nodes
- maintain DAG dependencies and cycle checks
- dirty propagation on source topic updates
- topological scheduling
- incremental and batch recompute execution

Current transform contracts:

- SISO and MIMO transforms run sequentially sample-by-sample
- transform state is stored in transform objects
- `reset()` is used before full recompute

## 5.7 Arrow IPC import adapter

File: `pj_datastore/src/arrow_import.cpp`.

Responsibilities:

- parse Arrow IPC schema from bytes
- map Arrow columns to engine columns
- convert buffers and call `DataWriter::appendColumns()`

---

## 6. Data flow

## 6.1 Row-at-a-time ingest flow

1. Caller obtains `DataWriter` from `DataEngine`.
2. Caller registers schema/topic or scalar series.
3. Caller appends rows through `beginRow` -> `set*` -> `finishRow`.
4. Builder auto-seals when `max_chunk_rows` is reached.
5. Caller calls `flush()` or `flushAll()`.
6. Caller commits via `DataEngine::commitChunks()`.
7. Optional: call `DataEngine::enforceRetention()`.

## 6.2 Bulk ingest flow

1. Caller builds timestamp array and `ColumnData[]`.
2. Caller calls `appendColumns()`.
3. Writer slices batch per remaining chunk capacity.
4. Builder appends data/validity and computes deferred bulk stats.
5. Auto-seal occurs at chunk boundary.
6. Flush and commit as in row path.

## 6.3 Query flow

1. Caller gets `DataReader`.
2. Caller issues `rangeQuery(QueryRange)` or `latestAt(QueryPoint)`.
3. Query layer traverses committed chunks in `TopicStorage`.
4. Rows are returned as `SampleRow` references into immutable chunk data.

## 6.4 Derived scheduling flow

1. Caller commits source chunks.
2. Changed topic IDs are passed to `DerivedEngine::onSourceCommitted()`.
3. Scheduler runs nodes in topological order.
4. Incremental runs process only unseen inputs using watermarks.
5. Output rows are written via writer APIs into derived output topics.
6. Batch recompute clears output and replays all historical input.

---

## 7. Storage and encoding strategy

Column storage kinds (`StorageKind`) are:

- `kFloat32`
- `kFloat64`
- `kInt32`
- `kInt64`
- `kUint64`
- `kBool`
- `kString`

Encoding types (`EncodingType`) are:

- `kRaw`
- `kDictionary`
- `kPackedBool`
- `kConstant`
- `kFrameOfReference`

Current seal-time behavior (implemented in `chunk.cpp`):

- strings -> dictionary encoding
- bools -> constant or packed bitfield
- signed ints -> constant, FOR, or raw
- floats and uint64 -> constant or raw
- validity bitmap copied only when column has nulls

Important implementation tradeoff:

- narrow integer logical types are widened at storage kind level (`int8/int16 -> int64`, `uint8/16/32 -> uint64`)

---

## 8. Time and retention model

Time-domain metadata is available in `DataEngine`:

- create/get time domain
- set display offset

Current behavior:

- visual offset metadata exists
- no advanced cross-domain alignment UI/control in this repo yet

Retention:

- eviction is per topic
- `t_keep_min = topic_time_max - retention_window`
- chunks with `chunk.t_max < t_keep_min` are evicted from front

---

## 9. Threading model and runtime semantics

Current implementation in this repo is effectively synchronous on commit:

- writers build and flush chunks
- engine commits chunks directly
- no plugin staging queues implemented in this codebase

Implication:

- current architecture and tests validate data semantics and APIs
- deferred multi-thread queue orchestration is integration work outside this core

---

## 10. Testing and benchmark coverage

Test suite location: `pj_datastore/tests/`.

Coverage includes:

- core types and utility abstractions (`Span`, `Expected`, type trees)
- buffers and validity behavior
- encoding and decoding correctness
- chunk lifecycle and stats
- topic storage and retention
- query behavior
- engine integration end-to-end
- derived DAG behavior and parity checks
- Arrow import path

Benchmark suite location: `pj_datastore/benchmarks/`.

Benchmarks cover:

- read throughput across raw/encoded/cursor paths
- ingest throughput (row vs bulk, builder and writer level)

---

## 11. Known gaps and deferred areas

Notable gaps relative to the broader plan documents:

- run-length encoding (RLE) for low-cardinality / constant-run columns is not implemented (Phase 4)
- plugin staging queue model (`PluginStagingContext`, SPSC queue) is not implemented; commit is synchronous (Phase 5 deferred)
- advanced time-domain alignment controls and automatic t0 alignment are not implemented (Phase 3)
- automatic schema inference from first message for schemaless formats (JSON, CBOR, MessagePack) is not implemented; the storage primitives (`ensureColumn`, schemaless `expandArray`) are ready, but plugin-level discovery and field auto-registration are not
- `SchemaVersionHistory` per topic (tracking schema version entries over time) is not implemented; schema evolution replaces the tree in-place via `TypeRegistry::evolveSchema()`
- full plugin migration/integration is out of scope in this repository stage

---

## 12. How to evaluate this architecture as a human

Recommended order:

1. Read `pj_datastore/docs/data_implementation_plan.md`.
2. Read public headers in `pj_base/include/pj_base/` and `pj_datastore/include/pj_datastore/`.
3. Trace `writer.cpp` -> `chunk.cpp` -> `topic_storage.cpp`.
4. Trace `reader.cpp` -> `query.cpp`.
5. Trace `derived_engine.cpp`.
6. Read tests corresponding to each module.
7. Run tests and benchmarks from `build/`.

Practical checks:

- correctness invariants (timestamp order, null fill, schema evolution)
- incremental vs batch parity in derived engine
- chunk metadata correctness under retention
- benchmark regressions for ingest/read hot paths

---

## 13. Summary

The implemented software is a test-heavy, modular C++ data engine with:

- typed schema and metadata management
- chunked columnar storage
- seal-time adaptive encodings
- range and latest-at query primitives
- derived transform DAG with incremental scheduling
- Arrow IPC ingestion adapter

It is already in a useful, evaluable state for core engine behavior, with clear extension points for plugin integration, richer time-domain features, and additional compression strategies.
