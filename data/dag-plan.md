# DerivedEngine: DAG Architecture and Implementation Plan

> **Status:** Design finalized. Supersedes the Phase 2 section of `data_implementation_plan.md`.
> See that document for the broader project context.


# Requirements and example

Typycal function that we want to implement:

**SISO (single input, single output)**:

1. Derivative: fiven a reference timeseries (time / value pairs), we create the derivative timeseries. First element skipped.
2. Time offset: example transform twhere the timestamp is not preseerved in the derived series

**MIMO (Multi input, multi output)**:

1. Quaternion -> RPY, has 4 input and 3 outputs
2. Copy value based on State variable: 2 inputs , one with value and the other a string or enum. 
   Value goes into the output only if the state is equal to a reference.

We need the concept of InputRow and OutputRow, being both potentially sparse.

Preserving the type might also be necessary.

For instance, consider using:

```cpp
using VarValue = std::variant<double, int64_t, std::string, std::nullopt_t>;
```

The multiple input can be etheregeous (numbers / strings), but share the same timestamp.
Some elements might be missing (optional values)


---

## 1. Overview

`DerivedEngine` adds computed (derived) series to `DataEngine` storage. It manages a
transform DAG: nodes consume source topics, apply a user-provided transform, and write
output topics back into the engine. Output topics are first-class committed series —
queryable, evictable, and chainable as inputs to further nodes.

**Scope of this document:**
- Full public API (SISO + MIMO), even though Phase 2 implements SISO only.
- Internal data model and algorithms.
- Phase breakdown and test plan.

---

## 2. Key Design Decisions

### 2.1  Sequential, point-at-a-time interface (not chunk-at-a-time)

Transforms receive one sample at a time via `calculate(time, input, out_time, out_value)`,
called in strictly ascending timestamp order. The engine owns chunk iteration, row
decoding, output batching, and flushing — the transform only sees individual (time, value)
pairs in order.

**Rationale:**
- Sequential ordering is the natural model for timeseries filters: derivative, moving
  average, integral, Kalman — all depend only on samples seen so far.
- State lives in transform member variables; no explicit `ChunkTail` needed in the
  public API. State persists across chunk boundaries automatically.
- `reset()` + full ordered replay is sufficient for batch recompute.
- Mirrors PlotJuggler's `calculateNextPoint(index)` — lower porting cost for existing
  filters (derivative, moving average, integral, etc.).

**Cost:** cannot do vectorized operations inside a transform. Acceptable for v1.

### 2.2  No ChunkTail in the public interface

The original `ITransformOp` design exposed `ChunkTail` to transform implementors.
With the point-at-a-time approach this is unnecessary:

- **Incremental path:** transform state is already "warm" from the previous call
  (member variables were not reset). The engine only delivers rows from chunks newer
  than `last_processed_chunk_id`.
- **Batch path:** engine calls `reset()`, then replays all rows from t=0.

`ChunkTail` is removed from the public interface entirely. The only internal tracking
needed per node is `last_processed_chunk_id`.

### 2.3  Data-eager scheduling (vs PlotJuggler's display-lazy SISO)

PlotJuggler SISO runs at render time inside `TransformedTimeseries::updateCache()`.
`DerivedEngine::schedule()` runs at commit time, writes to `TopicStorage`, and
commits via `DataEngine::commit_chunks()`. Derived topics are immediately queryable
and usable as inputs to downstream DAG nodes.

### 2.4  MIMO: primary-driven, secondaries sampled via `latest_at`

MIMO nodes have one **primary** input (drives timestamps) and N **secondary** inputs
(sampled at each primary timestamp via `latest_at` — most recent sample at or before
`t`, or NaN if none exists).

**Deliberate divergence from PlotJuggler:** `CustomFunction` uses `getIndexFromX`
(nearest-neighbor: lower_bounds on `t`, then picks the closer of `[index-1]` or
`[index]`). This is non-causal — it can return a sample slightly *after* `t`. For a
data engine where derived series must not depend on future inputs, causal `latest_at`
is the correct choice. Results will differ only when the nearest secondary sample
falls after the primary timestamp.

Phase 2 constraint: all inputs and outputs must be single-column (scalar float64)
topics. Multi-column primary/secondary is Phase 3.

### 2.5  Absl in `.cpp` only

Public headers use `std::vector`, `std::unordered_map`, `std::unordered_set`,
`pj::Expected`, `pj::Status`, `pj::Span`. Implementation files use
`absl::flat_hash_map`, `absl::flat_hash_set`, `absl::StrCat`, etc.

---

## 3. Public Interface

### 3.0  `VarValue` — the universal column value type

