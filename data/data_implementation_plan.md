# PlotJuggler Data Engine Redesign -- Detailed Plan

This document defines the plan for a new in-memory Data Engine for PlotJuggler.
It incorporates the original PLAN.md goals, reference architecture research,
design decisions confirmed with the author, and review feedback from both
codex_plan.md and claude_review.md.

> **Implementation status legend:**
> - ✅ = Implemented and tested
> - ⚠️ = Partially implemented or implemented with deviations
> - ❌ = Not yet implemented

---

## 1. Goals

1. Better memory (RAM) usage
2. Preserve the semantic of data types (type tree)
3. Support derived series (user-defined transforms, SISO and MIMO)
4. More decoupling from the rest of the code
5. Independent time domains for comparing datasets

---

## 2. High-Level Architecture

The engine is organized in four layers:

```
+-------------------------------------------------------------+
|                     Plugin API (typed)                       |
|  push_message(), register_schema(), create_transform(), ... |
+-------------------------------------------------------------+
|                     Logical Layer                            |
|  Type registry, schema management, topic/dataset metadata,  |
|  derived series dependency graph, time domain registry       |
+-------------------------------------------------------------+
|                     Storage Layer                            |
|  Chunk-based columnar storage, per-column encoding,         |
|  time-based eviction, validity bitmaps                      |
+-------------------------------------------------------------+
|                     Encoding Layer                           |
|  Delta encoding, RLE, dictionary encoding,                  |
|  typed arrays (float32/float64/int*/string)                 |
+-------------------------------------------------------------+
```

### Key principle: Logical / Physical separation

Inspired by Vortex and Arrow, the **logical layer** (type tree, schemas,
topic hierarchy) is completely decoupled from the **physical layer** (how
bytes are stored, which encoding is used). A `Quaternion` is a logical type;
physically it is four float32 columns sharing a delta-encoded timestamp column.

### Module Boundaries

> **Implementation note:** The planned five-module split was simplified into
> two libraries: `plotjuggler_base` (types, type tree, dataset metadata --
> zero dependencies) and `plotjuggler_engine` (everything else -- depends on
> Abseil and nanoarrow). This is a pragmatic consolidation; the logical
> separation between layers is preserved within the code. The
> `engine-derived`, `engine-time`, and `engine-bridge-legacy` modules are
> not yet implemented.

Five concrete modules, each independently testable:

1. ✅ **engine-core**: Owns datasets, topics, chunk metadata. Exposes
   `IDataWriter` and `IDataReader`. No UI dependencies.
   *Implemented as `DataEngine` + `DataWriter` + `DataReader` in the
   `plotjuggler_engine` library.*
2. ✅ **engine-storage**: Owns typed column buffers and chunk lifecycle.
   Arrow-compatible memory layout utilities.
   *Implemented as `RawBuffer`, `TypedColumnBuffer`, `TopicChunkBuilder`,
   `TopicChunk`, `TopicStorage` in the `plotjuggler_engine` library.*
3. ❌ **engine-derived**: Owns transform DAG, scheduler, dirty propagation.
   Depends on engine-core interfaces only.
4. ⚠️ **engine-time**: Owns time-domain registry and mapping functions for
   display.
   *Time domain creation and display offset are implemented in `DataEngine`.
   Visual alignment controls and automatic t0 alignment are not yet
   implemented.*
5. ❌ **engine-bridge-legacy**: Adapter from old PlotJuggler data interfaces
   to new APIs. Enables incremental migration without breaking existing
   plugins.

---

## 3. Data Hierarchy ✅

Three levels, each with associated metadata:

```
Dataset
 +-- metadata: source name, creation time, time domain ID
 |
 +-- Topic (AKA "channel")
 |    +-- metadata: schema ID, sample count, time range, byte size
 |    |
 |    +-- Series (columns / fields)
 |         +-- metadata: column type, encoding, null count
```

- **Dataset**: associated with a data source (file, stream). Owns a time domain.
- **Topic**: associated with a schema (type). Contains a columnar table.
- **Series**: individual typed columns within a topic's table.

This maps directly to MCAP's Schema -> Channel -> Message hierarchy.

**Implementation:** `DatasetInfo`, `TopicDescriptor`, `TopicMetadata`, and
`ColumnDescriptor` are implemented. `TopicMetadata` tracks: topic_id, name,
current_schema, dataset_id, time_range_min/max, total_row_count,
total_byte_size.

---

## 4. Type Tree and Schema Management

### 4.1 Central Registry with Late Discovery ✅

A hybrid approach:

- ✅ **Central type registry**: schemas are registered with unique IDs. Multiple
  topics sharing the same type reference the same schema ID.
  *Implemented in `TypeRegistry` with `register_schema()`,
  `register_or_get()`, `lookup()`, `find_by_name()`.*
- ❌ **Late discovery**: for schema-less formats (JSON, MessagePack, CBOR, XML),
  the schema is inferred from the first message and registered automatically.
  Subsequent messages may add new fields (see Schema Evolution below).
  *Not yet implemented -- requires plugin integration.*
- **Schema-based formats** (Protobuf, ROS, FlatBuffers, DDS): the schema is
  provided upfront by the data source plugin and is fixed.
  *The registration API is ready; plugin integration is Phase 5.*

### 4.2 Type Tree Representation ✅

The type tree preserves the full hierarchical structure. For the example:

```
Pose
  +-- frame_name: string
  +-- position: Position
  |     +-- x: float32
  |     +-- y: float32
  |     +-- z: float32
  +-- rotation: Quaternion
        +-- w: float32
        +-- x: float32
        +-- y: float32
        +-- z: float32
```

This tree is stored in the registry as a recursive structure. Each node has:
- A name
- A type kind (primitive, struct, array, enum)
- For structs: an ordered list of child fields
- For arrays: element type + optional fixed size
- For primitives: the concrete type (float32, float64, int32, uint8, string,
  bool, ...)
- For enums: a mapping from integer values to string names
- **Semantic tags** (optional): a set of string annotations that describe the
  semantic role of the node (e.g., `"quaternion"`, `"pose"`, `"color_rgb"`).
  Tags are distinct from the struct name: a struct named `MyRotation` can be
  tagged `"quaternion"` so that MIMO transforms discover it by semantic role,
  not by name. Tags are set by the data source plugin or inferred from the
  schema (e.g., Protobuf annotations, ROS message type names).

**Implementation:** All of the above is implemented in `TypeTreeNode`
(`pj/base/type_tree.hpp`) with factory functions `make_primitive()`,
`make_struct()`, `make_array()`, `make_enum()`. Helper functions
`flatten_field_paths()` and `count_leaf_fields()` are implemented for
converting the tree to flat column lists.

The type tree enables:
- MIMO transforms that operate on semantically meaningful groups (e.g.,
  quaternion-to-RPY knows it needs 4 floats that form a Quaternion)
- GUI display that reflects the original structure (drag "robot_pose" and
  see all 8 fields grouped by their parent types)
- Automatic encoding selection (enums get RLE, strings get dictionary, etc.)

### 4.3 Schema Evolution (Additive) ✅

For schema-less formats, new fields may appear mid-stream:

- New columns are added to the topic's table with nulls for all past data.
- Old chunks simply lack the new column; queries return null for those ranges.
- The registry tracks the "current" schema version per topic.
- Fields **cannot** change type or be removed mid-stream.

**Implementation:** `TypeRegistry::evolve_schema()` validates additive-only
changes: old fields must exist with same types in the new tree; new fields
are allowed; type changes and removals are rejected. Tested in
`type_registry_test.cpp`.

---

## 5. Storage Layer

### 5.1 Chunk-Based Columnar Storage ✅

Data is stored in **chunks** -- columnar tables inspired by Prometheus TSDB,
Rerun, and Arrow RecordBatches.

Each chunk contains:
- A shared timestamp column (one copy, not duplicated per field)
- One column per field in the topic's schema
- Validity bitmaps for nullable/sparse columns
- Per-column encoding metadata
- Per-chunk statistics (see Section 5.6)

