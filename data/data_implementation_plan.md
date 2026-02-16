# PlotJuggler Data Engine Redesign -- Detailed Plan

This document defines the plan for a new in-memory Data Engine for PlotJuggler.
It incorporates the original PLAN.md goals, reference architecture research,
design decisions confirmed with the author, and review feedback from both
codex_plan.md and claude_review.md.

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

Five concrete modules, each independently testable:

1. **engine-core**: Owns datasets, topics, chunk metadata. Exposes
   `IDataWriter` and `IDataReader`. No UI dependencies.
2. **engine-storage**: Owns typed column buffers and chunk lifecycle.
   Arrow-compatible memory layout utilities.
3. **engine-derived**: Owns transform DAG, scheduler, dirty propagation.
   Depends on engine-core interfaces only.
4. **engine-time**: Owns time-domain registry and mapping functions for
   display.
5. **engine-bridge-legacy**: Adapter from old PlotJuggler data interfaces
   to new APIs. Enables incremental migration without breaking existing
   plugins.

---

## 3. Data Hierarchy

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

---

## 4. Type Tree and Schema Management

### 4.1 Central Registry with Late Discovery

A hybrid approach:

- **Central type registry**: schemas are registered with unique IDs. Multiple
  topics sharing the same type reference the same schema ID.
- **Late discovery**: for schema-less formats (JSON, MessagePack, CBOR, XML),
  the schema is inferred from the first message and registered automatically.
  Subsequent messages may add new fields (see Schema Evolution below).
- **Schema-based formats** (Protobuf, ROS, FlatBuffers, DDS): the schema is
  provided upfront by the data source plugin and is fixed.

### 4.2 Type Tree Representation

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

The type tree enables:
- MIMO transforms that operate on semantically meaningful groups (e.g.,
  quaternion-to-RPY knows it needs 4 floats that form a Quaternion)
- GUI display that reflects the original structure (drag "robot_pose" and
  see all 8 fields grouped by their parent types)
- Automatic encoding selection (enums get RLE, strings get dictionary, etc.)

### 4.3 Schema Evolution (Additive)

For schema-less formats, new fields may appear mid-stream:

- New columns are added to the topic's table with nulls for all past data.
- Old chunks simply lack the new column; queries return null for those ranges.
- The registry tracks the "current" schema version per topic.
- Fields **cannot** change type or be removed mid-stream.

---

## 5. Storage Layer

### 5.1 Chunk-Based Columnar Storage

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

### 5.2 Chunk Lifecycle and Threading Model

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

- **Building**: the mutable chunk being filled by the writer. Owned
  exclusively by the plugin thread. Never accessed by the main thread.
- **Staged**: sealed and enqueued for transfer. Immutable. Waiting to be
  drained by the main thread.
- **Committed**: appended to `TopicStorage.sealed_chunks` on the main thread.
  Immutable. Readable by viewers and transforms.
- **Evicted**: dropped when its time range falls outside the retention window.

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

### 5.3 Time-Based Eviction

Each chunk tracks its time range [t_min, t_max]. Eviction is relative to
the **newest ingested sample time** (not wall-clock time), which ensures
correct behavior for both live streaming and offline playback:

```
t_keep_min = newest_timestamp_in_topic - retention_window
```

All chunks with `t_max < t_keep_min` are evicted. Eviction is O(1) per
chunk (pop front of the chunk deque). Eviction never removes from the middle
-- retained data is always a contiguous time range.

### 5.4 Variable-Length Arrays

For types like `vector<Pose> poses`, the primary access pattern is
"plot `poses[3].position.x` over time" (fixed index over time).

Strategy: expand array elements into indexed columns at ingest time:
- `poses[0].position.x`, `poses[0].position.y`, ...
- `poses[1].position.x`, `poses[1].position.y`, ...
- etc.

When a longer array is first seen, new columns are added dynamically.
Validity bitmaps track which indices exist at each timestamp (sparse table).
This reuses the additive schema evolution mechanism.

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

### 5.5 ScatterXY

Two modes:
1. **Reference mode** (common): a view pairing two existing time-indexed
   series for XY plotting. No separate storage. Purely a viewer-level concept.
2. **Value mode** (rare): actual stored XY pairs, generated by a data source.
   Stored as a simple two-column table without monotonic-time constraint.

### 5.6 Per-Chunk Statistics

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

---

## 6. Encoding Layer

### 6.1 Strategy: Moderate Compression

The goal is significant memory savings with negligible decode cost. Every
read ultimately produces raw values for rendering, so per-value decode cost
matters.

### 6.2 Encoding Selection (per-column, at seal time)

When a chunk is sealed, per-column statistics guide encoding selection:

| Data type | Encoding | Condition |
|---|---|---|
| **Timestamps** | Delta encoding | Always (monotonic, near-zero decode cost). |
| **float32 / float64** | Raw typed storage | Always. Preserve original width. |
| **int8/16/32/64, uint*** | Raw typed storage | Always. Preserve original width. |
| **bool** | Packed bitfield | Always. 1 bit per value. |
| **Any column** | Run-Length Encoding | When `run_count` is low relative to `row_count`. |
| **Strings** | Dictionary encoding | Always (default). See note below. |
| **Sparse columns** | Validity bitmap | When `null_count > 0`. |

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