```cpp
// engine/include/pj/engine/derived_engine.hpp

/// Universal value type for transform I/O.
/// Engine storage kinds map as follows:
///   kFloat32, kFloat64        → double  (float32 widens losslessly)
///   kInt8 … kInt64, kBool     → int64_t (sign-extend; bool → 0/1)
///   kUint64                   → int64_t (cast; document caveat for values > INT64_MAX)
///   kString                   → std::string
using VarValue = std::variant<int64_t, double, std::string>;
```

### 3.1  `ISISOTransform`

SISO is strictly **single-column**: one `VarValue` in, one `VarValue` out.
`add_siso_transform` returns an error if the input topic has more than one column.

/// Single-input / single-output transform operating on a scalar timeseries.
///
/// SEQUENTIAL CONTRACT (fundamental):
///   The engine calls calculate() once per sample, strictly in ascending
///   timestamp order. Implementations may therefore accumulate state freely
///   in member variables between calls (e.g. previous value for derivative,
///   ring buffer for moving average, running sum for integral).
///   State persists across chunk boundaries — the engine never resets it
///   during incremental scheduling.
///   reset() is the only path that clears state; the engine calls it
///   exclusively before a full batch recompute.
class ISISOTransform {
 public:
  virtual ~ISISOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  /// After reset(), the next calculate() call must behave as if no data has
  /// been seen (same as a freshly constructed instance).
  virtual void reset() {}

  /// Declare the StorageKind of the output column. Called once at registration.
  /// Default: kFloat64 (suitable for most numeric filters).
  /// Override to preserve integer types or produce strings.
  virtual StorageKind output_kind(StorageKind input_kind) const {
    (void)input_kind;
    return StorageKind::kFloat64;
  }

  /// Process one sample. Called in strictly ascending timestamp order.
  ///   time:      sample timestamp (nanoseconds since epoch)
  ///   input:     sample value decoded as VarValue
  ///   out_time:  output timestamp (written by callee; read by engine only when true)
  ///   out_value: output value   (written by callee; read by engine only when true)
  ///
  /// Returns true to emit a row, false to suppress (e.g. first row of derivative).
  ///
  /// out_time MAY differ from `time` — time-offset transforms and interpolation
  /// may produce output on a different time grid than their input.
  /// When true is returned, out_time must be >= all previously returned out_times.
  virtual bool calculate(pj::Timestamp time, const VarValue& input,
                         pj::Timestamp& out_time, VarValue& out_value) = 0;
};
```

Output schema: one column, `StorageKind` as declared by `output_kind()`, column name
preserved from input. The engine creates the output topic at registration time using
this declared kind and writes via the matching `set_float64` / `set_int64` /
`set_string` call.

### 3.2  `IMIMOTransform`

MIMO operates on N input topics that **share the same timestamp**. The engine iterates
the first input topic and emits a row only when all other input topics have a sample at
exactly that timestamp (inner join). Rows where any input lacks an exact timestamp
match are silently skipped.

All input topics must be single-column (scalar). This constraint mirrors PlotJuggler's
model where each series is a flat `(time, value)` sequence.

/// Multi-input / multi-output transform operating on N co-timestamped timeseries.
///
/// SEQUENTIAL CONTRACT (fundamental, same as ISISOTransform):
///   The engine calls calculate() once per joined sample, strictly in ascending
///   timestamp order. State may be accumulated in member variables between calls.
///   reset() clears all state; called exclusively before batch recompute.
class IMIMOTransform {
 public:
  virtual ~IMIMOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  /// After reset(), the next calculate() call must behave as if no data has
  /// been seen (same as a freshly constructed instance).
  virtual void reset() {}

  /// Declare output StorageKind for each output topic.
  /// Called once at registration with the input kinds (one per input topic).
  /// Return one StorageKind per output topic name passed to add_mimo_transform.
  virtual std::vector<StorageKind> output_kinds(
      pj::Span<const StorageKind> input_kinds) const = 0;

  /// Process one joined sample. Called in strictly ascending timestamp order,
  /// only when ALL input topics have a sample at exactly `time`.
  ///   inputs[i]  = value from input topic i (in add_mimo_transform order).
  ///   out_time   = output timestamp (written by callee; read only when true).
  ///   output     = pre-allocated buffer (size == num output topics); fill in-place.
  ///                output[k] corresponds to output_topic_names[k] from add_mimo_transform.
  ///
  /// Returns true to emit a row; false to suppress.
  /// out_time MAY differ from `time`. When true is returned, out_time must be
  /// >= all previously returned out_times. All M output topics share this timestamp.
  virtual bool calculate(pj::Timestamp time, pj::Span<const VarValue> inputs,
                         pj::Timestamp& out_time, std::vector<VarValue>& output) = 0;
};
```

### 3.3  `DerivedEngine`

