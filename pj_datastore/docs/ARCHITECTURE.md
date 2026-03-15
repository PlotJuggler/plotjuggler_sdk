# pj_datastore Architecture

## 1. Module Structure

Two libraries with a strict dependency direction:

- **pj_base** (`pj_base/`): Vocabulary types and SDK headers with zero external dependencies. Defines:
  - `Timestamp` (`int64_t`, nanoseconds since Unix epoch)
  - Identity types: `DatasetId`, `TopicId`, `FieldId`, `SchemaId`, `TimeDomainId` (all `uint32_t`), `ChunkId` (`uint64_t`), `NodeId` (`uint32_t`)
  - `PrimitiveType` enum (12 variants: kFloat32..kUint64, kBool, kString)
  - `NumericType`, `NumericValue` (variant of all numeric scalars)
  - `TypeTreeNode` (recursive schema tree: primitive, struct, array, enum nodes)
  - `Span<T>` (alias for `std::span`), `BitSpan` (bit-level view with offset)
  - `Expected<T>` / `Status` for fallible operations, `PJ_ASSERT` for invariants

- **pj_datastore** (`pj_datastore/`): Engine implementation. External dependencies: fmt, tsl::robin_map, nanoarrow (Arrow IPC import).

Dependency: `pj_datastore` -> `pj_base`. No reverse dependency. `pj_plugins` depends on `pj_base` only, never on `pj_datastore`.

## 2. Domain Model

Hierarchy: **Dataset -> Topic -> Chunk -> Column**

- **Dataset** (`DatasetInfo`): Identity (`DatasetId`), `source_name` string, bound `TimeDomain`. Represents one data source (file, live connection).

- **Topic** (`TopicDescriptor` + `TopicStorage`): Named data stream within a dataset. Key fields: `schema_id` (0 = schemaless), `max_chunk_rows` (default 1024), `array_expansion_limit` (default 64). `TopicStorage` owns the committed chunk deque and column descriptors for schemaless topics.

- **Schema** (`TypeRegistry` + `TypeTreeNode`): Tree of named typed nodes. `TypeRegistry` assigns `SchemaId` values, supports lookup by id/name, and additive-only evolution via `evolveSchema()`. Schemas are shared across topics.

- **Chunk** (`TopicChunk`): Sealed, immutable storage unit produced by `TopicChunkBuilder::seal()`. Contains: `ChunkId` (monotonic atomic counter), timestamps vector, columns vector (each holding `EncodedData` + optional `BitVector` validity bitmap + `shared_ptr<ColumnDescriptor>`), and `ChunkStats` (t_min, t_max, row_count, per-column `ColumnStats`).

- **Column** (`TopicChunk::Column`): The physical unit. `EncodedData` is a variant of 5 encoding types (see section 5). `ColumnDescriptor` carries field_id, logical_type, and fully-qualified field_path (e.g. `"pose.position.x"`).

## 3. Layer Architecture

### Logical Layer

**`DataEngine`** — Central owner of all state. Stores datasets, topics (as `TopicStorage`), time domains, and the global `TypeRegistry`, all in hash map containers (tsl::robin_map internally, std::unordered_map in headers). Provides:
- `createDataset()`, `createTopic()`, `createTimeDomain()` with monotonic ID allocation
- `commitChunks()` — appends sealed chunks to `TopicStorage`, returns deduplicated list of changed `TopicId`s
- `enforceRetention()` — evicts old chunks across all topics
- Factory methods `createWriter()` and `createReader()`

**`TypeRegistry`** — `registerSchema()` assigns a new `SchemaId`. `registerOrGet()` returns an existing ID if the name matches (for late-discovery schemas). `evolveSchema()` validates additive-only changes (no field removal or type change).

### Storage Layer

**`TopicChunkBuilder`** — Mutable builder that accumulates rows for one topic. Two append paths:
- *Row-at-a-time*: `beginRow(timestamp)` -> `set<T>(col, value)` / `setNull(col)` -> `finishRow()`. `finishRow()` pads unset columns with null.
- *Bulk*: `appendTimestamps(span)` -> `appendColumn<T>(col, span)` per column -> `appendColumnValidity(col, bitspan)` -> `finishBulkAppend()` (computes stats).

Tracks per-column `ColumnStats` incrementally (min, max, null_count, is_constant, run_count). `seal()` encodes columns, assigns a monotonic `ChunkId` via `std::atomic<ChunkId>`, and produces an immutable `TopicChunk`.

