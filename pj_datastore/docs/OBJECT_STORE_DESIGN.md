# ObjectStore Design

## 1. Purpose

A message-oriented storage engine that sits alongside the existing columnar
`DataEngine`. While `DataEngine` stores scalar time-series in typed columns
with adaptive encoding, `ObjectStore` stores timestamped opaque payloads
(images, point clouds, scene primitives, transforms, etc.) with lazy
on-demand resolution.

The two stores share the same `DatasetId` identity from `pj_base` but use
separate handle types (`TopicId` vs `ObjectTopicId`). They are independent
peer classes — the application owns both.

## 2. Motivation

PlotJuggler needs to handle data types that don't fit the columnar model:

- **Large blobs** (images, point clouds, grids) — variable-length,
  megabyte-sized payloads that would destroy columnar encoding assumptions.
- **Structured messages** (scene primitives, transforms, annotations) —
  small but semantically atomic; the message is the unit of meaning, not
  individual scalar fields.
- **Lazy decode** — for file-backed sources, storing a seek callback instead
  of copying raw bytes keeps memory flat regardless of dataset size.
- **Streaming capture** — for live sources, raw compressed bytes must be
  buffered in memory since the source data is ephemeral.

Some fields from these messages (e.g., a transform's translation.x) may
also be duplicated into the columnar `DataEngine` for plotting. This
duplication is managed by the DataSource plugin — the ObjectStore does not
coordinate with the DataEngine.

## 3. Design Decisions

### 3.1 Separate from DataEngine (Option B)

The ObjectStore is a standalone class in `pj_datastore`, not embedded inside
`DataEngine`. Rationale:

- Single responsibility — ObjectStore has no encoding, chunking, column
  buffers, or derived transforms.
- Independent development and testing.
- Separate handle types (`ObjectTopicId` vs `TopicId`) prevent misuse at
  compile time.
- Zero risk of regressions on the columnar path.

### 3.2 Storage Model: raw bytes, not decoded objects

Each entry stores raw compressed/serialized bytes — not decoded results.
A JPEG image stays as JPEG bytes (~100-200KB), not decoded RGB pixels (~6MB).
Decoding is always deferred to the consumer (viewer or transformer plugin).

For file-backed sources, the entry stores a fetch callback instead of owned
bytes. The callback captures a shared_ptr to the file reader and seeks back
to the message on demand. For streaming sources, the entry stores owned
copies of the raw bytes.

The ObjectStore exposes a **uniform interface** — consumers always receive
raw bytes via a resolve call. Whether the bytes are owned or fetched from a
callback is an internal storage detail, invisible to consumers.

### 3.3 Homogeneous series

Each object series (topic) carries one message type, declared at
registration time. There is no per-entry type discrimination. The series
metadata tells consumers what the bytes represent and which parser to use.

### 3.4 Threading model

Concurrent readers, single writer. Each `ObjectSeries` holds a
`std::shared_mutex`: read methods (`latestAt`, `at`, `indexAt`,
`entryTimestamps`, `timeRange`, `entryCount`) acquire a shared lock
internally; write methods (`pushOwned`, `pushLazy`, `evictBefore`,
`removeTopic`, `clear`) acquire an exclusive lock. Multiple decoder
worker threads (potentially from different widgets) may read the same
topic concurrently without external synchronization. Writes are
serialized.

Streaming sources still queue incoming messages internally on their own
thread and push during `poll()` on a single application thread — the
"single writer" is typically that thread draining the source's input
queue.

Returned `ResolvedObjectEntry` values are **owning**
(`shared_ptr`-based, see §4), so a reader can release its shared lock
the moment `latestAt` or `at` returns and continue decoding on its own
thread without holding any store lock. This is what makes worker-side
decoding safe while the application keeps pushing.

### 3.5 Retention and eviction

Retention is enforced automatically during push, driven by a per-topic
budget configured by the application. The budget has two orthogonal
axes:

- **Time window** (`time_window_ns`): drop entries whose timestamp is
  older than `newest_push_ts - time_window_ns`.
- **Memory cap** (`max_memory_bytes`): drop oldest entries until the
  topic's total owned-payload memory falls at or below the cap.

Either axis may be zero to disable it; both zero disables automatic
eviction for the topic entirely (useful for file-backed datasets that
should retain everything).

Automatic eviction runs **inside** `pushOwned` / `pushLazy`, on the
same thread that called push. This is what ties retention to the
live⊥scrub invariant from `pj_media/docs/REQUIREMENTS.md §4.3`:
eviction only happens while the application is actively pushing new
entries, which by the product-level requirement is exactly "live
mode". In scrub mode the application has stopped pushing, so no
eviction runs — the buffer is frozen for as long as the user wants
to explore it. Decoders and viewers can rely on this: during scrub,
any entry or handle they observe will not disappear beneath them.

Memory accounting tracks only **owned** entries (those inserted via
`pushOwned`). Lazy entries (inserted via `pushLazy`) contribute no
payload bytes to the topic's memory usage, because the store does
not own their bytes — each read re-invokes the fetch callback. For
file-backed datasets the memory cap is therefore effectively
unbounded unless the caller also sets a time window. This is
intentional: file-backed sources already have their bytes on disk,
and the store only holds lightweight closures.

Explicit eviction (`evictBefore`, `evictAllBefore`) remains
available for callers that want to trim without pushing new data —
for example, an application-driven "clear history before time T"
action. Entries are stored in a deque; all eviction paths remove
from the front.

### 3.6 Stateless entries

Every entry in the ObjectStore is independently resolvable. No entry depends
on prior entries for decoding. Stateful decoding (e.g., H.264 P-frames
requiring a preceding keyframe) is a viewer/consumer concern — the
ObjectStore stores raw NAL units; the video decoder widget maintains codec
state internally.

## 4. Data Model

### ObjectTopicId

Separate handle type from `TopicId`. Prevents accidental cross-use with
columnar APIs at compile time.

```cpp
struct ObjectTopicId {
  uint32_t id;
};
```

### Series metadata

Set once at registration time. Stored per series.

```cpp
struct ObjectTopicDescriptor {
  DatasetId dataset_id;
  std::string topic_name;
  std::string metadata_json;  // e.g. {"media_class":"image","encoding":"cdr",
                               //       "schema":"sensor_msgs/CompressedImage"}
};
```

The `metadata_json` is opaque to the ObjectStore — it stores and returns it
without interpretation. Consumers and the GUI use it to select viewers and
parsers.

### Entry

```cpp
struct ObjectEntry {
  int64_t timestamp;
  std::variant<
    std::shared_ptr<const std::vector<uint8_t>>, // owned raw bytes (streaming)
    std::function<std::vector<uint8_t>()>        // lazy fetch (file-backed)
  > payload;
};
```

The owned-bytes arm is a `shared_ptr<const vector<uint8_t>>`. On
`pushOwned`, the store wraps the caller-supplied vector in a
`shared_ptr` once and stores it; on read, multiple readers share the
same `shared_ptr` — no copies, zero-overhead sharing. Consumers never
see this variant directly; the public API resolves it internally.

### Resolved entry (returned to consumers)

```cpp
struct ResolvedObjectEntry {
  int64_t timestamp;
  std::shared_ptr<const std::vector<uint8_t>> data;  // owning
};
```

The `data` handle is **owning**. Its lifetime is independent of the
store's internal state — eviction, concurrent writes, even topic
removal cannot invalidate a handle the caller already holds. The
caller releases the handle by dropping the `shared_ptr`. Decoder
worker threads rely on this property: they may hold a handle while
the main thread pushes new entries or evicts old ones.

### Entry timestamps view

```cpp
/// RAII view over a topic's entry timestamps. Holds a shared lock on
/// the owning series for the view's lifetime. Blocks writers (pushes
/// and evictions) while any view is alive, so the contents are stable
/// during iteration. Dropped like any other RAII type — the shared
/// lock is released in the destructor.
class EntryTimestampsView {
 public:
  bool empty() const;
  size_t size() const;
  int64_t operator[](size_t i) const;
  const int64_t* begin() const;
  const int64_t* end() const;

 private:
  std::shared_lock<std::shared_mutex> lock_;
  const std::vector<int64_t>* timestamps_;  // nullable; null == empty view
  // (constructor is private; only ObjectStore can produce a view)
};
```

The view exposes a small const-access surface and owns a
`shared_lock` internally. Typical usage is a tight loop in a
seek-planning routine; the view is dropped immediately after the loop.
Holding a view across long operations would block pushes — callers
should copy into their own `std::vector<int64_t>` via the iterators
if they need to retain the data past the view's scope.

### Retention budget

```cpp
struct RetentionBudget {
  /// Drop entries older than `newest_push_ts - time_window_ns`.
  /// Zero disables the time axis.
  int64_t time_window_ns = 0;

  /// Drop oldest entries until the topic's owned-payload memory
  /// is at or below this cap. Zero disables the memory axis.
  /// Lazy entries do not count against this cap (they hold no
  /// owned bytes inside the store).
  size_t max_memory_bytes = 0;
};
```

Both fields default to zero, which means "no automatic eviction".
The application sets a budget per topic via
`ObjectStore::setRetentionBudget(id, budget)`. Budgets take effect
on the next `pushOwned` / `pushLazy` call; changing the budget does
not trigger retroactive eviction.

## 5. Public API

```cpp
namespace PJ {

class ObjectStore {
 public:
  // --- Registration ---

  /// Register a new object topic. Returns a unique handle.
  Expected<ObjectTopicId> registerTopic(const ObjectTopicDescriptor& descriptor);

  /// Get metadata for a registered topic.
  const ObjectTopicDescriptor& descriptor(ObjectTopicId id) const;

  /// List all registered topics, optionally filtered by dataset.
  std::vector<ObjectTopicId> listTopics() const;
  std::vector<ObjectTopicId> listTopics(DatasetId dataset_id) const;

  // --- Write ---

  /// Append an entry with owned raw bytes (streaming sources).
  Status pushOwned(ObjectTopicId id, int64_t timestamp,
                   std::vector<uint8_t> payload);

  /// Append an entry with a lazy fetch callback (file-backed sources).
  Status pushLazy(ObjectTopicId id, int64_t timestamp,
                  std::function<std::vector<uint8_t>()> fetch);

  // --- Read ---
  //
  // All read methods are thread-safe and may be called concurrently
  // from multiple threads (see §3.4). Returned ResolvedObjectEntry
  // handles are owning — readers may drop the store lock and continue
  // decoding on their own thread without risk of invalidation.

  /// Find the entry at or before the given timestamp.
  /// Returns nullopt if no entry exists at or before the timestamp.
  ///
  /// Semantics: "at or before" only — never returns a future entry
  /// even if one is closer in time. This guarantees causality for
  /// viewers: a render at time t only sees bytes produced at or
  /// before t.
  std::optional<ResolvedObjectEntry> latestAt(ObjectTopicId id,
                                               int64_t timestamp) const;

  /// Get the entry at a specific index.
  /// Combined with indexAt() below, supports the seek-then-decode-
  /// forward pattern used by video decoders.
  std::optional<ResolvedObjectEntry> at(ObjectTopicId id, size_t index) const;

  /// Find the index of the entry at or before the given timestamp, or
  /// nullopt if no such entry exists. Matches latestAt() semantics but
  /// returns only the index, not the resolved payload — useful when
  /// the caller will immediately iterate forward via at(i), at(i+1),
  /// ... and does not want to materialize the seek target's payload
  /// twice.
  std::optional<size_t> indexAt(ObjectTopicId id, int64_t timestamp) const;

  /// Number of entries in a series.
  size_t entryCount(ObjectTopicId id) const;

  /// Timestamp range of a series: {t_min, t_max}.
  /// For live topics with rolling eviction, this is the currently
  /// retained window, not the full history seen so far.
  std::pair<int64_t, int64_t> timeRange(ObjectTopicId id) const;

  /// RAII view over the topic's entry timestamps in index order. The
  /// view holds a shared lock on the series for its lifetime, so
  /// writers are blocked while any view is alive. Used by streaming
  /// keyframe indexers and seek planners that need batch timestamp
  /// access without resolving payloads. See §4 "Entry timestamps
  /// view" for details.
  EntryTimestampsView entryTimestamps(ObjectTopicId id) const;

  // --- Retention ---

  /// Set or update the retention budget for a topic. Takes effect on
  /// the next push; does not trigger eviction retroactively. Pass a
  /// default-constructed budget (both fields zero) to disable
  /// automatic eviction for this topic.
  void setRetentionBudget(ObjectTopicId id, RetentionBudget budget);

  /// Get the current retention budget for a topic.
  RetentionBudget retentionBudget(ObjectTopicId id) const;

  /// Current owned-payload memory use for a topic, in bytes. Lazy
  /// entries contribute zero. Useful for diagnostics and for
  /// application-level budgets that span multiple topics.
  size_t memoryUsage(ObjectTopicId id) const;

  // --- Explicit eviction (escape hatches) ---
  //
  // Normal eviction is automatic during push (see §3.5). These
  // methods are for callers that want to trim without pushing new
  // data — for example, an explicit "clear history before time T"
  // user action.

  /// Remove all entries with timestamp < threshold for a specific topic.
  void evictBefore(ObjectTopicId id, int64_t threshold);

  /// Remove all entries for all topics with timestamp < threshold.
  void evictAllBefore(int64_t threshold);

  // --- Lifecycle ---

  /// Remove a topic and all its entries.
  void removeTopic(ObjectTopicId id);

  /// Clear all topics and entries.
  void clear();
};

}  // namespace PJ
```

## 6. C ABI Surface (Plugin Access)

Plugins access the ObjectStore through two distinct host vtables,
mirroring the existing scalar plugin contract:

- **`PJ_object_write_host_vtable_t`** — exposed to **DataSource**
  plugins alongside the scalar `PJ_parser_write_host_vtable_t`. Write
  path only. See §6.1.
- **Toolbox / transformer extensions to `PJ_toolbox_host_vtable_t`** —
  exposed to **Toolbox** plugins and future transformer plugins. Read
  + write. See §6.2.

All the types referenced below (`PJ_object_topic_handle_t`,
`PJ_bytes_view_t`, etc.) live in `pj_base/include/pj_base/plugin_data_api.h`
alongside the existing scalar types. Both vtables follow the project's
ABI discipline: `version` + `size` header fields, append-only slot
ordering, `size` checked by the host at load time for forward
compatibility.

### 6.1 DataSource write host — `PJ_object_write_host_vtable_t`

Added to the set of host bindings offered to DataSource plugins. A
plugin that produces media topics uses this vtable in the same way a
parser plugin uses the scalar write host today: register topics,
push entries, optionally configure retention. Parser plugins
receiving a topic-scoped `PJ_object_write_host_t` during delegated
ingest see the same surface.

```c
typedef uint32_t PJ_object_topic_handle_t;   // opaque; 0 == invalid

/// Fetch function for lazy entries, invoked by the store when the
/// entry is resolved on the reader thread. On success, allocates a
/// buffer via `malloc()` and returns it via *out_data / *out_size.
/// The store takes ownership of the buffer, copies the bytes into an
/// internal shared_ptr, and frees the buffer with `free()` after the
/// copy. Returns false on failure (out params ignored).
typedef bool (*PJ_lazy_fetch_fn_t)(void* fetch_ctx,
                                   uint8_t** out_data,
                                   size_t* out_size);

typedef struct PJ_object_write_host_vtable_s {
  uint32_t version;  // starts at 1; bump on incompatible slot changes
  uint32_t size;     // sizeof(PJ_object_write_host_vtable_t); host checks

  // --- Topic registration ---

  /// Register a new object topic. Returns a handle, or 0 on failure.
  /// `metadata_json` is opaque to the store and copied internally.
  PJ_object_topic_handle_t (*register_topic)(
      void* ctx,
      const char* topic_name,
      const char* metadata_json);

  /// Get the metadata JSON a topic was registered with. Returned
  /// pointer is owned by the host and valid until the topic is
  /// removed.
  const char* (*topic_metadata)(
      void* ctx,
      PJ_object_topic_handle_t topic);

  // --- Push (streaming / owned bytes) ---

  /// Append an entry with owned raw bytes. The store copies the
  /// bytes into an internal shared_ptr on the hot path, so `data`
  /// does not need to outlive this call.
  bool (*push_owned)(
      void* ctx,
      PJ_object_topic_handle_t topic,
      int64_t timestamp_ns,
      const uint8_t* data,
      size_t size);

  // --- Push (file-backed / lazy fetch) ---

  /// Append an entry with a lazy fetch callback (file-backed
  /// sources). `fetch_ctx` is opaque plugin state passed to
  /// `fetch_fn`. `fetch_ctx_destroy` is called when the entry is
  /// evicted or the topic is removed; pass NULL if no cleanup is
  /// needed.
  bool (*push_lazy)(
      void* ctx,
      PJ_object_topic_handle_t topic,
      int64_t timestamp_ns,
      PJ_lazy_fetch_fn_t fetch_fn,
      void* fetch_ctx,
      void (*fetch_ctx_destroy)(void*));

  // --- Retention ---

  /// Configure per-topic retention. Either axis may be zero to
  /// disable it. Takes effect on the next push call.
  void (*set_retention_budget)(
      void* ctx,
      PJ_object_topic_handle_t topic,
      int64_t time_window_ns,
      size_t max_memory_bytes);

  // --- Keyframe index (video topics only) ---

  /// Publish a pre-computed keyframe timestamp list for a video topic.
  /// `timestamps` is a sorted array of `count` nanosecond timestamps.
  /// The host copies the array internally and forwards it to
  /// pj_media's `MediaIndexRegistry`. Non-video topics ignore this.
  /// DataSource plugins call this at file-open time after scanning
  /// the container (NAL start codes, MP4 stss atom, etc.). Streaming
  /// sources skip this — pj_media's VideoDecoder builds the index
  /// incrementally. See `pj_media/docs/ARCHITECTURE.md §6`.
  void (*publish_keyframe_index)(
      void* ctx,
      PJ_object_topic_handle_t topic,
      const int64_t* timestamps,
      size_t count);

} PJ_object_write_host_vtable_t;
```

`PJ_lazy_fetch_fn_t` is a plain `extern "C"` function pointer with C
linkage and no capture — the plugin must manually pass context via
`fetch_ctx`. This mirrors how scalar write hosts accept plain C
callbacks today.

**Lifetime note on `fetch_ctx`**: the store may retain the context
for an unbounded time (as long as the lazy entry remains in the
deque). When the entry is evicted (automatically by retention, or
explicitly via `evictBefore` / `removeTopic` / `clear`), the store
calls `fetch_ctx_destroy(fetch_ctx)` to release plugin-owned
resources. A typical DataSource plugin packs a shared handle to the
file reader into the context and uses `fetch_ctx_destroy` to drop
the shared reference.

### 6.2 Toolbox / transformer read host

Toolbox plugins (and future transformer plugins) need read access in
addition to the write surface. The read path uses **opaque owning
handles** rather than raw pointers, matching the C++
`shared_ptr<const vector<uint8_t>>` model from §4:

```c
typedef struct PJ_object_bytes_handle_s* PJ_object_bytes_handle_t;

// --- Read ---

/// Find the entry at or before the given timestamp on a topic. On
/// success, writes a new owning handle to *out_handle and the
/// entry's timestamp to *out_timestamp. The handle must be released
/// by the caller via `release_object_bytes`. Returns false (without
/// modifying the out params) if no entry exists at or before the
/// timestamp.
///
/// The handle keeps the bytes alive even if the underlying entry is
/// later evicted — the store holds an internal reference until the
/// last handle is released.
bool (*read_object_latest_at)(
    void* ctx,
    PJ_object_topic_handle_t topic,
    int64_t timestamp_ns,
    PJ_object_bytes_handle_t* out_handle,
    int64_t* out_timestamp);

/// Retrieve the raw bytes referenced by a handle. The pointer is
/// valid until the handle is released.
void (*get_object_bytes)(
    PJ_object_bytes_handle_t handle,
    const uint8_t** out_data,
    size_t* out_size);

/// Release a handle returned by any read_object_* function. Once
/// released, the handle must not be used again.
void (*release_object_bytes)(
    PJ_object_bytes_handle_t handle);

/// Look up a registered object topic by name. Returns the topic
/// handle, or 0 if no topic with that name exists. This is how a
/// toolbox or transformer plugin resolves a name from
/// list_object_topics into a handle usable with read_object_*.
PJ_object_topic_handle_t (*lookup_object_topic)(
    void* ctx,
    const char* topic_name);

/// List all registered object topics in this store.
/// Returns a JSON array: [{"topic":"...","metadata":{...}}, ...]
/// The returned string is owned by the host and valid until the
/// next call to this function on the same context.
const char* (*list_object_topics)(void* ctx);
```

These slots are appended to `PJ_toolbox_host_vtable_t` under the
existing `size`-guarded append-only rule; old toolbox plugins that
only use the scalar API keep working unchanged.

### 6.3 ABI discipline

Both vtables follow the same rules:

- **Append-only** — new slots go at the end; existing slots never
  reorder, change signature, or repurpose. A caller compiled against
  vtable size `N1` cannot call slots beyond offset `N1` even if the
  runtime host provides a larger vtable.
- **Version bump on incompatible change** — if an existing slot must
  change semantically, the `version` field increments and the host
  loads only plugins compiled against the new version (or emits a
  clear error).
- **Size check at load time** — the host validates that the plugin's
  declared vtable `size` is at least as large as the smallest
  version the host accepts. This lets newer plugins run on older
  hosts (the host ignores the trailing slots) and older plugins run
  on newer hosts (the plugin ignores the trailing slots).

The scalar `PJ_parser_write_host_vtable_t` and
`PJ_toolbox_host_vtable_t` already follow these rules; the new
object vtables match.

## 7. Integration Points

### DataSource plugins (write path)

A DataSource plugin writes media entries into the ObjectStore in one of
two modes (see `pj_media/docs/REQUIREMENTS.md §4.4` for the full
contract):

- **Direct ingest**: the plugin calls `registerTopic()` and then
  `pushOwned()` (streaming sources) or `pushLazy()` (file-backed
  sources, with a fetch callback capturing the file reader).
  Appropriate when the format is tight enough that a dedicated parser
  adds no value.

- **Delegated ingest**: the plugin registers the topic, binds a
  parser for the topic's wire encoding, and calls `pushRawMessage()`
  per frame. The host routes raw bytes to the bound
  `MessageParser::parse()`, which receives **both** a scalar write
  host and an object write host and writes the scalar portions
  (e.g., `header.seq`, `frame_id`) to the scalar host and the media
  payload to the object host — from a single parse call. Parsers are
  strictly codec-agnostic (envelope peelers): they never touch
  codec-level metadata like keyframe flags, which live entirely in
  pj_media's `VideoDecoder`.

A DataSource may also duplicate selected scalar fields into the
DataEngine via the existing scalar write host — for example, exposing
an image message's `header.seq` as a plottable scalar while the JPEG
bytes go to the ObjectStore.

### GUI / application (read path)

The application holds both `DataEngine` and `ObjectStore`. The catalog
merges topics from both. When the user opens a viewer:

- The GUI reads `metadata_json` to determine the viewer type (image
  viewer, video viewer, scene viewer, etc.) and the built-in decoder
  needed for rendering. Parsers are NOT involved at read time — they
  are ingest-time envelope peelers only. Decoding is performed by
  pj_media's built-in decoder classes (`VideoDecoder`, `ImageDecoder`,
  `SceneDecoder`; see `pj_media/docs/REQUIREMENTS.md §4.6`).