```cpp
class DerivedEngine {
 public:
  explicit DerivedEngine(DataEngine& engine);
  ~DerivedEngine();
  DerivedEngine(const DerivedEngine&) = delete;
  DerivedEngine& operator=(const DerivedEngine&) = delete;

  // ---- SISO ----
  // Creates one scalar output topic (StorageKind from op->output_kind()).
  // Returns error if:
  //   - input_topic_id does not exist
  //   - input topic has more than one column
  //   - output_topic_name already registered within output_dataset_id
  //     (DerivedEngine enforces uniqueness via Impl name index; DataEngine does not)
  [[nodiscard]] pj::Expected<pj::NodeId> add_siso_transform(
      pj::TopicId input_topic_id,
      std::string output_topic_name,
      pj::DatasetId output_dataset_id,
      std::unique_ptr<ISISOTransform> op);

  // ---- MIMO (Phase 3) ----
  // All input topics must be single-column (scalar).
  // A row is emitted only when ALL input topics share the exact same timestamp.
  // Creates output_topic_names.size() new topics (kinds from op->output_kinds()).
  // Returns error if any input topic has more than one column, or if any
  // output name is already registered within output_dataset_id.
  [[nodiscard]] pj::Expected<pj::NodeId> add_mimo_transform(
      std::vector<pj::TopicId> input_topic_ids,   // flat list; no primary/secondary
      std::vector<std::string> output_topic_names,
      pj::DatasetId output_dataset_id,
      std::unique_ptr<IMIMOTransform> op);

  // ---- DAG management ----
  pj::Status remove_node(pj::NodeId id);
  [[nodiscard]] bool has_node(pj::NodeId id) const noexcept;

  // Returns output topic IDs: 1 for SISO, M for MIMO.
  [[nodiscard]] std::vector<pj::TopicId> output_topics(pj::NodeId id) const;

  // Kahn's topological order (upstream → downstream).
  [[nodiscard]] std::vector<pj::NodeId> topological_order() const;

  // ---- Commit-cycle hook ----
  // Call after DataEngine::commit_chunks() with the set of changed topic IDs.
  // Marks directly dependent nodes dirty.
  void on_source_committed(pj::Span<const pj::TopicId> changed_topics);

  // ---- Scheduling ----
  // Process all dirty nodes in topological order (incremental path).
  // If active_nodes is non-empty, only nodes in that set (plus their transitive
  // dependencies) are considered.
  pj::Status schedule(const std::unordered_set<pj::NodeId>& active_nodes = {});

  // Full history recompute: clear output, reset transform, replay all input.
  // Use after parameter changes or to verify incremental correctness.
  pj::Status recompute_batch(pj::NodeId node_id);

 private:
  struct Impl;           // absl containers — defined in .cpp
  DataEngine& engine_;
  pj::NodeId next_node_id_ = 1;
  std::unique_ptr<Impl> impl_;
};
```

---

## 4. Internal Data Model

All types in this section live in `derived_engine.cpp` and are not exposed in headers.

```cpp
// In derived_engine.cpp

struct DerivedNode {
  pj::NodeId id = pj::kInvalidNodeId;
  bool is_mimo = false;

  // SISO fields
  pj::TopicId siso_input_topic_id = 0;
  StorageKind siso_input_kind = StorageKind::kFloat64;   // cached at registration
  std::unique_ptr<ISISOTransform> siso_op;

  // MIMO fields (flat list; no primary/secondary distinction)
  std::vector<pj::TopicId> mimo_input_topic_ids;
  std::vector<StorageKind> mimo_input_kinds;             // cached at registration
  std::unique_ptr<IMIMOTransform> mimo_op;

  // Common
  std::vector<pj::TopicId> output_topic_ids;   // 1 for SISO, M for MIMO
  bool dirty = true;
  pj::ChunkId last_processed_chunk_id = 0;     // 0 = never run

  // Reusable decode buffers (avoid per-row allocation)
  VarValue in_val_buf;                          // SISO input
  VarValue out_val_buf;                         // SISO output
  std::vector<VarValue> mimo_in_buf;            // MIMO inputs (one per input topic)
  std::vector<VarValue> mimo_out_buf;           // MIMO outputs (one per output topic)

  // Helper: returns all input topic IDs
  const std::vector<pj::TopicId>& all_input_topics() const;
};

struct DerivedEngine::Impl {
  absl::flat_hash_map<pj::NodeId, DerivedNode> nodes;

  // For topological sort and dirty propagation:
  //   downstream_of[N] = list of nodes whose inputs include an output of N
  absl::flat_hash_map<pj::NodeId, std::vector<pj::NodeId>> downstream_of;

  //   topic_to_nodes[T] = list of nodes that use T as an input
  absl::flat_hash_map<pj::TopicId, std::vector<pj::NodeId>> topic_to_nodes;

  //   output_topic_to_node[T] = node that produces T (for cycle detection)
  absl::flat_hash_map<pj::TopicId, pj::NodeId> output_topic_to_node;

  // Name uniqueness guard (DataEngine::create_topic has no name index).
  // Scope: within dataset. Key = (dataset_id, topic_name) → topic_id.
  // Populated on add_*_transform; checked before calling create_topic.
  absl::flat_hash_map<std::pair<pj::DatasetId, std::string>, pj::TopicId>
      registered_output_names;
};
```