**`TopicStorage`** — Per-topic container of committed chunks in a `std::deque<TopicChunk>`. `appendSealedChunk()` validates ordering: each chunk's `t_min >= previous chunk's t_max`. `evictBefore()` removes chunks whose `t_max < threshold`. Also stores column descriptors for schemaless (schema_id == 0) topics and per-field array expansion counts.

**`TypedColumnBuffer`** — In-memory typed column buffer used internally by the builder. One buffer per column. Supports per-type single-row append (`appendFloat64`, `appendInt64`, etc.) and bulk append (`appendFloat64Bulk`, etc.). String storage uses a separate offsets buffer (`RawBuffer`) with Arrow-compatible uint32 offset layout. Validity bitmap (`BitVector`) is lazily initialized -- only allocated when the first null is appended.

**`RawBuffer`** — Growable byte vector wrapping `std::vector<uint8_t>`. Used for column value storage and encoded payloads.

**`BitVector`** — Owning packed validity bitmap with Arrow-compatible LSB-first layout. Supports `initValid()`, `setNull()`, `isValid()`, `countNulls()`, and bulk `assignBytes()`.

### Writer Layer

**`DataWriter`** — High-level write facade bound to a `DataEngine`. Manages one `TopicChunkBuilder` and a pending chunk list per topic. Key operations:
- `registerTopic()` / `registerScalarSeries()` — topic creation
- `ensureColumn()` — dynamic column addition for schemaless topics. Rejects if a row is in progress (for new columns only). Seals any pending builder before modifying column layout.
- `expandArray()` — variable-length array expansion. Seals current builder, adds new `ColumnDescriptor`s, updates `TopicStorage`. Clamps to `array_expansion_limit`.
- `appendColumns()` — bulk ingest with auto-chunking: splits batches that exceed `max_chunk_rows`, calling `appendTimestamps` / `appendColumn` / `finishBulkAppend` / `autoSeal` per sub-batch.
- `flush()` / `flushAll()` — seals remaining builders and returns `vector<pair<TopicId, TopicChunk>>` for `commitChunks()`.
- `autoSeal()` — called when builder is full (`rowCount >= max_chunk_rows`). Seals builder, moves chunk to `pending_chunks_`.

### Reader Layer

**`DataReader`** — Read-only facade over committed `DataEngine` storage. Provides `listDatasets()`, `listTopics()`, `getTypeTree()`, `getMetadata()`, `rangeQuery()`, and `latestAt()`.

### Query Layer

**`RangeCursor`** — Iterates rows in `[t_min, t_max]` across the chunk deque. Constructor binary-searches the deque for the first overlapping chunk and row. Supports:
- `forEach(callback)` — per-row iteration via `SampleRow` (timestamp + chunk pointer + row index)
- `forEachChunk(callback)` — bulk iteration via `ChunkRowRange` (chunk pointer + row start/end)

**`latestAt(chunks, t)`** — Binary search for the most recent row at or before timestamp `t`. Returns `optional<SampleRow>`.

Both are free functions operating on `const std::deque<TopicChunk>&`.

### Encoding Layer

Seven `StorageKind` values map logical `PrimitiveType` to physical storage:

| StorageKind | Source PrimitiveTypes |
|---|---|
| kFloat32 | kFloat32 |
| kFloat64 | kFloat64 |
| kInt32 | kInt32 |
| kInt64 | kInt8, kInt16, kInt64 |
| kUint64 | kUint8, kUint16, kUint32, kUint64 |
| kBool | kBool |
| kString | kString |

Five `EncodingType` values, selected at seal time per column:

| EncodingType | When used | Representation |
|---|---|---|
| kRaw (`RawBuffer`) | Default for float/uint64 when not constant | Typed byte buffer |
| kConstant (`ConstantEncoded`) | All non-null values equal (`is_constant && rowCount > 0`) | 8-byte value + count |
| kFrameOfReference (`FrameOfReferenceEncoded`) | kInt32/kInt64 when range fits in fewer bytes | int64 reference + packed uint8/16/32 offsets |
| kDictionary (`DictionaryEncoded`) | Always for kString | Unique string list + narrowed uint8/16/32 indices |
| kPackedBool (`PackedBools`) | kBool when not constant | 1 bit per value, LSB first |

`EncodedData = std::variant<RawBuffer, ConstantEncoded, FrameOfReferenceEncoded, DictionaryEncoded, PackedBools>`

Encoding selection in `TopicChunkBuilder::seal()`:
1. **Strings**: always dictionary encoded via `dictionaryEncodeStrings()`.
2. **Bools**: constant if `is_constant`, otherwise packed bits via `packBools()`.
3. **Signed integers** (kInt32/kInt64): recomputes exact int64 min/max from raw buffer (avoids double-precision loss). Constant if all equal, frame-of-reference if `offsetBytesFor(range) < storageKindSize(kind)`, otherwise raw.
4. **Float32/float64/uint64**: constant if `is_constant`, otherwise raw.

### Derived Layer

**`DerivedEngine`** — Manages a transform DAG. Uses pimpl (`DerivedEngineImpl`). Key operations:

- `addSisoTransform()` — registers a single-input/single-output node. Input must be a single-column topic. Creates an output scalar topic. Returns `NodeId`.
- `addMimoTransform()` — registers a multi-input/multi-output node. All inputs must be single-column. Creates N output topics.
- `topologicalOrder()` — Kahn's algorithm. Cycle detection via DFS at registration time.
- `onSourceCommitted(changed_topics)` — marks directly dependent nodes dirty.
- `scheduleAll()` — processes all dirty nodes in topological order (incremental path).
- `scheduleActive(active_nodes)` — processes only specified nodes and their transitive upstream dependencies.
- `recompute_batch(node_id)` — clears output topic, calls `transform.reset()`, replays full input history.

**`ISISOTransform`** — Point-at-a-time interface. `calculate(time, input, &out_time, &out_value) -> bool`. Called in strictly ascending timestamp order. State persists across chunk boundaries. `reset()` clears state for batch recompute. `outputKind()` declares output `StorageKind` (default kFloat64).

**`IMIMOTransform`** — N inputs -> M outputs. `calculate(time, inputs_span, &out_time, &outputs_vec) -> bool`. Exact-timestamp inner join: only called when ALL input topics have a sample at the same timestamp. `outputKinds(input_kinds)` declares one `StorageKind` per output topic.

**`VarValue = std::variant<int64_t, uint64_t, double, std::string>`** — Universal value type for transform I/O. Mapping: float32/float64 -> double, int8..int64/bool -> int64_t, uint64 -> uint64_t, string -> std::string.

Incremental scheduling: each node tracks a `last_processed_chunk_id` watermark. `scheduleAll()` iterates only chunks with id > watermark, reads each row, calls `calculate()`, writes output via `beginRow`/`set`/`finishRow`, then flushes and commits.

### Plugin Host Layer

**`DatastoreSourceWriteHost`** / **`DatastoreParserWriteHost`** / **`DatastoreToolboxHost`** — Bridge between the C ABI plugin protocol (`PJ_source_write_host_t`, etc.) and the C++ `DataWriter`/`DataEngine`. Each wraps a pimpl state struct. Provides `raw()` to get the C function-pointer table and `flushPending()` to seal/commit accumulated data.

The host translates C ABI calls (ensureTopic, ensureField, appendRecord) into `DataWriter` operations (registerTopic/ensureColumn, beginRow/set/finishRow).

### Import Adapter

**Arrow IPC** (`PJ::arrow_import` namespace):
- `schemaFromIpc()` — parses Arrow schema from IPC stream bytes via nanoarrow. Maps Arrow fields to `ArrowColumnMapping` (arrow column index -> PJ column index + PrimitiveType). Unsupported Arrow types are skipped.
- `importIpcStream()` — reads record batches, appends via `DataWriter::appendColumns()` with validity bitmaps.

## 4. Data Flow

### Row-at-a-time Ingest (via plugin host)

1. Plugin calls `writeHost.ensureTopic(name)` -> host creates topic via `DataWriter::registerTopic()`
2. Plugin calls `writeHost.ensureField(topic, name, type)` -> host calls `DataWriter::ensureColumn()`
3. Plugin calls `writeHost.appendRecord(topic, timestamp, fields)` -> host calls `DataWriter::beginRow()`, `set()` per field, `finishRow()`
4. `finishRow()` pads unset columns with null, updates per-column stats
5. If builder is full (`rowCount >= max_chunk_rows`): `autoSeal()` seals builder, moves `TopicChunk` to `pending_chunks_`
6. Host calls `flushPending()` -> `DataWriter::flushAll()` seals remaining builders -> `DataEngine::commitChunks()` appends to `TopicStorage`
7. Chunks are now visible to readers and derived engine

### Bulk Ingest

1. `DataWriter::appendColumns(topic_id, timestamps, column_data_array)`
2. Auto-chunks: splits batch into sub-batches fitting builder's `remainingCapacity()`
3. For each sub-batch: `appendTimestamps()`, `appendColumn<T>()` per column, `finishBulkAppend()` (computes stats), `autoSeal()` if full
4. Flush + commit same as row-at-a-time

### Query

1. `DataReader::rangeQuery(QueryRange{topic_id, t_min, t_max})` -> `RangeCursor`
2. Cursor binary-searches the chunk deque for start position
3. `forEach` callback receives `SampleRow` (chunk pointer + row index)
4. Caller reads values via `chunk->readNumericAsDouble(col, row)`, `readString()`, `readBool()`, `isNull()`, or batch via `readColumnAsDoubles()`

### Derived Scheduling

1. `engine.commitChunks()` returns changed topic IDs
2. Caller invokes `derivedEngine.onSourceCommitted(changed_topics)` -> marks dependent nodes dirty
3. `derivedEngine.scheduleAll()` processes dirty nodes in topological order
4. For each dirty SISO node: iterate new input chunks (id > `last_processed_chunk_id`), read each row, call `transform.calculate()`, write output via `beginRow`/`set`/`finishRow`, flush + commit
5. For each dirty MIMO node: iterate primary input's new chunks, for each timestamp check all other inputs via `latestAt()`, call `transform.calculate()` only when all inputs have matching timestamps

## 5. Key Invariants

- **Dense field IDs**: Field IDs within a topic are always 0, 1, 2, ... with no gaps.
- **Monotonic timestamps**: Timestamps within a topic are monotonically non-decreasing. Enforced at `beginRow()` and `appendTimestamps()`.
- **ensureColumn guards**: Rejects new columns after a row is in progress. Invalidates stale 0-row builders when adding columns.
- **expandArray seals first**: `expandArray()` seals the current builder before modifying column layout, preventing mid-chunk schema changes.
- **Chunk ordering**: Each chunk's `t_min >= previous chunk's t_max`. Enforced by `TopicStorage::appendSealedChunk()`.
- **Lazy validity bitmaps**: `TypedColumnBuffer` only allocates a `BitVector` on first `appendNull()`. Sealed chunks include validity only when `hasNulls()` is true.
- **NaN for nulls**: `readColumnAsDoubles()` writes `NaN` at null positions, preventing confusion between null and zero.
- **ChunkId monotonicity**: `TopicChunkBuilder` uses a `static atomic<ChunkId>` counter starting at 1. `kInvalidChunkId` (0) is the sentinel for "no chunk seen yet".