- On each timestamp update: `objectStore.latestAt(topic, timestamp)`
  returns an owning handle to the raw bytes; the viewer's decoder
  produces pixels / typed results from them; the viewer displays the
  result.

### Transformer plugins (future)

A transformer plugin reads object entries from one topic (e.g., image),
processes them (e.g., object detection), and writes object entries to
another topic (e.g., annotation). Both reads and writes go through the
C ABI host vtable.

## 8. Internal Storage

```cpp
struct ObjectSeries {
  ObjectTopicDescriptor descriptor;

  // The entry deque: back-appendable, front-evictable. Binary search
  // for timestamp lookup uses the parallel vector below, not this
  // deque directly (deque is not contiguous).
  std::deque<ObjectEntry> entries;

  // Parallel vector of timestamps in index order. Kept strictly in
  // sync with `entries`: push appends to both; evict_front pops from
  // both. Contiguous storage is load-bearing here so that:
  //   (a) indexAt() can binary-search a cache-friendly layout, and
  //   (b) entryTimestamps() can return a view into contiguous memory
  //       via its RAII wrapper.
  std::vector<int64_t> entry_timestamps;

  // Retention policy (see §3.5). Updated via setRetentionBudget();
  // consulted by push paths to decide whether to run automatic
  // eviction after the new entry is appended.
  RetentionBudget budget;

  // Running total of owned payload sizes. Incremented on pushOwned
  // by the payload's size(); decremented on front-eviction by the
  // evicted entry's size(). Lazy entries contribute zero. Used by
  // the memory-cap eviction path and exposed via memoryUsage().
  size_t memory_bytes = 0;

  // Concurrent-readers / single-writer lock (see §3.4). Readers
  // acquire std::shared_lock; writers acquire std::unique_lock.
  mutable std::shared_mutex mutex;
};
```