## 7. Derived Series and Transforms

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

## 8. Time Domains

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

### 8.2 Cross-Dataset Comparison

When two Datasets are loaded with different time domains, their data is
displayed on a shared axis with offsets applied. This enables side-by-side
comparison of experiments recorded at different absolute times.

### 8.3 Future Extensions (out of scope for v1)

- Time scaling (compare experiments at different speeds)
- Named alignment points (align by event rather than start time)
- Multiple timeline columns per chunk (Rerun-style)

---

## 9. Plugin API

### 9.1 Design Principle: High-Level Typed API

Plugins never see raw columnar storage, encodings, or chunks. They interact
through a typed API that hides all internal details. Arrow-compatible
internal buffers are used, but Arrow objects are not exposed as public
plugin API types.

### 9.2 Core Interfaces

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

Write-path ergonomics are intentionally split:
- **Simple path**: `append()` / `append_batch()` with field-name based
  `MessageView` construction. Best for prototypes and low-rate inputs.
- **Fast path**: resolve/bind once (`bind_topic_writer`, `FieldId`) and use
  `append_fast()` / `append_batch_fast()`. No per-message field-name string
  comparisons in the hot loop.
- **Scalar path**: `register_scalar_series()` + `append_scalar()` for the
  current PlotJuggler-style time/value append workflow.

Row construction may be performed field-by-field by the writer, but v1
commit semantics are still full-row: at row finalization, every field must
have a defined state for that row (explicit value, explicit null, or writer
error per policy). Missing fields never imply carry-forward from prior rows.

Two query modes (inspired by Rerun):
- **RangeQuery**: returns a cursor/iterator over all samples in [t_min,
  t_max]. Binary search on chunk time bounds, scan only intersecting chunks.
  Primary path for time-series plotting.
- **LatestAtQuery**: returns the most recent value at or before time t.
  Binary search on chunk max times + per-chunk binary search. Useful for
  dashboard views and MIMO transforms needing aligned inputs.

Renderers should consume iterators/cursors, not materialize full vectors.

### 9.3 ABI Considerations

C++ virtual interfaces are sufficient for v1. All plugins are compiled
against the same PlotJuggler version. If binary compatibility across
compiler versions becomes necessary in the future, a C ABI wrapper can
be added.

---

## 10. Performance and Correctness Targets

Define measurable acceptance criteria **before** implementation lock:

### Performance targets

| Metric | Target (to be baselined) |
|---|---|
| bytes/sample by data type and topic shape | Benchmark vs. current engine |
| Ingest throughput (rows/sec) in streaming | At least on par with current |
| Max append latency per batch | Within frame budget |
| Derived incremental update latency | Within frame budget |
| Range query latency for plotting | At least on par with current |

### Correctness criteria

- Type tree fidelity: round-trip schema registration and retrieval.
- Derived DAG correctness: incremental output matches batch output within
  numeric tolerance.
- Retention correctness: no data outside retention window, no gaps in
  retained data.
- Schema evolution: additive field changes handled without data corruption.

---

## 11. Concrete Data Structures (v1)