## 6. Threading Model

Effectively single-threaded. `DataWriter` accumulates in-memory. `DataEngine::commitChunks()` is synchronous. No internal locks or queues. The only concurrency point is `TopicChunkBuilder::next_chunk_id_` (a `std::atomic`), which allows multiple builders to generate unique chunk IDs without coordination. Plugin sources that receive data on network threads must queue messages internally and process them in `onPoll()`.

## 7. Testing

14 test executables covering all layers:

| Test | Coverage |
|---|---|
| `buffer_test` | `RawBuffer`, `BitVector` |
| `column_buffer_test` | `TypedColumnBuffer` per-type append/read, validity |
| `type_registry_test` | Schema registration, lookup, evolution |
| `encoding_test` | All 5 encoding types: constant, FOR, dictionary, packed bool, raw |
| `chunk_test` | `TopicChunkBuilder` row/bulk paths, seal, read-back |
| `topic_storage_test` | Commit ordering, eviction, metadata |
| `query_test` | `RangeCursor`, `latestAt`, edge cases |
| `engine_integration_test` | Full `DataEngine` + `DataWriter` + `DataReader` round-trip |
| `derived_engine_test` | SISO/MIMO transforms, topological order, incremental + batch recompute |
| `array_expansion_test` | `expandArray`, clamping, cross-builder expansion |
| `regression_test` | Bug-specific regression cases |
| `plugin_host_write_test` | C ABI write host -> datastore round-trip |
| `plugin_host_read_test` | C ABI read host queries |
| `arrow_import_test` | Arrow IPC schema parsing and batch import |

Build and run: `./build.sh --debug && ./test.sh`