**Push path** (holds a unique lock on `mutex`):
1. Append the new `ObjectEntry` to `entries` and the timestamp to
   `entry_timestamps`.
2. For `pushOwned`: add the payload size to `memory_bytes`.
3. Run automatic eviction (see below) if `budget` is non-trivial.

**Automatic eviction** (inside push, before the lock is released):
1. If `budget.time_window_ns > 0`: compute
   `threshold = newest_push_ts - budget.time_window_ns` and
   `evict_front_while(entry.timestamp < threshold)`.
2. If `budget.max_memory_bytes > 0`:
   `evict_front_while(memory_bytes > budget.max_memory_bytes)`.
3. Each `evict_front` step pops both `entries[0]` and
   `entry_timestamps[0]` and subtracts the evicted payload's size
   from `memory_bytes`.

Eviction runs under the writer lock, so readers never observe a
partially-evicted state; they either see the pre-push snapshot or
the post-push-and-eviction snapshot.

Both `latestAt()` and `indexAt()` binary-search `entry_timestamps`
(timestamps are monotonically non-decreasing by the ingest invariant).
`latestAt()` resolves the owned-bytes or lazy-callback variant inside
the reader lock and returns an owning `shared_ptr` handle;
`indexAt()` returns only the index. `entryTimestamps()` constructs
an `EntryTimestampsView` that takes a `std::shared_lock` on the
series — writers block until the view is dropped.