```cpp
using DatasetId    = uint32_t;
using TopicId      = uint32_t;
using FieldId      = uint32_t;   // Scoped per-topic (unique within a topic, not globally)
using ChunkId      = uint64_t;
using TimeDomainId = uint32_t;
using SchemaId     = uint32_t;
using PluginId     = uint32_t;

// All timestamps are stored as int64 nanoseconds. This is a locked v1
// decision. int64_t nanoseconds covers ~292 years of range with nanosecond
// precision, sufficient for all robotics and data logging use cases.
using Timestamp = int64_t;  // nanoseconds since epoch

struct ChunkStats {
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
struct EnumMapping {
    std::unordered_map<int64_t, std::string> value_to_name;
    std::unordered_map<std::string, int64_t> name_to_value;
};

// Tracks schema evolution per topic. Each entry represents a version of
// the schema, enabling correct interpretation of old chunks that may
// lack columns added in later versions.
struct SchemaVersionEntry {
    SchemaId schema_id;
    uint32_t version;                       // Monotonically increasing
    std::shared_ptr<TypeTreeNode> type_tree; // Type tree snapshot at this version
    std::vector<FieldId> field_ids;          // Ordered list of fields in this version
};

struct SchemaVersionHistory {
    TopicId topic_id;
    std::vector<SchemaVersionEntry> versions;  // Ordered by version number
    // current() returns versions.back()
};

// Tail context provided to incremental transforms that need cross-chunk
// history (e.g., derivative needs the last sample, moving average needs
// the last N samples).
struct ChunkTail {
    uint32_t requested_rows;    // How many trailing rows the transform declared it needs
    // Read-only view into the tail of the previous output chunk(s).
    // Managed by the derived engine: stored alongside each derived node,
    // updated after each incremental computation.
    // The engine retains this data even if the source chunk has been evicted
    // by retention -- ChunkTail is a copy, not a reference.
    span<const Timestamp> timestamps;
    std::vector<span<const uint8_t>> column_values;  // One per output field
};

struct ColumnBuffer {
    FieldId field_id;
    ArrowType logical_type;     // Logical type from type tree
    EncodingType encoding;      // Physical encoding (raw, delta, RLE, dict)
    Buffer values;              // Arrow-style value buffer
    Buffer validity;            // Validity bitmap (if null_count > 0)
    Buffer offsets;             // For variable-length types
};

struct TopicChunk {
    ChunkId id;
    TopicId topic_id;
    SchemaId schema_version;    // For additive schema evolution
    ChunkStats stats;
    Buffer timestamp_values;    // Shared timestamp column (delta-encoded)
    std::vector<ColumnBuffer> columns;
};

struct TopicStorage {
    TopicId topic_id;
    SchemaId current_schema;
    TimeDomainId time_domain_id;
    int64_t retention_window_ns;
    std::deque<TopicChunk> sealed_chunks;   // Immutable, committed
    // Note: there is no active_chunk here. The mutable building chunk
    // is owned by the plugin thread via PluginStagingContext (below).
};

// Owned by the plugin thread. One per plugin. Not accessed by the main thread
// except to drain sealed_queue.
struct PluginStagingContext {
    PluginId plugin_id;
    TopicChunk building_chunk;              // Mutable, exclusively owned by plugin thread
    // Sealed chunks waiting for main-thread commit. SPSC queue (plugin pushes,
    // main thread drains). This is the only synchronization point between
    // plugin and main threads.
    SPSCQueue<TopicChunk> sealed_queue;
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

1. **Arrow integration**: Arrow-compatible internal buffers. Do not expose
   Arrow objects directly as public plugin API types.
2. **Chunk sizing**: Fixed row-count in v1. Byte-based adaptive chunking
   deferred.
3. **Derived scheduling**: Batched lazy on main thread (once per frame) as
   default. Batch recompute available on demand as correctness oracle.
4. **Variable-length arrays**: Expand to indexed columns at ingest, with
   configurable max expansion limit. Viewer may additionally filter.
5. **Update semantics**: Column-by-column write APIs are allowed, but row
   commit semantics are full-row in v1. No implicit field-level carry-forward
   from prior rows.
6. **Timeline model**: One timestamp index per topic chunk in v1. Additional
   timeline/index columns deferred.
7. **Threading**: Plugin threads build chunks independently. Main thread
   drains staging queues in deterministic order, runs derived scheduler,
   enforces retention, and renders. Single-threaded commit path.
8. **Schema evolution**: Additive only. New fields allowed. Type changes
   and field removal prohibited.
9. **String encoding**: Dictionary encoding is the default for all string
   columns.
10. **Retention**: Time-window based, relative to newest ingested sample
    time (not wall-clock time).
11. **Timestamp representation**: `int64_t` nanoseconds since epoch. This
    replaces the current `double` representation. Covers ~292 years of range
    with nanosecond precision.
12. **Writer API tiers**: v1 exposes both a simple string-based append API
    and a pre-bound handle fast path (`FieldId`-based) to avoid per-message
    string comparisons in high-rate streams.
13. **Scalar convenience API**: v1 includes a first-class scalar
    `append_scalar(time, value)` path for single-value time-series ingestion.
14. **Semantic tags**: Type tree nodes carry an optional set of string tags
    for semantic role discovery by transforms (e.g., `"quaternion"`), distinct
    from the struct name.

---

## 13. Implementation Phases

### Phase 1: Core model + typed chunk store (engine-core, engine-storage)

- Dataset / topic / field metadata
- Type tree registry with hybrid discovery
- Typed append / read paths
- Shared timestamp column
- Chunk lifecycle (build, seal, commit, evict)
- Per-chunk statistics
- Range and latest-at queries
- **Baseline encodings** (locked v1 decisions, not deferred):
  - Delta encoding for timestamp columns
  - Dictionary encoding for string columns
  - Packed bitfields for bool columns
  - Validity bitmaps for nullable/sparse columns
  - Raw typed storage for all numeric types (preserving original width)

### Phase 2: Derived DAG engine (engine-derived)

- Node / edge model with topological sort
- Cycle detection on registration
- Incremental chunk-aligned scheduling
- Batch recompute path
- Correctness parity tests (incremental vs. batch)

### Phase 3: Time domains + UI integration (engine-time)

- Domain IDs and offset mapping
- Visual alignment controls
- Per-dataset automatic t0 alignment

### Phase 4: Advanced memory optimizations

- Run-Length Encoding (RLE) for low-cardinality / constant-run columns
- Encoding selection heuristics at seal time (using per-chunk statistics
  to choose between raw, RLE, etc.)
- Benchmark-driven evaluation of further compression on representative
  datasets
- Note: delta (timestamps), dictionary (strings), packed bitfields (bools),
  and validity bitmaps are Phase 1 baseline encodings, not deferred here.

### Phase 5: Migration and parity (engine-bridge-legacy)

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