```
Chunk for topic "robot_pose":
+------------+--------+--------+--------+--------+--------+--------+--------+---...
| timestamp  | pos.x  | pos.y  | pos.z  | rot.w  | rot.x  | rot.y  | rot.z  | frame
| (delta)    | f32    | f32    | f32    | f32    | f32    | f32    | f32    | dict
+------------+--------+--------+--------+--------+--------+--------+--------+---...
| 0          | 1.0    | 2.0    | 0.5    | 1.0    | 0.0    | 0.0    | 0.0    | "base"
| +10ms      | 1.1    | 2.0    | 0.5    | 0.99   | 0.01   | 0.0    | 0.0    | "base"
| +10ms      | 1.2    | 2.1    | 0.5    | 0.98   | 0.02   | 0.0    | 0.0    | "base"
| ...        | ...    | ...    | ...    | ...    | ...    | ...    | ...    | ...
+------------+--------+--------+--------+--------+--------+--------+--------+---...
```

This solves Problem 1 from the original plan: **timestamps are stored once
per topic, not once per field**.

Chunk sizing: **fixed row-count** in v1 (simpler, predictable). Byte-based
adaptive chunking is deferred.

**Implementation:** `TopicChunk` (sealed, immutable) and `TopicChunkBuilder`
(mutable, building) in `pj/engine/chunk.hpp`. The builder uses
`TypedColumnBuffer` instances for accumulation, with two ingestion APIs:
- **Row-at-a-time**: `begin_row`, `set_*`, `finish_row` -- stats are
  computed incrementally per value.
- **Bulk columnar**: `append_timestamps()`, `append_column_*()`,
  `append_column_validity()`, `finish_bulk_append()` -- data is memcpy'd,
  stats are computed in a single pass after all data and validity bitmaps
  are in place.

Auto-sealing triggers when `max_chunk_rows` is reached.

### 5.2 Chunk Lifecycle and Threading Model ⚠️

PlotJuggler uses separate plugin threads for data ingestion with periodic
batch transfer to the GUI (main) thread. The chunk-based design formalizes
this pattern:

```
  Plugin threads (one per source)       Main thread (GUI + commit)
  ================================      ==========================

  [building chunk in staging buffer]    [reading sealed chunks]
  push_back(msg)                        for chunk in topic.sealed_chunks:
  push_back(msg)                            render(chunk)
  push_back(msg)
  ...
  chunk full or flush timer fires
       |
       v
  [seal chunk, enqueue for transfer]
                                        [drain staging queues in deterministic order]
                                        [append sealed chunks to topic storage]
                                        [run derived series scheduler]
                                        [enforce retention window]
                                        [render frame]
```

Chunk states:

- ✅ **Building**: the mutable chunk being filled by the writer. Owned
  exclusively by the plugin thread. Never accessed by the main thread.
  *Implemented via `TopicChunkBuilder` in `DataWriter`.*
- ⚠️ **Staged**: sealed and enqueued for transfer. Immutable. Waiting to be
  drained by the main thread.
  *Sealing is implemented. Staging queues (`PluginStagingContext`,
  `SPSCQueue`) are not yet implemented -- `DataWriter::flush()` returns
  sealed chunks directly, and `DataEngine::commit_chunks()` appends them
  synchronously. The multi-threaded staging model is deferred to Phase 5
  (plugin integration).*
- ✅ **Committed**: appended to `TopicStorage.sealed_chunks` on the main thread.
  Immutable. Readable by viewers and transforms.
  *Implemented in `TopicStorage::append_sealed_chunk()` and
  `DataEngine::commit_chunks()`.*
- ✅ **Evicted**: dropped when its time range falls outside the retention window.
  *Implemented in `TopicStorage::evict_before()` and
  `DataEngine::enforce_retention()`.*

#### Ordering contract (v1)

Each plugin has a dedicated staging queue. On the main thread commit point
(once per frame):

1. Drain all staging queues in a **fixed, deterministic order** (e.g., order
   of plugin registration).
2. For each drained chunk, append to the corresponding `TopicStorage`.
3. After all chunks are committed, run the derived series scheduler.
4. Enforce retention on all topics.

This guarantees deterministic execution order on the main thread with no
locks on the read path. The only synchronization is the staging queue
(lock-free SPSC queue per plugin, or a simple mutex-guarded push/drain).

> **Implementation note:** Steps 2 and 4 are implemented. Steps 1 and 3
> require `PluginStagingContext` (not yet implemented) and the derived
> engine (Phase 2), respectively. The current `commit_chunks()` API accepts
> a vector of (topic_id, chunk) pairs and appends them synchronously.

### 5.3 Time-Based Eviction ✅

Each chunk tracks its time range [t_min, t_max]. Eviction is relative to
the **newest ingested sample time** (not wall-clock time), which ensures
correct behavior for both live streaming and offline playback:

```
t_keep_min = newest_timestamp_in_topic - retention_window
```

All chunks with `t_max < t_keep_min` are evicted. Eviction is O(1) per
chunk (pop front of the chunk deque). Eviction never removes from the middle
-- retained data is always a contiguous time range.

**Implementation:** `TopicStorage::evict_before(t_keep_min)` pops chunks
from the front of the deque. `DataEngine::enforce_retention()` iterates all
topics and calls evict. Tested in `topic_storage_test.cpp`.

### 5.4 Variable-Length Arrays ❌

For types like `vector<Pose> poses`, the primary access pattern is
"plot `poses[3].position.x` over time" (fixed index over time).

Strategy: expand array elements into indexed columns at ingest time:
- `poses[0].position.x`, `poses[0].position.y`, ...
- `poses[1].position.x`, `poses[1].position.y`, ...
- etc.

When a longer array is first seen, new columns are added dynamically.
Validity bitmaps track which indices exist at each timestamp (sparse table).
This reuses the additive schema evolution mechanism.

> **Implementation note:** The type tree supports array nodes
> (`make_array()`), but the writer does not yet handle dynamic array
> expansion at ingest time. The validity bitmap infrastructure is in place.

#### Column explosion guard

To prevent unbounded column growth from noisy variable-length data, enforce
a **configurable max array expansion limit** per field (e.g., max 64
elements). Arrays exceeding this limit are truncated at the storage level.
The limit is exposed as a per-topic configuration with a sensible default.