The old single-entry resolve cache (`last_resolved_ts_`,
`last_resolved_bytes_`) is **removed**. Under the owning-handle model,
callers that repeatedly poll the same timestamp (for example a paused
GUI) already get cheap sharing: owned entries hold a
`shared_ptr<const vector<uint8_t>>` internally, so every `latestAt`
call returns a `shared_ptr` that shares the same underlying buffer.
Lazy entries still re-invoke their fetch callback on each read —
caching lazy results is a viewer-side concern (pj_media), not the
store's responsibility.

Topic lookup uses `tsl::robin_map<ObjectTopicId, ObjectSeries>`.

## 9. Relationship to datatypes_2D.md

The ObjectStore stores raw serialized messages. It does not define or
enforce any specific message schema. The types defined in `datatypes_2D.md`
(Image, PointCloud, ScenePrimitive, FrameTransform, etc.) are the domain
types that viewer-side decoders produce and viewers consume. The
ObjectStore sits between ingest and display:

```
Write path (ingest):
  DataSource → [Parser, if delegated] → ObjectStore (raw bytes)

Read path (display):
  ObjectStore (raw bytes) → Viewer decoder (pj_media_core) → Typed result → Viewer
```

The ObjectStore is agnostic to the domain type. It stores whatever bytes
the DataSource provides, tagged with `metadata_json` that tells
downstream consumers which decoder to apply. Parsers (MessageParser
plugins) participate only at ingest time in the delegated-ingest path;
they never appear in the read path.

## 10. What This Design Does NOT Cover

- Derived object series (transform DAG for objects) — deferred.
- Disk persistence / sqlite caching — deferred. The interface
  (`pushLazy` with a fetch callback) does not prevent adding disk-backed
  storage later; the callback can read from any source.
- Video GOP-aware eviction — deferred. Automatic eviction (§3.5)
  drops oldest entries without codec awareness, which can leave
  orphan P-frames whose keyframe has already been evicted. During
  live mode this is harmless (the viewer shows the newest entry and
  never decodes old P-frames). On transition from live to scrub it
  can leave the front of the buffer partially undecodable; pj_media's
  video decoder handles this by falling back to the next available
  keyframe, losing at most one GOP of scrubbable history at the
  front. A future extension could make eviction keyframe-aligned via
  a caller-supplied eviction hook — the hook would consult pj_media's
  `MediaIndexRegistry` (see `pj_media/docs/REQUIREMENTS.md §4.4`) and
  only permit evicting whole GOPs. Out of scope for the initial
  design because the fallback behavior is acceptable.
- Compression of owned bytes — deferred. Raw bytes are stored as-is.
  In-memory compression (e.g., LZ4) can be added transparently later
  inside `pushOwned` / `latestAt` without API changes.