---

## 5. Algorithms

### 5.1  Cycle Detection

Since each `add_*_transform` call creates **fresh output topics** (new names not
previously in the engine), a cycle is only possible if a node's output feeds back
into its own input chain via an existing derived topic used as an input.

**Detection (DFS from new node's inputs):**
```
given: new_node with input_topics = {I} and output_topics = {O}

for each topic I_i in {I}:
    queue = [I_i]
    while queue not empty:
        t = queue.pop()
        if t in {O}: ERROR — cycle detected
        if t is an output of some existing node M:
            for each input of M: queue.push(input)
```

Phase 2 (SISO only): since SISO creates exactly one new output topic and that topic
cannot exist yet, cycles are structurally impossible. The DFS check is still
implemented as a correctness guard for Phase 3 MIMO chaining.

### 5.2  Topological Sort (Kahn's Algorithm)

```
1. Compute in-degree[N] for every node N:
     = number of nodes M such that M has an output topic that N uses as input

2. Queue all nodes with in-degree = 0

3. While queue not empty:
     Pop N → append to order
     For each downstream node M in downstream_of[N]:
       in-degree[M] -= 1
       if in-degree[M] == 0: enqueue M

4. If order.size() != total nodes: cycle exists (should not happen post-registration)
```

`downstream_of` is maintained incrementally: when node N is registered, for each
input topic of N, if that topic is produced by node M, add N to `downstream_of[M]`.

### 5.3  Incremental Scheduling

`schedule()` algorithm:
```
1. order = topological_order()
2. if active_nodes non-empty: filter order to (active_nodes ∪ their transitive deps)
3. For each node N in order:
     if !N.dirty: continue
     run_node_incremental(N)
     N.dirty = false
     for each M in downstream_of[N]: M.dirty = true   // propagate
```

`run_node_incremental(node)`:
```
1. Get input TopicStorage from engine
2. new_chunks = sealed_chunks where chunk.id > node.last_processed_chunk_id
3. if new_chunks empty: return
4. writer = engine.create_writer()

5a. SISO path:
    pj::Timestamp out_ts; VarValue out_val;
    For each chunk C in new_chunks:
      For each row i in [0, C.stats.row_count):
        ts = C.timestamps[i]
        in_val = decode_as_varvalue(C, col=0, row=i, node.siso_input_kind)
        if siso_op->calculate(ts, in_val, out_ts, out_val):
          writer.begin_row(output_topic_ids[0], out_ts)
          write_varvalue(writer, output_topic_ids[0], col=0, out_val)
          writer.finish_row(output_topic_ids[0])

    // decode_as_varvalue: read_numeric_as_double → double, or read_string → string,
    //   or read int column → int64_t; determined by siso_input_kind cached at reg.
    // write_varvalue: visits variant, calls set_float64 / set_int64 / set_string.

5b. MIMO path (exact-timestamp inner join):
    mimo_in_buf.resize(num_inputs)
    // First input topic drives iteration
    For each chunk C of mimo_input_topic_ids[0] (in new_chunks):
      For each row i:
        ts = C.timestamps[i]
        mimo_in_buf[0] = decode_as_varvalue(C, col=0, row=i, mimo_input_kinds[0])
        // Check exact timestamp match for all other inputs
        all_matched = true
        For j in [1, num_inputs):
          sec_storage = engine.get_topic_storage(mimo_input_topic_ids[j])
          sample = latest_at(sec_storage->sealed_chunks(), ts)
          if sample is null OR sample.timestamp != ts:
            all_matched = false; break
          mimo_in_buf[j] = decode_as_varvalue(sample, col=0, mimo_input_kinds[j])
        if !all_matched: continue   // skip row — inputs not co-sampled at ts
        pj::Timestamp out_ts;
        if mimo_op->calculate(ts, mimo_in_buf, out_ts, mimo_out_buf):
          For k in [0, num_outputs):
            writer.begin_row(output_topic_ids[k], out_ts)
            write_varvalue(writer, output_topic_ids[k], col=0, mimo_out_buf[k])
            writer.finish_row(output_topic_ids[k])

6. auto chunks = writer.flush_all()
   engine.commit_chunks(std::move(chunks))

7. node.last_processed_chunk_id = max(chunk.id for chunk in new_chunks)
```

Downstream dirty propagation is handled exclusively by `schedule()`'s outer loop
(`for each M in downstream_of[N]: M.dirty = true` in step 3). `run_node_incremental`
does not call `on_source_committed` — doing so would re-enter dirty marking from
within the loop and is redundant.

### 5.4  Batch Recompute

```
recompute_batch(node_id):
1. node = impl_->nodes.at(node_id)
2. Clear each output topic:
     engine.get_topic_storage(out_id)->evict_before(
         std::numeric_limits<pj::Timestamp>::max())
     — evicts all chunks with t_max < INT64_MAX (i.e. all of them)
3. if SISO: node.siso_op->reset()
   if MIMO: node.mimo_op->reset()
4. node.last_processed_chunk_id = 0   // pretend never run
5. Run run_node_incremental(node)     // will process ALL chunks (id > 0)
6. node.dirty = false
```

---

## 6. Built-in Transforms

### 6.1  `DerivativeTransform` (Phase 2)

Numerical derivative: `d(value)/d(t)` in units/second. Works on all columns.
Skips the first row (no previous sample). Required for correctness parity testing.

```cpp
class DerivativeTransform : public ISISOTransform {
  pj::Timestamp prev_time_ = 0;
  double prev_value_ = 0.0;
  bool has_prev_ = false;

 public:
  void reset() override { has_prev_ = false; }

  // output_kind() returns kFloat64 (default) — derivative is always float.

  bool calculate(pj::Timestamp time, const VarValue& input,
                 pj::Timestamp& out_time, VarValue& out_value) override {
    double v = std::get<double>(input);  // engine widens to double per output_kind()
    if (!has_prev_) {
      prev_time_ = time;
      prev_value_ = v;
      has_prev_ = true;
      return false;  // skip first row — no previous sample
    }
    double dt = static_cast<double>(time - prev_time_) * 1e-9;
    out_time = time;
    out_value = (v - prev_value_) / dt;
    prev_time_ = time;
    prev_value_ = v;
    return true;
  }
};
```

### 6.2  `MovingAverageTransform` (Phase 3)

Sliding window average over the last N rows per column. Circular buffer.

### 6.3  `QuaternionToRPYTransform` (Phase 3, MIMO)

4 scalar inputs (qw, qx, qy, qz) → 3 scalar outputs (roll, pitch, yaw).
Implements angle-wrapping state across rows (like PlotJuggler's quaternion toolbox).

### 6.4  `LuaTransform` (Phase 3, MIMO)

Wraps `IMIMOTransform` with a user-supplied Lua script. Lua signature mirrors
PlotJuggler's `CustomFunction`:

```lua
-- Single output (SISO-style Lua):
function calc(time, value, v1, v2, ...)
  return result
end

-- Multiple outputs (MIMO):
function calc(time, value, v1, v2, ...)
  return r1, r2, r3   -- Lua multiple returns → out_values[0..2]
end
```

`time` is in seconds (float). `value` is `primary[0]`. `v1, v2, ...` are
`secondary[0], secondary[1], ...`.

**Differences from PlotJuggler `LuaCustomFunction`:**
- Hard cap of 8 additional sources is removed.
- Table-of-`{x,y}`-pairs return form is NOT supported (was a single-output density
  mechanism; not applicable here).
- Multi-return → multi-output is supported natively.
- Runs incrementally (per new rows), not on every UI tick like `ReactiveLuaFunction`.

**Dependencies:** sol2 + Lua 5.4 — add to `data/conanfile.txt` in Phase 3.

---

## 7. File Structure

### New files

| File | Contents |
|---|---|
| `engine/include/pj/engine/derived_engine.hpp` | `ISISOTransform`, `IMIMOTransform`, `MIMORow`, `DerivedEngine` |
| `engine/src/derived_engine.cpp` | `DerivedEngine` implementation (`Impl` with absl) |
| `engine/include/pj/engine/builtin_transforms.hpp` | `DerivativeTransform`, `MovingAverageTransform` |
| `engine/src/builtin_transforms.cpp` | Implementations |
| `tests/derived_engine_test.cpp` | Full test suite |

### Modified files

| File | Change |
|---|---|
| `base/include/pj/base/types.hpp` | Add `NodeId = uint32_t`, `kInvalidNodeId = 0` |
| `CMakeLists.txt` | Add `derived_engine.cpp`, `builtin_transforms.cpp` to `plotjuggler_engine`; add `derived_engine_test` target |

### NOT modified
`engine.hpp`, `writer.hpp`, `reader.hpp`, `query.hpp`, `chunk.hpp`,
`topic_storage.hpp`, and all existing tests. `DerivedEngine` is a pure consumer of
the existing API.

---

## 8. Phase Breakdown

### Phase 2 (next)

| Item | Notes |
|---|---|
| `NodeId` type in `types.hpp` | Trivial — add alias + sentinel |
| `ISISOTransform` interface | Point-at-a-time, reset(), calculate() |
| `DerivedEngine::add_siso_transform` | Registers fresh output topic, registers node |
| Cycle detection (guard) | DFS; trivially safe for SISO but implement correctly |
| `topological_order()` | Kahn's algorithm |
| `on_source_committed()` | Mark dependent nodes dirty |
| `schedule()` incremental | Chunk-loop → row-loop → calculate() → writer |
| `recompute_batch()` | evict_before + reset + full replay |
| `DerivativeTransform` | Reference SISO implementation |
| Tests: correctness | Derivative of linear = constant; within 1e-9 |
| Tests: parity | `schedule()` output == `recompute_batch()` output for 1, 2, 3 chunks |
| Tests: chaining | A → B: topological order, both nodes run correctly |
| Tests: lazy | `active_nodes` filter skips non-requested nodes |

### Phase 3 (future)

| Item | Notes |
|---|---|
| `IMIMOTransform` interface | MIMORow, secondary sampling via latest_at |
| `DerivedEngine::add_mimo_transform` | N inputs → M output topics |
| `QuaternionToRPYTransform` | Built-in MIMO, validates interface |
| `MovingAverageTransform` | SISO, circular buffer |
| `LuaTransform` | sol2 + Lua 5.4, mirrors PlotJuggler CustomFunction |
| Multi-column secondary inputs | Expand `secondary` to `Span<Span<const double>>` |
| `remove_node` propagation | Cascade removal of orphaned downstream nodes |

---

## 9. Test Plan (Phase 2)

```cpp
// ---- NodeId ----
TEST(TypesTest, NodeId_IsUint32)

// ---- Header compiles ----
TEST(DerivedEngineTest, HeaderCompiles)

// ---- DerivativeTransform unit ----
TEST(DerivativeTransformTest, SkipsFirstRow)
TEST(DerivativeTransformTest, CorrectDerivative_ConstantRate)   // y=slope*t → dy/dt = slope/dt_sec
TEST(DerivativeTransformTest, Reset_ClearsState)

// ---- add_siso_transform ----
TEST(DerivedEngineTest, AddTransform_CreatesOutputTopic)
TEST(DerivedEngineTest, AddTransform_DuplicateOutputName_Fails)
TEST(DerivedEngineTest, AddTransform_UnknownInputTopic_Fails)

// ---- topological_order ----
TEST(DerivedEngineTest, TopologicalOrder_SingleNode)
TEST(DerivedEngineTest, TopologicalOrder_Chain_ABOrder)    // A→B, order is [A, B]
TEST(DerivedEngineTest, TopologicalOrder_Fork)             // A→B, A→C, order has A first

// ---- on_source_committed ----
TEST(DerivedEngineTest, DirtyPropagation_SourceChanged)
TEST(DerivedEngineTest, DirtyPropagation_Chain)            // A dirty → schedule → B dirty

// ---- schedule (incremental) ----
TEST(DerivedEngineTest, Schedule_ProducesOutput)
TEST(DerivedEngineTest, Schedule_SecondCallNoNewChunks_NoOp)
TEST(DerivedEngineTest, Schedule_Lazy_SkipsInactiveNode)
TEST(DerivedEngineTest, Schedule_Chain_BothNodesRun)

// ---- recompute_batch ----
TEST(DerivedEngineTest, RecomputeBatch_ClearsAndRegenerates)
TEST(DerivedEngineTest, RecomputeBatch_SameCountAsIncremental)

// ---- parity: incremental == batch ----
TEST(DerivedEngineTest, Parity_SingleChunk)
TEST(DerivedEngineTest, Parity_TwoChunks_CrossBoundary)
TEST(DerivedEngineTest, Parity_ThreeChunks)
```

**Parity tolerance:** `EXPECT_NEAR(incremental_val, batch_val, 1e-9)` for all rows.

---

## 10. Testing Patterns

This section explains **how** to test transforms and the engine, with reusable code
patterns. All tests live in `tests/derived_engine_test.cpp`.

### 10.1  Testing a transform in isolation (no engine)

The sequential contract means transform correctness can be verified without any engine
machinery — just call `calculate()` in a loop with hand-crafted samples.

```cpp
// DerivativeTransform: y = 2*t (slope=2, dt=1s)
DerivativeTransform op;
pj::Timestamp out_time;
VarValue out_val;

// First sample: suppressed (no previous point yet)
EXPECT_FALSE(op.calculate(0, VarValue{0.0}, out_time, out_val));

// Second sample: dy/dt = (2.0 - 0.0) / 1.0 = 2.0
EXPECT_TRUE(op.calculate(1'000'000'000LL, VarValue{2.0}, out_time, out_val));
EXPECT_EQ(out_time, 1'000'000'000LL);
EXPECT_NEAR(std::get<double>(out_val), 2.0, 1e-9);

// Third sample: same slope, same result
EXPECT_TRUE(op.calculate(2'000'000'000LL, VarValue{4.0}, out_time, out_val));
EXPECT_NEAR(std::get<double>(out_val), 2.0, 1e-9);
```

This is the preferred first test for any new transform. It requires no fixtures and
runs instantly. Write one test per meaningful behaviour:
- correct output value at each step
- first-row suppression (if applicable)
- edge cases (zero dt, negative values, type promotion)

### 10.2  Testing `reset()`

`reset()` must restore the transform to exactly its initial state. Test by running a
sequence, resetting, then running the same sequence again and comparing outputs.

```cpp
DerivativeTransform op;
pj::Timestamp t; VarValue v;

// First pass
op.calculate(0,              VarValue{0.0}, t, v);
op.calculate(1'000'000'000LL, VarValue{2.0}, t, v);  // v = 2.0

// Reset and repeat identical sequence
op.reset();
EXPECT_FALSE(op.calculate(0, VarValue{0.0}, t, v));  // suppressed again

bool ok = op.calculate(1'000'000'000LL, VarValue{2.0}, t, v);
EXPECT_TRUE(ok);
EXPECT_NEAR(std::get<double>(v), 2.0, 1e-9);  // identical to first pass
```

### 10.3  Test helpers

Define these in `derived_engine_test.cpp` at file scope to avoid boilerplate in each test.

```cpp
// Build a single-column float64 topic and write `n` rows with value = slope * t_seconds.
// Commits the chunk. Returns the topic_id.
static pj::TopicId make_linear_topic(DataEngine& engine, double slope, int n,
                                     pj::Timestamp step_ns = 100'000'000LL /*10Hz*/) {
  // 1. register schema: one float64 column "value"
  // 2. create dataset + topic
  // 3. writer.begin_row / set_float64 / finish_row for each row
  // 4. engine.commit_chunks(writer.flush_all())
  // returns topic_id
}

// Collect all float64 values from a topic's output into a flat vector.
static std::vector<double> collect_values(const DataEngine& engine, pj::TopicId topic_id) {
  auto reader = engine.create_reader();
  auto cursor = *reader.range_query({topic_id, 0, std::numeric_limits<pj::Timestamp>::max()});
  std::vector<double> out;
  cursor.for_each([&](const SampleRow& row) {
    out.push_back(row.chunk->read_numeric_as_double(0, row.row_index));
  });
  return out;
}

// Collect (timestamp, value) pairs.
static std::vector<std::pair<pj::Timestamp, double>>
collect_rows(const DataEngine& engine, pj::TopicId topic_id) { ... }
```

### 10.4  Engine integration test pattern

```cpp
TEST(DerivedEngineTest, Schedule_ProducesCorrectDerivative) {
  DataEngine engine;
  DerivedEngine derived(engine);

  // slope=2.0, 10Hz, 11 rows → 10 derivative rows (first suppressed)
  pj::TopicId src = make_linear_topic(engine, /*slope=*/2.0, /*n=*/11);

  auto node = *derived.add_siso_transform(
      src, "d_linear", dataset_id,
      std::make_unique<DerivativeTransform>());

  derived.on_source_committed({src});
  ASSERT_TRUE(derived.schedule().has_value());

  // derivative of 2*t at 10Hz = 2.0 * 10.0 = 20.0 (units/second)
  auto vals = collect_values(engine, derived.output_topics(node)[0]);
  ASSERT_EQ(vals.size(), 10u);
  for (double v : vals) EXPECT_NEAR(v, 20.0, 1e-6);
}
```

### 10.5  Parity test pattern (incremental == batch, correctness oracle)

This is the most important test for the engine itself. For any transform, the
output of incremental scheduling (chunk by chunk) must exactly match the output
of `recompute_batch()`.

```cpp
// Helper: run incremental chunk-by-chunk, return collected output.
static std::vector<double> run_incremental(DataEngine& engine, DerivedEngine& derived,
                                            pj::TopicId src, pj::NodeId node,
                                            /* source chunks written in caller */) {
  derived.on_source_committed({src});
  EXPECT_TRUE(derived.schedule().has_value());
  return collect_values(engine, derived.output_topics(node)[0]);
}

TEST(DerivedEngineTest, Parity_TwoChunks_CrossBoundary) {
  DataEngine engine;
  DerivedEngine derived(engine);

  // Write chunk 1 (rows 0..19), commit, run incremental
  pj::TopicId src = make_linear_topic(engine, 3.0, 20);  // rows 0-19
  pj::NodeId node = *derived.add_siso_transform(src, "deriv", ds,
                        std::make_unique<DerivativeTransform>());
  derived.on_source_committed({src});
  ASSERT_TRUE(derived.schedule().has_value());

  // Write chunk 2 (rows 20..39), commit, run incremental again
  append_linear_topic(engine, src, 3.0, 20, /*start_idx=*/20);
  derived.on_source_committed({src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto incremental = collect_values(engine, derived.output_topics(node)[0]);

  // Now batch recompute and compare
  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_EQ(incremental.size(), batch.size());
  for (size_t i = 0; i < batch.size(); ++i)
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
}
```

The cross-chunk boundary row is the one most likely to differ: in the incremental
path the transform's internal state carries over naturally (no reset), while in the
batch path it replays from row 0. Both must produce the same result for a correct
implementation.

### 10.6  Usability: writing a new SISO transform

A minimal transform needs only `calculate()`. More complex ones also override
`reset()` and `output_kind()`.

```cpp
// Clamps values to [lo, hi]. Stateless — no reset() needed.
class ClampTransform : public ISISOTransform {
  double lo_, hi_;
 public:
  ClampTransform(double lo, double hi) : lo_(lo), hi_(hi) {}

  bool calculate(pj::Timestamp time, const VarValue& input,
                 pj::Timestamp& out_time, VarValue& out_value) override {
    double v = std::clamp(std::get<double>(input), lo_, hi_);
    out_time = time;
    out_value = v;
    return true;  // always emit
  }
};

// Exponential moving average. Stateful — must override reset().
class EMATransform : public ISISOTransform {
  double alpha_;
  double ema_ = 0.0;
  bool has_prev_ = false;
 public:
  explicit EMATransform(double alpha) : alpha_(alpha) {}

  void reset() override { has_prev_ = false; }

  bool calculate(pj::Timestamp time, const VarValue& input,
                 pj::Timestamp& out_time, VarValue& out_value) override {
    double v = std::get<double>(input);
    ema_ = has_prev_ ? alpha_ * v + (1.0 - alpha_) * ema_ : v;
    has_prev_ = true;
    out_time = time;
    out_value = ema_;
    return true;
  }
};
```

The isolation test for `EMATransform`:

```cpp
TEST(EMATransformTest, ConvergesOnConstantInput) {
  EMATransform op(0.1);
  pj::Timestamp t; VarValue v;
  // Feed constant value 10.0; EMA must converge toward 10.0
  double prev = 0.0;
  for (int i = 0; i < 100; ++i) {
    bool ok = op.calculate(static_cast<pj::Timestamp>(i) * 1'000'000LL,
                           VarValue{10.0}, t, v);
    ASSERT_TRUE(ok);
    double cur = std::get<double>(v);
    EXPECT_GT(cur, prev);   // monotonically increasing
    EXPECT_LE(cur, 10.0);   // never overshoots
    prev = cur;
  }
  EXPECT_NEAR(std::get<double>(v), 10.0, 0.01);
}
```

---

## 11. Open Questions (Resolved)

| Question | Decision |
|---|---|
| Point-at-a-time vs chunk-at-a-time? | **Point-at-a-time.** Engine handles chunking. |
| ChunkTail in public API? | **No.** State lives in member variables. |
| Transform I/O value type | **`VarValue = variant<int64_t, double, std::string>`.** Covers all engine storage kinds without the full 10-way NumericValue. |
| SISO: single-column or multi-column? | **Single-column only.** Multi-column → use MIMO. Error on registration if input has >1 column. |
| SISO output column type | **Declared by `output_kind(input_kind)`.** Default kFloat64; overridable to preserve integer types or produce strings. |
| SISO timestamp in output | **May differ from input timestamp.** Contract: monotonically increasing. Enables time-offset and interpolation transforms. |
| MIMO join strategy | **Exact timestamp inner join.** Row emitted only when all input topics have data at exactly the same timestamp. No primary/secondary; first topic drives iteration. |
| MIMO vs PlotJuggler nearest-neighbor | **Exact match is stricter.** For co-published signals (same message) this is equivalent. Deliberately avoids non-causal sampling. |
| MIMO input column constraint | **Single-column (scalar) per input topic.** |
| Cycle detection in Phase 2? | **Implemented** as DFS guard even though SISO cannot cycle. |
| Output topic name uniqueness | **Within-dataset.** `DerivedEngine::Impl` maintains `(dataset_id, name) → topic_id` index; `DataEngine::create_topic` has no such check. |
| Lua in Phase 2? | **No.** Phase 3. `IMIMOTransform` interface is already designed for it. |
| `active_nodes` in `schedule()` | **Supported.** Empty set = run all dirty nodes. |
| `calculate()` return type | **`bool` + out-params.** Returns `false` to suppress row; `true` to emit. `out_time` and `out_value`/`output` are out-params pre-allocated by the engine. No `std::optional` wrapper, no `std::vector` return — zero per-call allocation. |