**Truncation visibility**: when array elements are dropped due to the
expansion limit, the engine must record this in per-topic metadata:
- `max_observed_array_length`: the largest array length actually seen.
- `truncated_sample_count`: number of samples where elements were dropped.
- These are queryable via `TopicMetadata` so that the GUI can display a
  warning (e.g., "poses: 12 samples truncated, max observed length 128,
  limit 64"). Data is never silently lost without indication.

This is a storage-level policy. The viewer may additionally choose to show
only a subset of expanded columns.

### 5.5 ScatterXY ❌

Two modes:
1. **Reference mode** (common): a view pairing two existing time-indexed
   series for XY plotting. No separate storage. Purely a viewer-level concept.
2. **Value mode** (rare): actual stored XY pairs, generated by a data source.
   Stored as a simple two-column table without monotonic-time constraint.

### 5.6 Per-Chunk Statistics ✅

Maintained incrementally during append at near-zero cost:

| Statistic | Purpose |
|---|---|
| `t_min`, `t_max` | **Required** for range query chunk intersection and retention. |
| `min_value`, `max_value` per numeric column | **Required** for fast Y-axis bounds recomputation in visible ranges, especially during streaming updates. |
| `row_count` | Chunk size bookkeeping. |
| `null_count` per column | Skip fully-null columns; inform encoding selection. |
| `run_count` per column | If low relative to rows, prefer RLE encoding at seal time. |
| `is_constant` per column | Fast-path: derivative of a constant column is trivially zero. |

`t_min`/`t_max` and numeric `min_value`/`max_value` are mandatory. The rest
are maintained because they are essentially free (a few comparisons per
append) and enable encoding selection heuristics at seal time.

**Implementation:** All six statistics are implemented in `ColumnStats` and
`ChunkStats` (`pj/engine/chunk.hpp`). They are tracked incrementally during
`TopicChunkBuilder` append operations and finalized at seal time. Tested
in `chunk_test.cpp`.

---

## 6. Encoding Layer

### 6.1 Strategy: Moderate Compression ✅

The goal is significant memory savings with negligible decode cost. Every
read ultimately produces raw values for rendering, so per-value decode cost
matters.

### 6.2 Encoding Selection (per-column, at seal time) ⚠️

When a chunk is sealed, per-column statistics guide encoding selection:

| Data type | Encoding | Condition | Status |
|---|---|---|---|
| **Timestamps** | Delta encoding | Always (monotonic, near-zero decode cost). | ❌ **Not implemented.** Timestamps are stored as raw `int64_t` vectors. |
| **float32 / float64** | Raw typed storage | Always. Preserve original width. | ✅ Implemented. |
| **int8/16/32/64, uint*** | Raw typed storage | Always. Preserve original width. | ⚠️ Narrow integers (int8, int16, uint8-32) are **widened** to int64/uint64 at the `StorageKind` level. See deviation note below. |
| **bool** | Packed bitfield | Always. 1 bit per value. | ✅ Implemented. Constant encoding used when all values are equal; packed bitfield otherwise. |
| **Any column** | Run-Length Encoding | When `run_count` is low relative to `row_count`. | ❌ Not implemented (Phase 4). |
| **Strings** | Dictionary encoding | Always (default). See note below. | ✅ Implemented with narrowed index width (1/2/4 bytes). |
| **Sparse columns** | Validity bitmap | When `null_count > 0`. | ✅ Implemented with lazy initialization. |

> **Deviation: integer width preservation.** The plan specifies "preserve
> original width" for all integer types. The implementation defines 7
> `StorageKind` values: `kFloat32`, `kFloat64`, `kInt32`, `kInt64`,
> `kUint64`, `kBool`, `kString`. Narrow integers (int8, int16) are widened
> to int64; unsigned integers (uint8, uint16, uint32) are widened to uint64.
> This simplifies the encoding and decode paths at the cost of some memory
> overhead for narrow integer columns. The plan's `int8/16` raw storage
> target is not met.
>
> **Deviation: timestamps not delta-encoded.** The plan lists delta encoding
> for timestamps as a Phase 1 baseline encoding. The implementation stores
> timestamps as plain `std::vector<int64_t>` in `TopicChunk`. The delta
> encoding infrastructure exists in `encoding.hpp` (used for Frame of
> Reference encoding on integer columns), but is not applied to timestamps.
>
> **Addition: Frame of Reference encoding.** The implementation adds
> Frame-of-Reference (FOR) encoding for signed integer columns: stores the
> column minimum as a reference value, then encodes offsets using narrowed
> width (1, 2, or 4 bytes). This was not explicitly in the plan but aligns
> with the spirit of moderate compression. Applied automatically at seal
> time when the value range fits in a narrower width than the original.
>
> **Addition: Constant encoding.** Single-value constant encoding is applied
> to any column where `is_constant == true` at seal time (bools, integers,
> floats). This was implicitly covered by the plan's `is_constant` stat but
> not listed as a separate encoding.

Encoding is selected per-column, per-chunk. Different chunks of the same
column may use different encodings if data characteristics change.

**String encoding trade-off**: Dictionary encoding is highly effective for
enum-like strings (small set of repeated values) but can degrade for
high-cardinality free-form strings (log messages, filenames) where the
dictionary grows large with few repeated entries. In v1, dictionary
encoding is the default for all string columns. If benchmarks show
significant overhead for free-form string columns, a fallback to raw
string storage (offset + data buffers) can be added in Phase 4.

### 6.3 Why Not More Aggressive Compression?

- **Gorilla/XOR for floats**: measurable decode cost per sample; marginal
  benefit given that preserving float32 already halves memory vs. double.
- **ALP (Adaptive Lossless floating-Point)**: interesting (Vortex uses it),
  but the decode cost is not justified for a real-time visualization tool.
- **General-purpose compression (LZ4, Snappy, ZSTD)**: opaque to queries,
  prevents any compute push-down, requires full decompression.

These may be evaluated post-v1 if benchmarks show further memory pressure.

### 6.4 Memory Savings Estimate (Hypothesis)

For the `robot_pose` example (7 float32 fields + 1 string field per message),
compared to the current `std::deque<std::pair<double, double>>` approach:

| Cost per sample | Current | New (estimated) |
|---|---|---|
| Timestamps | 8 x 8 bytes = 64 B | 1 x ~2 B (delta) |
| Values | 8 x 8 bytes = 64 B | 7 x 4 B (f32) + ~4 B (dict) = ~32 B |
| Container overhead | ~16 B/elem x 8 = ~128 B | Columnar, amortized ~0 B |
| **Total per sample** | **~256 B** | **~34 B** |

> **Implementation reality check:** Because timestamps are not yet
> delta-encoded (stored as raw int64 = 8 B per sample), the actual per-sample
> cost for timestamps is ~8 B, not ~2 B. The current implementation achieves
> approximately **~40 B per sample** for this data shape (8 B timestamp +
> 28 B float32 values + ~4 B dictionary string). The delta encoding gap
> accounts for ~6 B/sample of unrealized savings.

**This is a hypothesis, not a guarantee.** Actual savings depend on data
shape, string cardinality, and sparsity. These estimates must be validated
with benchmark gates (see Section 10) on representative datasets before
claiming specific ratios.

**Phase breakdown of estimated savings:**
- **Phase 1** delivers: shared timestamps (64 B -> 8 B raw int64), type
  preservation (64 B -> 28 B for f32 fields), columnar layout (~128 B ->
  ~0 B container overhead), delta-encoded timestamps (8 B -> ~2 B),
  dictionary-encoded strings. Estimated ~256 B -> ~38 B per sample.
- **Phase 4** adds: RLE for constant/low-cardinality columns (further
  reduces value storage for enum-like fields). Marginal improvement
  on top of Phase 1 for this data shape; larger impact on topics with
  many constant or enum fields.

The ~34 B estimate in the table above reflects full Phase 1 + Phase 4.
Phase 1 alone is expected to achieve the majority of the savings.

---

## 7. Derived Series and Transforms ❌

> **Not yet implemented.** This is Phase 2. The interfaces and data
> structures described below remain the design target.

### 7.1 Dependency Graph

Derived series form a DAG (Directed Acyclic Graph):

```
  [raw: rotation.*] ---> [MIMO: quat_to_rpy] ---> [derived: roll]
                                                ---> [derived: pitch]
                                                ---> [derived: yaw]
                                                        |
                                              [SISO: derivative] ---> [derived: yaw_rate]
                                                        |
                                              [SISO: low_pass]   ---> [derived: yaw_rate_filtered]
```

Circular dependencies are prohibited. The graph is validated on transform
creation (topological sort) and rejected if a cycle would be introduced.

### 7.2 Evaluation Strategy

#### Default (v1): Batched, Lazy, Chunk-Aligned

The default execution model for v1, executed on the main thread as part of
the commit cycle:

1. **Dirty tracking**: when new chunks are committed for a source series,
   all downstream derived series in the DAG are marked dirty.
2. **Batched**: dirty flags accumulate. The derived scheduler runs once per
   frame, after all staging queues are drained and committed.
3. **Lazy**: only derived series that are **currently displayed** by a
   viewer are recomputed. Hidden/unused derived series stay dirty until
   viewed.
4. **Chunk-aligned incremental**: transforms process only the new chunks
   since their last computation. Output is also chunk-aligned.

#### Execution order within a frame

```
1. Drain all plugin staging queues (deterministic order)
2. Commit sealed chunks to TopicStorage
3. Topological-order walk of dirty derived nodes:
   a. Skip nodes not currently displayed (lazy)
   b. For each active dirty node:
      - If node supports incremental: process new input chunks only
      - If node does not support incremental: full batch recompute
   c. Mark node clean, propagate dirty to downstream nodes
4. Enforce retention on all topics (source + derived)
5. Render frame
```

This is single-threaded and deterministic within the main thread. Plugin
threads run concurrently but only interact via staging queues.

#### Batch recompute (on-demand)

Full recompute is available as a separate mode, triggered by:
- Transform parameter changes
- Data reload
- Explicit user request
- Correctness validation (incremental output must match batch output within
  numeric tolerance)

Batch recompute serves as the **correctness oracle** for incremental mode.

### 7.3 Transform Interface

```cpp
// Conceptual interface (not final)
class ITransformOp {
    // Declare inputs: which series/groups this transform reads
    virtual InputSpec declare_inputs() = 0;

    // Declare outputs: what series/groups this transform produces
    virtual OutputSpec declare_outputs() = 0;

    // Whether this transform supports incremental computation
    virtual bool supports_incremental() = 0;

    // Process new chunk(s). For transforms that need history across chunk
    // boundaries (moving average, derivative), previous_context provides
    // read-only access to the tail of the previous output chunk.
    virtual Chunk compute_incremental(
        span<const Chunk> new_input_chunks,
        optional<ChunkTail> previous_context) = 0;

    // Full recompute over all available data. Reference implementation
    // for correctness validation.
    virtual void compute_batch(
        const RangeCursor& full_input,
        DataWriter& output) = 0;
};
```

SISO transforms (derivative, filter, integral) take one series, produce one.
MIMO transforms (quaternion-to-RPY) take a group of series (identified by
type in the type tree), produce a group.

The `previous_context` parameter is a **hard requirement** in the interface.
Transforms that need cross-chunk state (derivative needing the last sample,
moving average needing the last N samples) must be able to access the tail
of their previous output without reading the entire history.

`ChunkTail` semantics (see Section 11 for the concrete structure):
- Each transform declares the number of trailing rows it requires (e.g.,
  derivative needs 1, moving average of N needs N-1).
- The derived engine stores a `ChunkTail` per derived node, updated after
  each incremental computation.
- The `ChunkTail` is a **copy** of the tail data, not a reference into
  sealed chunks. This ensures it survives retention eviction of the
  underlying source data.
- The window size is declared by the transform at registration time and
  is immutable.

---

## 8. Time Domains ⚠️

### 8.1 V1: Simple Offset

Each Dataset owns a time domain. A time domain is:

```cpp
struct TimeDomain {
    TimeDomainId id;
    std::string name;           // e.g., "Experiment A"
    Timestamp display_offset;   // nanoseconds (using Timestamp alias)
    // display_time = raw_time - display_offset
};
```

The offset can be:
- **Automatic**: set to the first sample's timestamp, so data starts at t=0.
- **Manual**: set by the user in the GUI.

This is intentionally **visual-only**. It does not change stored source
timestamps. No cross-domain algebra or join semantics.

**Implementation:** `TimeDomain` struct and `DataEngine::create_time_domain()`,
`get_time_domain()`, `set_display_offset()` are implemented. Automatic t0
alignment and GUI visual alignment controls are not yet implemented.

### 8.2 Cross-Dataset Comparison

When two Datasets are loaded with different time domains, their data is
displayed on a shared axis with offsets applied. This enables side-by-side
comparison of experiments recorded at different absolute times.

> **Not yet implemented** -- requires UI integration (Phase 3).

### 8.3 Future Extensions (out of scope for v1)

- Time scaling (compare experiments at different speeds)
- Named alignment points (align by event rather than start time)
- Multiple timeline columns per chunk (Rerun-style)

---

## 9. Plugin API

### 9.1 Design Principle: High-Level Typed API ✅

Plugins never see raw columnar storage, encodings, or chunks. They interact
through a typed API that hides all internal details. Arrow-compatible
internal buffers are used, but Arrow objects are not exposed as public
plugin API types.

**Implementation:** `DataWriter` and `DataReader` expose typed APIs. Writers
use `begin_row()` / `set_*()` / `finish_row()` or `append_scalar()` for
row-at-a-time ingestion, and `append_columns()` for bulk columnar ingestion.
Readers use `range_query()` and `latest_at()`. Internal encoding and chunk
details are fully hidden.

### 9.2 Core Interfaces ⚠️

```cpp
// Lightweight handle for hot-path writes: no per-message string lookup.
struct TopicWriteHandle {
    TopicId topic_id;
    std::vector<FieldId> field_ids; // Resolved once at bind time
};

// Scalar convenience handle (equivalent to current time/value push style).
struct ScalarSeriesHandle {
    TopicId topic_id;   // Backed by a normal topic with one numeric field
    FieldId value_field;
};

class IDataWriter {
public:
    virtual SchemaId register_schema(const TypeTree& type_tree) = 0;
    virtual TopicId register_topic(DatasetId, const TopicDescriptor&) = 0;
    virtual TopicWriteHandle bind_topic_writer(TopicId) = 0;
    virtual FieldId resolve_field(TopicId, std::string_view field_path) = 0;
    virtual void append(TopicId, const MessageView&) = 0;
    virtual void append_fast(const TopicWriteHandle&, const MessageView&) = 0;
    virtual void append_batch(TopicId, span<const MessageView>) = 0;
    virtual void append_batch_fast(const TopicWriteHandle&, span<const MessageView>) = 0;

    // Scalar convenience API.
    virtual ScalarSeriesHandle register_scalar_series(
        DatasetId, std::string_view topic_name, NumericType value_type) = 0;
    virtual void append_scalar(const ScalarSeriesHandle&, Timestamp t, NumericValue v) = 0;
};

class IDataReader {
public:
    // List / inspect
    virtual vector<DatasetId> list_datasets() const = 0;
    virtual vector<TopicId> list_topics(DatasetId) const = 0;
    virtual const TypeTree& get_type_tree(TopicId) const = 0;
    virtual TopicMetadata get_metadata(TopicId) const = 0;

    // Query
    virtual RangeCursor range_query(const QueryRange&) const = 0;
    virtual LatestValue latest_at(const QueryPoint&) const = 0;
};

class IDerivedEngine {
public:
    virtual NodeId add_transform(const TransformDescriptor&) = 0;
    virtual void remove_node(NodeId) = 0;
    virtual void recompute(NodeId, RecomputeMode) = 0;
};
```

> **Implementation status by interface:**
>
> **IDataWriter:** Implemented as concrete `DataWriter` class (not a virtual
> interface). The API deviates from the plan:
> - ✅ `register_schema()`, `register_topic()`, `bind_topic_writer()`,
>   `resolve_field()` -- implemented as planned.
> - ⚠️ `append()` / `append_fast()` / `append_batch()` /
>   `append_batch_fast()` with `MessageView` -- **not implemented**. Instead,
>   the writer provides two ingestion APIs:
>   1. **Row-at-a-time**: `begin_row(topic_id, timestamp)`,
>      `set_float32()`, `set_string()`, etc., `finish_row()`.
>   2. **Bulk columnar**: `append_columns(topic_id, timestamps, columns)`
>      accepts raw pointers to contiguous column data via `ColumnData`
>      descriptors. Handles chunk boundary splitting transparently.
>   Both are more explicit than the planned `MessageView`-based API.
> - ✅ `register_scalar_series()`, `append_scalar()` -- implemented as
>   planned.
> - ✅ `flush()` / `flush_all()` -- additional methods for explicit chunk
>   sealing (not in the plan's interface but needed for the commit cycle).
>
> **IDataReader:** Implemented as concrete `DataReader` class (not a virtual
> interface).
> - ✅ `list_datasets()`, `list_topics()`, `get_type_tree()`,
>   `get_metadata()` -- implemented as planned.
> - ✅ `range_query()`, `latest_at()` -- implemented as planned, returning
>   `absl::StatusOr` for error handling.
>
> **IDerivedEngine:** ❌ Not yet implemented (Phase 2).

Write-path ergonomics are intentionally split:
- ⚠️ **Simple path**: `append()` / `append_batch()` with field-name based
  `MessageView` construction. Best for prototypes and low-rate inputs.
  *Not implemented. Replaced by row-builder API.*
- ✅ **Fast path**: resolve/bind once (`bind_topic_writer`, `FieldId`) and use
  `append_fast()` / `append_batch_fast()`. No per-message field-name string
  comparisons in the hot loop.
  *The `bind_topic_writer()` / `resolve_field()` pattern is implemented.
  The row-builder uses field indices internally after binding.*
- ✅ **Scalar path**: `register_scalar_series()` + `append_scalar()` for the
  current PlotJuggler-style time/value append workflow.

Row construction may be performed field-by-field by the writer, but v1
commit semantics are still full-row: at row finalization, every field must
have a defined state for that row (explicit value, explicit null, or writer
error per policy). Missing fields never imply carry-forward from prior rows.

**Implementation:** `TopicChunkBuilder::finish_row()` auto-fills any unset
columns with null, enforcing full-row semantics.

Two query modes (inspired by Rerun):
- ✅ **RangeQuery**: returns a cursor/iterator over all samples in [t_min,
  t_max]. Binary search on chunk time bounds, scan only intersecting chunks.
  Primary path for time-series plotting.
  *Implemented as `RangeCursor` with per-row (`advance()`) and per-chunk
  (`for_each_chunk()`) iteration paths.*
- ✅ **LatestAtQuery**: returns the most recent value at or before time t.
  Binary search on chunk max times + per-chunk binary search. Useful for
  dashboard views and MIMO transforms needing aligned inputs.
  *Implemented as `latest_at()` with reverse chunk/row scan.*

Renderers should consume iterators/cursors, not materialize full vectors.

### 9.3 ABI Considerations

C++ virtual interfaces are sufficient for v1. All plugins are compiled
against the same PlotJuggler version. If binary compatibility across
compiler versions becomes necessary in the future, a C ABI wrapper can
be added.

> **Implementation note:** The implementation uses concrete classes rather
> than pure virtual interfaces. This is simpler for v1. Virtual interfaces
> can be extracted later if needed for testing with mocks or for
> dependency injection.

---

## 10. Performance and Correctness Targets

Define measurable acceptance criteria **before** implementation lock:

### Performance targets

| Metric | Target (to be baselined) | Status |
|---|---|---|
| bytes/sample by data type and topic shape | Benchmark vs. current engine | ⚠️ Benchmark infrastructure exists (`read_benchmark.cpp`) but no comparison with current engine yet. |
| Ingest throughput (rows/sec) in streaming | At least on par with current | ✅ Benchmarked. See Section 17 below. |
| Max append latency per batch | Within frame budget | ✅ Benchmarked: 0.16ms per 100K rows via bulk path. |
| Derived incremental update latency | Within frame budget | ❌ Derived engine not implemented. |
| Range query latency for plotting | At least on par with current | ⚠️ Read benchmark exists. |

### Correctness criteria

- ✅ Type tree fidelity: round-trip schema registration and retrieval.
  *Tested in `type_tree_test.cpp` and `type_registry_test.cpp`.*
- ❌ Derived DAG correctness: incremental output matches batch output within
  numeric tolerance.
  *Derived engine not implemented.*
- ✅ Retention correctness: no data outside retention window, no gaps in
  retained data.
  *Tested in `topic_storage_test.cpp`.*
- ✅ Schema evolution: additive field changes handled without data corruption.
  *Tested in `type_registry_test.cpp`.*

---

## 11. Concrete Data Structures (v1) ⚠️

```cpp
using DatasetId    = uint32_t;    // ✅ Implemented in pj/base/types.hpp
using TopicId      = uint32_t;    // ✅ Implemented
using FieldId      = uint32_t;    // ✅ Implemented
using ChunkId      = uint64_t;    // ✅ Implemented
using TimeDomainId = uint32_t;    // ✅ Implemented
using SchemaId     = uint32_t;    // ✅ Implemented
using PluginId     = uint32_t;    // ✅ Implemented

// All timestamps are stored as int64 nanoseconds. This is a locked v1
// decision. int64_t nanoseconds covers ~292 years of range with nanosecond
// precision, sufficient for all robotics and data logging use cases.
using Timestamp = int64_t;  // nanoseconds since epoch  // ✅ Implemented

struct ChunkStats {          // ✅ Implemented in pj/engine/chunk.hpp
    Timestamp t_min;
    Timestamp t_max;
    uint32_t row_count;
    // Per-column stats (indexed by FieldId)
    struct ColumnStats {
        uint32_t null_count;
        uint32_t run_count;
        bool is_constant;
        // For numeric columns only. Stored as double to uniformly cover
        // all numeric types (int*, uint*, float32, float64). Updated
        // incrementally during append; used for fast Y-axis bounds.
        std::optional<double> min_value;
        std::optional<double> max_value;
    };
    std::vector<ColumnStats> column_stats;
};

// For enum fields: maps integer wire values to human-readable names.
// Stored in the type registry as part of the type tree node.
struct EnumMapping {          // ✅ Implemented in pj/base/type_tree.hpp
    std::unordered_map<int64_t, std::string> value_to_name;
    std::unordered_map<std::string, int64_t> name_to_value;
};

// Tracks schema evolution per topic. Each entry represents a version of
// the schema, enabling correct interpretation of old chunks that may
// lack columns added in later versions.
struct SchemaVersionEntry {   // ❌ Not implemented as a standalone struct.
    SchemaId schema_id;       //    Schema evolution is handled inside
    uint32_t version;         //    TypeRegistry::evolve_schema() which
    std::shared_ptr<TypeTreeNode> type_tree; // replaces the tree in-place.
    std::vector<FieldId> field_ids;
};

struct SchemaVersionHistory { // ❌ Not implemented. No version history
    TopicId topic_id;         //    tracking per topic. evolve_schema()
    std::vector<SchemaVersionEntry> versions; // validates and replaces.
    // current() returns versions.back()
};

// Tail context provided to incremental transforms that need cross-chunk
// history (e.g., derivative needs the last sample, moving average needs
// the last N samples).
struct ChunkTail {            // ❌ Not implemented (Phase 2: derived engine).
    uint32_t requested_rows;
    span<const Timestamp> timestamps;
    std::vector<span<const uint8_t>> column_values;
};

struct ColumnBuffer {         // ✅ Implemented as TypedColumnBuffer
    FieldId field_id;         //    in pj/engine/column_buffer.hpp.
    ArrowType logical_type;   //    Uses PrimitiveType + StorageKind
    EncodingType encoding;    //    instead of ArrowType.
    Buffer values;
    Buffer validity;
    Buffer offsets;
};

struct TopicChunk {           // ✅ Implemented in pj/engine/chunk.hpp.
    ChunkId id;               //    Deviates: timestamps stored as
    TopicId topic_id;         //    std::vector<int64_t> (not encoded Buffer).
    SchemaId schema_version;  //    Columns stored as encoded RawBuffers
    ChunkStats stats;         //    with separate encoding_data variant.
    Buffer timestamp_values;
    std::vector<ColumnBuffer> columns;
};

struct TopicStorage {         // ✅ Implemented in pj/engine/topic_storage.hpp.
    TopicId topic_id;
    SchemaId current_schema;
    TimeDomainId time_domain_id;
    int64_t retention_window_ns;
    std::deque<TopicChunk> sealed_chunks;
    // Note: there is no active_chunk here. The mutable building chunk
    // is owned by the plugin thread via PluginStagingContext (below).
};

// Owned by the plugin thread. One per plugin. Not accessed by the main thread
// except to drain sealed_queue.
struct PluginStagingContext {  // ❌ Not implemented. Multi-threaded staging
    PluginId plugin_id;       //    is deferred to Phase 5 (plugin integration).
    TopicChunk building_chunk;//    The building chunk is currently managed
    SPSCQueue<TopicChunk> sealed_queue; // by DataWriter directly.
};
```

Notes:
- `TopicChunk` does not have a `sealed` flag. The sealed/building
  distinction is structural: `building_chunk` lives in `PluginStagingContext`
  (plugin thread), while `sealed_chunks` lives in `TopicStorage`
  (main thread).
- `PluginStagingContext` is the only structure shared between plugin and
  main threads, and only via the `sealed_queue`. The `building_chunk` is
  exclusively owned by the plugin thread.
- `schema_version` on each chunk tracks which schema was active when the
  chunk was written. Old chunks may have fewer columns than the current
  schema.
- `ChunkStats` is populated incrementally during append and finalized at
  seal time.

---

## 12. v1 Locked Decisions

1. ✅ **Arrow integration**: Arrow-compatible internal buffers. Do not expose
   Arrow objects directly as public plugin API types.
2. ✅ **Chunk sizing**: Fixed row-count in v1. Byte-based adaptive chunking
   deferred.
3. ❌ **Derived scheduling**: Batched lazy on main thread (once per frame) as
   default. Batch recompute available on demand as correctness oracle.
   *Derived engine not implemented.*
4. ❌ **Variable-length arrays**: Expand to indexed columns at ingest, with
   configurable max expansion limit. Viewer may additionally filter.
   *Type tree supports arrays; writer expansion not implemented.*
5. ✅ **Update semantics**: Column-by-column write APIs are allowed, but row
   commit semantics are full-row in v1. No implicit field-level carry-forward
   from prior rows.
6. ✅ **Timeline model**: One timestamp index per topic chunk in v1. Additional
   timeline/index columns deferred.
7. ⚠️ **Threading**: Plugin threads build chunks independently. Main thread
   drains staging queues in deterministic order, runs derived scheduler,
   enforces retention, and renders. Single-threaded commit path.
   *Commit and retention are implemented. Staging queues and derived
   scheduler are not.*
8. ✅ **Schema evolution**: Additive only. New fields allowed. Type changes
   and field removal prohibited.
9. ✅ **String encoding**: Dictionary encoding is the default for all string
   columns.
10. ✅ **Retention**: Time-window based, relative to newest ingested sample
    time (not wall-clock time).
11. ✅ **Timestamp representation**: `int64_t` nanoseconds since epoch. This
    replaces the current `double` representation. Covers ~292 years of range
    with nanosecond precision.
12. ⚠️ **Writer API tiers**: v1 exposes both a simple string-based append API
    and a pre-bound handle fast path (`FieldId`-based) to avoid per-message
    string comparisons in high-rate streams.
    *Fast path is implemented. Simple path uses row-builder instead of
    `MessageView`.*
13. ✅ **Scalar convenience API**: v1 includes a first-class scalar
    `append_scalar(time, value)` path for single-value time-series ingestion.
14. ✅ **Semantic tags**: Type tree nodes carry an optional set of string tags
    for semantic role discovery by transforms (e.g., `"quaternion"`), distinct
    from the struct name.
15. ✅ **Bulk ingest API**: `DataWriter::append_columns()` accepts raw
    columnar data pointers. Engine core is Arrow-free; Arrow adapter is a
    separate layer linked PRIVATE. See Section 17.

---

## 13. Implementation Phases

### Phase 1: Core model + typed chunk store (engine-core, engine-storage) -- ⚠️ MOSTLY COMPLETE

- ✅ Dataset / topic / field metadata
- ✅ Type tree registry with hybrid discovery
- ✅ Typed append / read paths (row-at-a-time + bulk columnar)
- ✅ Shared timestamp column
- ✅ Chunk lifecycle (build, seal, commit, evict)
- ✅ Per-chunk statistics (row-at-a-time + bulk with null-aware computation)
- ✅ Range and latest-at queries
- ✅ Bulk columnar ingest API (`DataWriter::append_columns()`)
- ✅ Arrow import utilities (`arrow_import.hpp`)
- **Baseline encodings** (locked v1 decisions, not deferred):
  - ❌ Delta encoding for timestamp columns
  - ✅ Dictionary encoding for string columns
  - ✅ Packed bitfields for bool columns
  - ✅ Validity bitmaps for nullable/sparse columns
  - ⚠️ Raw typed storage for all numeric types (preserving original width)
    *Narrow integers widened to int64/uint64.*

**Remaining Phase 1 work:**
1. Delta encoding for timestamps
2. Optionally: preserve narrow integer widths (int8, int16, uint8-32) instead
   of widening

### Phase 2: Derived DAG engine (engine-derived) -- ❌ NOT STARTED

- Node / edge model with topological sort
- Cycle detection on registration
- Incremental chunk-aligned scheduling
- Batch recompute path
- Correctness parity tests (incremental vs. batch)

### Phase 3: Time domains + UI integration (engine-time) -- ⚠️ PARTIALLY STARTED

- ✅ Domain IDs and offset mapping
- ❌ Visual alignment controls
- ❌ Per-dataset automatic t0 alignment

### Phase 4: Advanced memory optimizations -- ⚠️ PARTIALLY DONE (AHEAD OF SCHEDULE)

- ❌ Run-Length Encoding (RLE) for low-cardinality / constant-run columns
- ⚠️ Encoding selection heuristics at seal time (using per-chunk statistics
  to choose between raw, RLE, etc.)
  *Implemented for constant, FOR, dictionary, packed bool. RLE not yet
  implemented.*
- ❌ Benchmark-driven evaluation of further compression on representative
  datasets
- Note: delta (timestamps), dictionary (strings), packed bitfields (bools),
  and validity bitmaps are Phase 1 baseline encodings, not deferred here.

**Bonus (not in plan):**
- ✅ Frame of Reference (FOR) encoding for integer columns
- ✅ Constant encoding for any column type

### Phase 5: Migration and parity (engine-bridge-legacy) -- ❌ NOT STARTED

- Adapter from legacy PlotJuggler interfaces
- Side-by-side validation on representative data
- Remove legacy paths after parity criteria are met

---

## 14. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Overly complex first release | Defer advanced compression and non-essential features. Fixed row-count chunks. No partial updates. |
| Incremental DAG bugs | Always keep batch recompute as reference path and parity oracle. |
| Plugin breakage during migration | Strict interface boundaries. engine-bridge-legacy adapter layer. Side-by-side validation. |
| Column explosion from variable-length arrays | Configurable max expansion limit per field. |
| Memory estimates not realized in practice | Present estimates as hypotheses. Require benchmark gates before claiming ratios. |

---

## 15. Key Inspirations and References

| System | What we take from it |
|---|---|
| **Apache Arrow** | Columnar layout, nested types (Struct/List), validity bitmaps, Run-End Encoding, extension types for semantic metadata, dictionary type for strings |
| **Vortex** | Logical/physical separation, per-column encoding selection, cascading encodings concept, per-chunk statistics |
| **MCAP** | Schema -> Channel -> Message hierarchy with schema IDs, dual timestamps |
| **Rerun** | Multiple named timelines, chunk-based Arrow storage, partial/sparse updates, RangeQuery + LatestAtQuery modes |
| **Prometheus TSDB** | In-memory head block + sealed immutable chunks, time-based retention, chunk lifecycle |
| **TDengine** | Super-table (type template) + sub-tables (instances), tags vs columns |
| **InfluxDB/TimescaleDB** | Delta encoding for timestamps, Gorilla encoding reference, dictionary encoding |
| **Differential Dataflow** | Incremental recomputation concepts (simplified to chunk-aligned for our use case) |

### Reference Links

- [Arrow Columnar Format](https://arrow.apache.org/docs/format/Columnar.html) - Relevant for in-memory column layout, buffers, nullability, and nested type representation.
- [Apache Parquet Format](https://parquet.apache.org/docs/file-format/) - Relevant for persistence/export targets and columnar encoding design vocabulary.
- [Parquet Nested Encoding](https://parquet.apache.org/docs/file-format/nestedencoding/) - Relevant for struct/list modeling and nullability semantics in nested data.
- [MCAP Specification](https://mcap.dev/spec) - Relevant for Schema -> Channel -> Message hierarchy and schema-ID based organization.
- [Foxglove Data Platform](https://docs.foxglove.dev/docs) - Relevant as a practical robotics telemetry UX/data model reference point.
- [Prometheus TSDB Storage](https://prometheus.io/docs/prometheus/latest/storage/) - Relevant for head + sealed chunk lifecycle and time-window retention patterns.
- [Gorilla Compression Paper](https://www.vldb.org/pvldb/vol8/p1816-teller.pdf) - Relevant as a baseline reference for time-series compression tradeoffs.
- [Timescale Compression](https://docs.timescale.com/use-timescale/latest/compression/about-compression/) - Relevant for practical columnar/time-series compression strategies and tradeoffs.
- [Materialize Materialized Views](https://materialize.com/docs/sql/create-materialized-view/) - Relevant for incremental view maintenance patterns: continuously updating derived results as new data arrives.
- [Differential Dataflow Paper](https://www.microsoft.com/en-us/research/publication/differential-dataflow/) - Relevant for efficient incremental recomputation and dependency-aware update propagation in dataflow graphs.
- [Apache Flink Architecture](https://flink.apache.org/what-is-flink/flink-architecture/) - Relevant for stream-processing execution patterns (stateful operators, event-time/windowing concepts, scheduling).
- [TDengine Data Model](https://docs.tdengine.com/basic-features/data-model/) - Relevant for template/instance modeling ideas for typed telemetry families.
- [Rerun: Entities and Components](https://rerun.io/docs/concepts/logging-and-ingestion/entity-component) - Relevant for hierarchical entity/component organization and typed component thinking.
- [Rerun: Timelines](https://rerun.io/docs/concepts/logging-and-ingestion/timelines) - Relevant for timeline modeling and time-domain concepts.
- [Rerun: Chunks](https://rerun.io/docs/concepts/logging-and-ingestion/chunks) - Relevant for immutable chunk-centric ingestion/storage/query architecture.
- [Rerun: Query Semantics](https://rerun.io/docs/concepts/logging-and-ingestion/latest-at) - Relevant for `RangeQuery` and `LatestAt` style query API semantics.
- [Rerun: Static Data](https://rerun.io/docs/concepts/logging-and-ingestion/static) - Relevant for future static-data semantics considerations.
- [Rerun: Component Batches](https://rerun.io/docs/concepts/logging-and-ingestion/batches) - Relevant for batch ingestion ergonomics and throughput-oriented APIs.
- [Vortex (GitHub)](https://github.com/vortex-data/vortex) - Relevant for logical/physical separation and per-column/per-chunk encoding mindset.
- [Vortex: Compute Without Decompression](https://spiraldb.com/post/what-if-we-just-didnt-decompress-it) - Relevant for framing decode-cost vs compute-on-encoded-data tradeoffs.
- [Vortex: Towards 1.0](https://spiraldb.com/post/towards-vortex-10) - Relevant for practical evolution of a modern columnar engine architecture.

---

## 16. Post-v1 Candidates

Explicitly deferred items that should not block v1:

- **Persistence / serialization**: Arrow-based internals make export to Arrow
  IPC or Parquet straightforward.
- **Event/marker data**: discrete timestamped events as a first-class concept.
- **Multiple timeline columns per chunk**: Rerun-style multi-index.
- **Partial updates**: field-level carry-forward semantics.
- **Static data semantics**: data valid across all timelines (Rerun-style).
- **Compute push-down on encoded data**: Vortex-style evaluation directly on
  compressed columns.
- **Cascaded encodings**: delta -> zigzag -> bitpack pipelines.
- **Multi-threaded derived workers**: async transform execution.
- **ALP float compression**: evaluate after benchmarks show memory pressure.

---

## Appendix A: Implementation File Map

> Added post-implementation to track where plan concepts map to code.

### Libraries

| Library | Path | Dependencies |
|---|---|---|
| `plotjuggler_base` | `data/base/` | None (zero deps) |
| `plotjuggler_engine` | `data/engine/` | Abseil (absl::status, absl::flat_hash_map, ...), nanoarrow + nanoarrow_ipc (PRIVATE) |

### Headers

| File | Plan Concept |
|---|---|
| `base/include/pj/base/types.hpp` | Section 11: ID types, Timestamp, NumericType/NumericValue |
| `base/include/pj/base/type_tree.hpp` | Section 4.2: TypeTreeNode, PrimitiveType, EnumMapping |
| `base/include/pj/base/dataset.hpp` | Section 3/8: DatasetDescriptor, DatasetInfo, TimeDomain |
| `base/include/pj/base/assert.hpp` | PJ_ASSERT macro (exceptions or assert per PJ_ASSERT_THROWS) |
| `base/include/pj/base/span.hpp` | Minimal non-owning contiguous view (like std::span) |
| `base/include/pj/base/expected.hpp` | Value-or-error container (like Rust Result) |
| `engine/include/pj/engine/buffer.hpp` | Section 11: RawBuffer, validity bitmaps |
| `engine/include/pj/engine/column_buffer.hpp` | Section 11: TypedColumnBuffer (ColumnBuffer in plan) |
| `engine/include/pj/engine/encoding.hpp` | Section 6: Dictionary, PackedBools, Constant, FOR encodings |
| `engine/include/pj/engine/chunk.hpp` | Section 5.1/5.6: TopicChunk, TopicChunkBuilder, ChunkStats |
| `engine/include/pj/engine/topic_storage.hpp` | Section 5/11: TopicStorage, TopicDescriptor, TopicMetadata |
| `engine/include/pj/engine/type_registry.hpp` | Section 4.1/4.3: TypeRegistry |
| `engine/include/pj/engine/query.hpp` | Section 9.2: RangeCursor, LatestAtResult, QueryRange, QueryPoint |
| `engine/include/pj/engine/reader.hpp` | Section 9.2: DataReader (IDataReader in plan) |
| `engine/include/pj/engine/writer.hpp` | Section 9.2: DataWriter (IDataWriter in plan) |
| `engine/include/pj/engine/engine.hpp` | Section 2: DataEngine (coordinator) |
| `engine/include/pj/engine/arrow_import.hpp` | Section 17: nanoarrow IPC import adapter utilities |

### Tests

| File | Coverage |
|---|---|
| `tests/types_test.cpp` | NumericType sizes, variant indexing, conversion |
| `tests/type_tree_test.cpp` | Factory functions, flattening, semantic tags |
| `tests/span_test.cpp` | Span<T> construction, element access, subviews |
| `tests/expected_test.cpp` | Expected<T,E> value/error construction, accessors |
| `tests/buffer_test.cpp` | RawBuffer, validity bitmap operations |
| `tests/column_buffer_test.cpp` | All 7 storage types, nulls, read_as_double, bulk append |
| `tests/encoding_test.cpp` | Dictionary encoding, packed bools |
| `tests/chunk_test.cpp` | Builder API, sealing, stats, encoding selection, bulk append + deferred stats |
| `tests/topic_storage_test.cpp` | Chunk append, eviction, metadata |
| `tests/type_registry_test.cpp` | Register, lookup, duplicate detection, schema evolution |
| `tests/query_test.cpp` | Range queries, latest_at, multi-chunk, boundaries |
| `tests/engine_integration_test.cpp` | End-to-end: scalar + structured write/read, bulk ingest |
| `tests/arrow_import_test.cpp` | nanoarrow IPC stream import, schema parsing, type widening |

### Benchmarks

| File | What it measures |
|---|---|
| `benchmarks/read_benchmark.cpp` | Per-row and per-column read throughput across encodings |
| `benchmarks/ingest_benchmark.cpp` | Row-at-a-time vs bulk ingest throughput (builder + writer levels) |

### Examples

| File | Description |
|---|---|
| `examples/parquet_import.cpp` | Parquet file import via Arrow → bulk ingest API |

### Build Configuration

| File | Notes |
|---|---|
| `data/CMakeLists.txt` | C++20, two library targets, tests, benchmarks, examples |
| `data/conanfile.txt` | abseil/20240722.0, gtest/1.15.0, benchmark/1.8.3, arrow/18.1.0, nanoarrow/0.7.0 |

---

## 17. Bulk Columnar Ingest API ✅

> **Added post-implementation.** This section documents the bulk ingest path
> that supplements the row-at-a-time writer API.

### 17.1 Motivation

The row-at-a-time writer API (`begin_row`/`set_*`/`finish_row`) incurs
per-value overhead that becomes prohibitive for large batch imports.
For a Parquet file with 17K rows and 10K columns, the row-at-a-time
path takes ~10 seconds due to:
- 192M hash-map lookups (`builders_.find(topic_id)`) per cell
- 192M per-value stats computations (min/max/run_count/is_constant)
- Per-row state machine overhead

The bulk API eliminates this by accepting contiguous column arrays and
deferring stats to a single pass over the column buffer.

### 17.2 Architecture

```
Arrow IPC bytes (ABI-safe serialization boundary)
        |  deserialize via nanoarrow IPC
        v
nanoarrow ArrowArrayView (in adapter layer, depends on nanoarrow)
        |  extract raw pointers (arrow_import.hpp)
        v
DataWriter::append_columns(timestamps, columns)   <-- ENGINE API
        |  memcpy + deferred stats
        v
TopicChunkBuilder → seal() → commit_chunks()
```

The engine core uses nanoarrow (a lightweight, ABI-stable Arrow C
library) as a PRIVATE dependency for the import adapter. The bulk API
accepts raw pointers (`const float*`, `const int64_t*`, etc.), making
it usable from any columnar source without an Arrow dependency.

### 17.3 API Surface

**`ColumnData` struct** (`pj/engine/writer.hpp`): Describes one column's
data for bulk append. Supports all 7 storage kinds via typed factory
methods: `Float32()`, `Float64()`, `Int32()`, `Int64()`, `Uint64()`,
`Bool()`, `String()`. Each accepts optional validity bitmap.

**`DataWriter::append_columns()`**: Bulk columnar append with automatic
chunk boundary splitting:

```cpp
absl::Status append_columns(
    TopicId topic_id,
    absl::Span<const Timestamp> timestamps,
    absl::Span<const ColumnData> columns);
```

- Timestamps must be monotonically increasing and continuous with
  the topic's last committed timestamp.
- Columns are provided as `ColumnData` descriptors pointing to raw
  contiguous arrays (no copy for the caller).
- When a batch exceeds the current chunk's remaining capacity,
  `append_columns()` transparently slices the batch across chunk
  boundaries using pointer arithmetic (no extra copy).
- Null positions are tracked via Arrow-compatible validity bitmaps
  and correctly excluded from stats computation.

**`TopicChunkBuilder` bulk methods**: The builder exposes per-type bulk
append methods (`append_timestamps()`, `append_column_float32()`, etc.)
plus `append_column_validity()` and `finish_bulk_append()`. Stats are
computed in `finish_bulk_append()` after both data and validity are set.

**Arrow import adapter** (`pj/engine/arrow_import.hpp`): Higher-level
utilities for importing Arrow IPC data via nanoarrow:

```cpp
// Parse schema from Arrow IPC stream bytes → PJ type tree + column mappings
StatusOr<pair<shared_ptr<TypeTreeNode>, vector<ArrowColumnMapping>>>
    schema_from_ipc(Span<const uint8_t> ipc_stream);

// Import all record batches from Arrow IPC stream into a topic
Status import_ipc_stream(writer, topic_id, ipc_stream, mappings, ts_col);
```

Handles type widening (int8/int16 → int64, uint8-32 → uint64), bool
unpacking (Arrow packed bits → uint8_t array), and LargeString → String
offset narrowing (int64 → uint32).

### 17.4 Stats Computation

The bulk path defers stats computation to `finish_bulk_append()`, which
performs a single pass per column after both data and validity bitmaps
are in place. This is critical for correctness: null positions must be
excluded from min/max/is_constant/run_count stats.

For numeric columns, `compute_bulk_numeric_stats()` reads from the
`TypedColumnBuffer` using `col.has_nulls()` and `col.is_valid(row)` to
skip null positions. For string columns, `compute_bulk_string_stats()`
counts null entries and tracks uniqueness.

This produces identical stats to the row-at-a-time path, ensuring
encoding selection (constant, FOR, dictionary, etc.) and compression
ratios match.

### 17.5 Benchmark Results

Measured with `ingest_benchmark.cpp` (100,000 rows, Release build):

| Benchmark | Row-at-a-time | Bulk | Speedup |
|---|---|---|---|
| Builder Float32 (single column) | 1.40 ms | 0.13 ms | **10.7x** |
| Builder Int64 (single column) | 1.62 ms | 0.68 ms | **2.4x** |
| Builder 10x Float32 (multi-col) | 9.21 ms | 2.68 ms | **3.4x** |
| DataWriter Float32 (end-to-end) | 2.09 ms | 0.16 ms | **13.3x** |

The DataWriter bulk path achieves **638M items/sec** throughput.

### 17.6 Parquet Import Validation

Validated with a 19.8 MB Parquet file (17,832 rows × 10,758 columns):

| Metric | Row-at-a-time | Bulk API |
|---|---|---|
| Ingest time | ~10 s | **0.6 s** |
| Compression ratio | 0.02x | **0.02x** (identical) |
| Actual memory | 19.8 MB | 19.8 MB |
| Theoretical uncompressed | 843.7 MB | 843.7 MB |

The bulk API produces the same compressed output as the row-at-a-time
path. Most columns use Constant encoding (10,746 columns are constant),
strings use Dictionary encoding, and the shared timestamp column
eliminates per-field timestamp duplication.

### 17.7 Design Decisions

1. **Engine uses nanoarrow, not full Arrow C++**: nanoarrow (a
   lightweight, ABI-stable Arrow C library) is a PRIVATE dependency of
   `plotjuggler_engine` for the import adapter only. The bulk API
   accepts raw pointers (`const float*`, `const int64_t*`, etc.),
   making it usable from any columnar source. Full Arrow C++ is only
   required for the optional Parquet import example.

2. **Bools as uint8_t**: The bulk API accepts bools as one-byte-per-value
   `uint8_t` arrays (not packed bits). The Arrow adapter unpacks
   Arrow's packed bitmaps. This matches the internal `TypedColumnBuffer`
   format and simplifies the engine.

3. **String offsets are uint32_t**: Matching engine internals. Arrow
   LargeString (int64 offsets) is narrowed by the adapter.

4. **No separate ColumnBatchData struct**: The plan proposed a separate
   `ColumnBatchData` struct. The implementation uses `ColumnData` in
   `writer.hpp` which combines column index, storage kind, data
   pointer, count, string offsets, and validity bitmap in a single
   struct with typed factory methods. The `TopicChunkBuilder` bulk
   methods are called directly by `DataWriter::append_columns()`
   using `ColumnData` fields.

5. **Deferred stats, not skipped stats**: The plan proposed a
   `stats_deferred_` flag with stats computed in `seal()`. The
   implementation computes stats eagerly in `finish_bulk_append()`
   (called by `DataWriter` after each chunk-filling batch). This
   ensures stats are ready before seal time and matches the
   row-at-a-time pattern where stats are finalized before encoding.
