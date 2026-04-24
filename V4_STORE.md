# Plan: extend v4 plugin ABI with ObjectStore surface

## Context

`pj_datastore::ObjectStore` (already rebased into the working branch
from `media_implementation`) is a message-oriented peer to `DataEngine`
for timestamped opaque payloads (small structured messages like
markers and annotations, plus large blobs like images and point
clouds). Storage is in place; plugin-side wiring is not. The v4 ABI
(from `feat/v4-abi`) meanwhile hardened around a service-registry
model.

This plan proposes how to extend the v4 plugin ABI so plugins can write
into and read from ObjectStore alongside the existing `DataEngine`
surface, using the same service-registry philosophy we adopted in
v3.1/v4.

### Canary use case: MCAP plugin

One DataSource plugin, one open file, mixed payloads:

- **Scalars** (numeric channels, imu, odom) → delegated ingest.
  Plugin calls `host.ensureParserBinding()` per topic, pushes raw bytes
  via `host.pushRawMessage()`. The bound MessageParser decodes to
  `DataEngine` via the existing `pj.parser_write.v1` service. Unchanged.
- **Small structured messages** (e.g. `visualization_msgs/Marker`,
  2D scene primitives, ImageAnnotations, `diagnostic_msgs/DiagnosticArray`)
  → ObjectStore, **eager storage**. Plugin calls
  `objectWrite.registerTopic(name, metadata_json)` and per message calls
  `objectWrite.pushOwned(handle, ts_ns, serialized_bytes)` — the store
  copies the bytes in and owns them. Appropriate when per-message size is
  tens-to-hundreds of bytes and the full session fits comfortably in
  memory. A marker array for a 10-minute log at 10 Hz is <1 MB in
  total — eager is the obvious choice; there is no benefit to the lazy
  path's shared-file-reader bookkeeping.
- **Large blobs** (still images, point clouds) → ObjectStore, **lazy
  storage**. Plugin calls `objectWrite.registerTopic(...)` and for each
  message constructs a fetch callback that captures a
  `shared_ptr<mcap::Reader>` + the message's byte offset, and pushes
  via `objectWrite.pushLazy(handle, ts_ns, fetch_closure)`. Zero
  decode at load time; memory stays flat regardless of dataset size.
  Decode happens in the viewer when the user scrubs to that timestamp.

Video topics are **deferred**. Video needs both the auxiliary-index
mechanism (keyframe seek) and the viewer-side decoder to be useful;
shipping only storage for video would plant a half-wired feature. See
"Deferred — video topics" below.

If MCAP can express scalars + eager markers + lazy images through the
C ABI, the base design is proven.

### Existing v4 ABI shape (what we're extending)

The v4 ABI (commits `e57c852` + `59e841f`) uses a service-registry
pattern. Each host capability is a named service resolved at
`bind(services)` time:

| Service name | Consumer | Purpose |
|---|---|---|
| `pj.source_write.v1` | DataSource | scalar write, multi-topic |
| `pj.parser_write.v1` | MessageParser | scalar write, **topic-scoped** (one per parser instance) |
| `pj.toolbox_write.v1` | Toolbox | scalar read + write + catalog + `readSeriesArrow` |
| `pj.runtime.v1` | DataSource | progress, parser binding, raw dispatch |
| `pj.toolbox_runtime.v1` | Toolbox | message reporting, notifyDataChanged |
| `pj.colormap.v1` | Toolbox | optional, colormap registry |

Host-side plumbing lives in `pj_datastore/src/plugin_data_host.cpp`
(vtable builders + trampolines). Service trait structs live in
`pj_base/include/pj_base/sdk/service_traits.hpp`. SDK views live in
`pj_base/include/pj_base/sdk/plugin_data_api.hpp` and
`data_source_host_views.hpp`.

### Prerequisites (already rebased into the working branch)

The datastore-side pieces are already present on the current branch
(rebased from `media_implementation`):

- `pj_datastore/include/pj_datastore/object_store.hpp`
- `pj_datastore/src/object_store.cpp`
- `pj_datastore/tests/object_store_test.cpp`
- `pj_datastore/docs/OBJECT_STORE_DESIGN.md`

Plugin-boundary work starts directly from here — no branch or rebase
step is pending.

---

## Design decision: compose, don't break

The `OBJECT_STORE_DESIGN.md §6` and `pj_media/docs/REQUIREMENTS.md`
Prerequisites were written assuming a pre-v4 mental model, where
parsers receive a single write host bound at setup. They therefore
frame the "two-host parse()" requirement as an **ABI-breaking v2 bump**.

With v4's service registry in place, that framing is wrong. We can
deliver the same contract without bumping the parser protocol version:

**Add the object write host as a second, optional, topic-scoped
service** that parsers resolve alongside the scalar write host:

- `pj.parser_write.v1` — unchanged, topic-scoped scalar write.
- `pj.parser_object_write.v1` — **new**, topic-scoped object write,
  registered by the host only when the parser is bound to a media topic.

The parser's `parse()` signature does not change. A media-capable parser
overrides `bind()` to resolve both services (`require<ScalarWrite>()` +
`require<ObjectWrite>()` — or `optional<ObjectWrite>()` for
dual-purpose parsers). A scalar-only parser resolves only the scalar
service and keeps working unchanged. **No ABI break**, **no protocol
bump**, **no signature change**.

This applies the same asymmetry we already use for `pj.colormap.v1` (an
optional service resolved opportunistically by toolboxes that want it).

---

## Phases

Sequenced so each phase compiles and tests independently. Each phase is
committable on its own.

### Phase 1 — DataSource object write host (`pj.source_object_write.v1`)

**Goal:** let a DataSource plugin register object topics and push owned
or lazy payloads. This is what MCAP needs for image / pointcloud /
marker topics.

**New C ABI vtable** (`pj_base/include/pj_base/plugin_data_api.h`):

```c
typedef uint32_t PJ_object_topic_handle_t;  // opaque, 0 == invalid

typedef bool (*PJ_lazy_fetch_fn_t)(void* fetch_ctx,
                                   uint8_t** out_data, size_t* out_size);

typedef struct PJ_object_write_host_vtable_s {
  uint32_t version;          // starts at 1
  uint32_t size;

  PJ_object_topic_handle_t (*register_topic)(
      void* ctx, PJ_string_view_t topic_name, PJ_string_view_t metadata_json,
      PJ_error_t* out_error);

  bool (*push_owned)(void* ctx, PJ_object_topic_handle_t topic,
                     int64_t timestamp_ns,
                     const uint8_t* data, size_t size,
                     PJ_error_t* out_error);

  bool (*push_lazy)(void* ctx, PJ_object_topic_handle_t topic,
                    int64_t timestamp_ns,
                    PJ_lazy_fetch_fn_t fetch_fn,
                    void* fetch_ctx,
                    void (*fetch_ctx_destroy)(void*),
                    PJ_error_t* out_error);

  void (*set_retention_budget)(void* ctx, PJ_object_topic_handle_t topic,
                               int64_t time_window_ns,
                               size_t max_memory_bytes);

  const char* (*topic_metadata)(void* ctx, PJ_object_topic_handle_t topic);
} PJ_object_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_object_write_host_vtable_t* vtable;
} PJ_object_write_host_t;
```

**Design notes — not pure transcription of `OBJECT_STORE_DESIGN.md §6.1`:**
- Slots that can fail (`register_topic`, `push_*`) carry `PJ_error_t*` for
  consistent error propagation with v4 conventions. The design doc omits
  this; we add it to stay uniform.
- `set_retention_budget` remains infallible — it is just configuration.
  Note: per `pj_media/docs/REQUIREMENTS.md §4.3 + §4.4`, the **application**
  sets the retention budget, not the plugin. The plugin's slot exists only
  because Toolbox / transformer plugins may legitimately need it. DataSource
  plugins should leave budgets alone.

**New service traits** (`pj_base/include/pj_base/sdk/service_traits.hpp`):

```cpp
namespace PJ::sdk {
struct SourceObjectWriteHostService {
  static constexpr std::string_view kName = "pj.source_object_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_object_write_host_t;
  using Vtable = PJ_object_write_host_vtable_t;
  using View = SourceObjectWriteHostView;
};
}
```

**New C++ view** (`pj_base/include/pj_base/sdk/plugin_data_api.hpp`):

```cpp
class SourceObjectWriteHostView {
 public:
  [[nodiscard]] Expected<ObjectTopicHandle> registerTopic(
      std::string_view name, std::string_view metadata_json) const;

  [[nodiscard]] Status pushOwned(ObjectTopicHandle topic,
                                  Timestamp ts,
                                  Span<const uint8_t> payload) const;

  // SDK wraps a C++ lambda in the C callback trampoline.
  // The lambda is move-captured into a heap closure; destructor runs
  // exactly once when the store evicts the entry.
  template <class Fetch>
  [[nodiscard]] Status pushLazy(ObjectTopicHandle topic, Timestamp ts,
                                 Fetch&& fetch) const;

  void setRetentionBudget(ObjectTopicHandle topic,
                           int64_t time_window_ns,
                           size_t max_memory_bytes) const;
};
```

The `pushLazy(Fetch&&)` helper is the real ergonomic win — it hides the
`fetch_ctx` / `fetch_ctx_destroy` ABI dance behind a C++ closure. Pattern:

```cpp
// Heap-allocate a closure; trampoline casts the ctx back.
auto* closure = new std::function<std::vector<uint8_t>()>(std::move(fetch));
auto fetch_fn = +[](void* ctx, uint8_t** out, size_t* sz) -> bool { ... };
auto destroy = +[](void* ctx) {
  delete static_cast<std::function<std::vector<uint8_t>()>*>(ctx);
};
return pushLazyRaw(topic, ts, fetch_fn, closure, destroy);
```

**Host-side plumbing** (`pj_datastore/src/plugin_data_host.cpp`):

- Add `DatastoreSourceObjectWriteHost` class paralleling
  `DatastoreSourceWriteHost`. Holds a `std::shared_ptr<ObjectStore>` and a
  `DatasetId` resolved at creation time.
- Trampolines `sourceObjectRegisterTopic`, `sourceObjectPushOwned`,
  `sourceObjectPushLazy`, `sourceObjectSetBudget`,
  `sourceObjectTopicMetadata`.
- `pushLazy`: wrap the plugin's `fetch_ctx + fetch_ctx_destroy` in a
  `std::function<std::vector<uint8_t>()>` via a helper RAII struct that
  destroys the ctx on destruction, and hand that to
  `ObjectStore::pushLazy`.

**DataSource SDK change** (`data_source_plugin_base.hpp`):

- `DataSourcePluginBase::bind()` additionally does
  `services.optional<SourceObjectWriteHostService>()` and stores the view.
- Add `protected: const SourceObjectWriteHostView* objectWriteHost() const`
  that returns `nullptr` if the host did not provide the service.

**Tests:**
- `pj_datastore/tests/plugin_data_host_object_test.cpp` — push owned,
  push lazy (exercise the destroy callback), register topic, metadata
  round-trip.
- `pj_plugins/examples/mock_object_source.cpp` — a minimal
  DataSource that publishes a synthetic image topic. Two-line demo.

### Phase 2 — Toolbox object read host (`pj.toolbox_object_read.v1`)

**Goal:** let a Toolbox plugin read ObjectStore entries. Minimum
viable surface — write-from-toolbox deferred to phase 5.

**New C ABI vtable** — same file as phase 1:

```c
typedef struct PJ_object_bytes_handle_s* PJ_object_bytes_handle_t;

typedef struct PJ_object_read_host_vtable_s {
  uint32_t version;
  uint32_t size;

  PJ_object_topic_handle_t (*lookup_topic)(
      void* ctx, PJ_string_view_t topic_name);

  bool (*list_topics)(void* ctx,
                       PJ_object_topic_handle_t* out_buffer,
                       size_t buffer_capacity,
                       size_t* out_count,
                       PJ_error_t* out_error);

  const char* (*topic_metadata)(void* ctx, PJ_object_topic_handle_t topic);

  bool (*read_latest_at)(void* ctx, PJ_object_topic_handle_t topic,
                          int64_t timestamp_ns,
                          PJ_object_bytes_handle_t* out_handle,
                          int64_t* out_timestamp,
                          PJ_error_t* out_error);

  void (*get_bytes)(PJ_object_bytes_handle_t handle,
                     const uint8_t** out_data, size_t* out_size);

  void (*release_bytes)(PJ_object_bytes_handle_t handle);

  size_t (*entry_count)(void* ctx, PJ_object_topic_handle_t topic);

  bool (*time_range)(void* ctx, PJ_object_topic_handle_t topic,
                      int64_t* out_min_ts, int64_t* out_max_ts);
} PJ_object_read_host_vtable_t;
```

**Note on naming:** the design doc proposes *appending* these slots to
`PJ_toolbox_host_vtable_t`. A separate vtable + separate service is
cleaner because (a) it matches the one-service-per-capability pattern
already established in v4 and (b) future transformer plugins — which
may need object read but not scalar write — can pick and choose.

**RAII wrapper** in the SDK (`pj_base/include/pj_base/sdk/object_bytes.hpp`, new):

```cpp
class ObjectBytes {
 public:
  ObjectBytes() = default;
  ObjectBytes(ObjectBytes&&) noexcept;
  ~ObjectBytes();  // calls release_bytes via the stored vtable

  [[nodiscard]] Span<const uint8_t> view() const;
  [[nodiscard]] bool empty() const { return handle_ == nullptr; }
};
```

Move-only, zero copies. Decoder workers hold one across worker-thread
boundaries without any store lock — matches the `shared_ptr` model in
`OBJECT_STORE_DESIGN.md §4`.

**View** (`plugin_data_api.hpp`):

```cpp
class ToolboxObjectReadHostView {
 public:
  std::optional<ObjectTopicHandle> lookupTopic(std::string_view name) const;
  std::vector<ObjectTopicHandle> listTopics() const;
  std::string_view topicMetadata(ObjectTopicHandle) const;
  [[nodiscard]] Expected<ObjectBytes> readLatestAt(
      ObjectTopicHandle, Timestamp, Timestamp* out_ts = nullptr) const;
  size_t entryCount(ObjectTopicHandle) const;
  std::pair<Timestamp, Timestamp> timeRange(ObjectTopicHandle) const;
};
```

**Host plumbing**: `DatastoreToolboxObjectReadHost` in
`plugin_data_host.cpp`. `PJ_object_bytes_handle_t` is cast from a
`shared_ptr<const std::vector<uint8_t>>*` allocated via `new` on each
successful `read_latest_at`; `release_bytes` deletes it. The
`shared_ptr` keeps the bytes alive independent of the store — exactly
the OBJECT_STORE_DESIGN.md contract.

**Toolbox SDK change** (`toolbox_plugin_base.hpp`):

- `bind()` additionally does
  `services.optional<ToolboxObjectReadHostService>()`.
- Add `protected: const ToolboxObjectReadHostView* objectReadHost() const`.

**Tests:**
- `pj_datastore/tests/plugin_data_host_object_read_test.cpp` — round-trip
  write-via-host + read-via-host, owning-handle lifetime across store
  mutations, `ObjectBytes` destructor releases correctly.

### Phase 3 — MessageParser object write as optional service

**Goal:** deliver the "two-host `parse()`" contract from
`pj_media/docs/REQUIREMENTS.md` Prerequisites **without bumping the
parser protocol version**.

**New service trait** — same vtable shape as phase 1
(`PJ_object_write_host_vtable_t`), new service name:

```cpp
struct ParserObjectWriteHostService {
  static constexpr std::string_view kName = "pj.parser_object_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_object_write_host_t;  // same vtable as source variant
  using View = ParserObjectWriteHostView;
};
```

**Host behavior** — when the host creates a parser instance for a
topic, it populates the registry with:

- `pj.parser_write.v1` (topic-scoped scalar) — always.
- `pj.parser_object_write.v1` (topic-scoped object) — **only when the
  host has an object-capable target** for the parser (e.g., delegated
  ingest from a DataSource that registered an object topic alongside
  the scalar topic).

**MessageParser SDK change** (`message_parser_plugin_base.hpp`):

```cpp
Status bind(sdk::ServiceRegistry services) override {
  auto scalar = services.require<ParserWriteHostService>();
  if (!scalar) return scalar.status();
  write_host_view_ = *scalar;

  auto object = services.optional<ParserObjectWriteHostService>();
  if (object) object_write_host_view_ = *object;
  return okStatus();
}
```

Scalar-only parsers work unchanged. Media-capable parsers override
`bind()` to tighten the object host from optional to required, or just
check `objectWriteHost() != nullptr` inside `parse()`.

**Delegated ingest wiring** (host side,
`pj_plugins/src/message_parser_host.cpp`): when the DataSource calls
`ensureParserBinding({topic, encoding, schema, object_topic?})` with an
`object_topic` field, the host's per-binding service registry gets both
services. The `PJ_parser_binding_request_t` struct grows one optional
field: `PJ_object_topic_handle_t object_topic; // 0 == scalar-only`.

**Tests:**
- `pj_plugins/tests/parser_two_host_test.cpp` — a mock parser receives
  both hosts, writes a scalar field and an object payload from a single
  `parse()` call, asserts both land.

### Phase 4 — SDK ergonomics: typed handle, metadata builder

- **Typed `ObjectTopicHandle`** — not just `uint32_t`. A one-member
  struct with `operator==`, `bool(handle)`. Same pattern as the
  existing `TopicHandle` / `FieldHandle`.
- **`pushLazy(Fetch&&)` helper** (phase 1) and
  **`pushOwned(std::vector<uint8_t>&&)` rvalue overload** — move into
  the store on the C++ side when we can, fall back to copy on the C
  ABI slot. Mirrors the `appendArrowStream(ArrowStreamHolder&&)`
  pattern from Tier 1b of the previous plan.
- **`MediaMetadataBuilder`** — tiny helper for constructing the JSON
  string the design doc mandates:
  ```cpp
  auto meta = MediaMetadataBuilder()
      .mediaClass("video")
      .encoding("h264")
      .schema("foxglove/CompressedVideo")
      .build();
  host.registerTopic(name, meta);
  ```
  Three documented keys (`media_class`, `encoding`, `schema`) become
  typed methods; the builder emits minimal valid JSON with no external
  dep. Prevents typos that would break viewer auto-routing.

### Phase 5 — Toolbox object write (transformer plugins, future)

Deferred. Once transformer plugins become real, add
`pj.toolbox_object_write.v1` reusing the same
`PJ_object_write_host_vtable_t`. All plumbing patterns from phase 1
apply.

Not scoped in this plan; noted so reviewers know the shape.

### Phase 6 — MCAP plugin demonstration

Once phases 1–4 land, port the MCAP plugin (on `pj_official_plugins`)
to the new surface. Four internal modes:

1. **Scalar topics** — existing delegated parser binding, unchanged.
2. **Small-message topics (eager)** — e.g.
   `visualization_msgs/Marker`, ImageAnnotation, scene primitives.
   - At file open: register one object topic per channel with
     `MediaMetadataBuilder`.
   - Per message: `pushOwned(handle, ts_ns, bytes)` — the store
     takes ownership of the serialized payload. No fetch closure, no
     shared-reader bookkeeping.
3. **Large-blob topics (lazy)** — still images, point clouds.
   - At file open: register one object topic per channel.
   - Per message: `pushLazy` with a closure capturing
     `{shared_ptr<mcap::Reader>, message.data_offset,
     message.data_size}`.
4. **Shared topics** (e.g., `sensor_msgs/CompressedImage` with scalar
   `header.seq` + bytes): delegated ingest with a CDR parser that
   resolves both `pj.parser_write.v1` and `pj.parser_object_write.v1`,
   writes `header.seq` to the former and JPEG bytes to the latter — from
   a single `parse()` call. The parser's object-side push is `pushOwned`
   here because the parser is given already-deserialized inner bytes
   from the CDR envelope — the seek-and-reread shape doesn't apply.

**Video channels are skipped** at file-open time: the plugin logs them
as "deferred" and does not register object topics for them. See
"Deferred — video topics" in Out of scope.

Validate end-to-end: open an MCAP with scalars + eager markers + lazy
images, confirm scalars land in `DataEngine`, confirm the markers
channel reports the right `entryCount` immediately after load (no
lazy unroll needed), confirm `latestAt(image_topic, ts)` on the
image channel invokes the fetch callback and returns bytes, confirm
memory after load is dominated by the eager small-message data and
not by the image bytes (lazy closures only), confirm `evictBefore`
tears down `fetch_ctx_destroy` correctly on the lazy channel.

---

## Critical files

### New files

| File | Phase | Purpose |
|---|---|---|
| `pj_base/include/pj_base/sdk/object_bytes.hpp` | 2 | `ObjectBytes` RAII wrapper |
| `pj_base/include/pj_base/sdk/object_topic_handle.hpp` | 4 | Typed handle struct |
| `pj_base/include/pj_base/sdk/media_metadata.hpp` | 4 | `MediaMetadataBuilder` |
| `pj_plugins/examples/mock_object_source.cpp` | 1 | Canary plugin |
| `pj_datastore/tests/plugin_data_host_object_test.cpp` | 1 | Write surface tests |
| `pj_datastore/tests/plugin_data_host_object_read_test.cpp` | 2 | Read surface tests |
| `pj_plugins/tests/parser_two_host_test.cpp` | 3 | Two-host parser test |

### Files touched

| File | Phase | Change |
|---|---|---|
| `pj_base/include/pj_base/plugin_data_api.h` | 1, 2 | Add two new vtables + handle types |
| `pj_base/include/pj_base/sdk/service_traits.hpp` | 1, 2, 3 | Add three service trait structs |
| `pj_base/include/pj_base/sdk/plugin_data_api.hpp` | 1, 2, 3, 4 | Add three views + RAII helpers |
| `pj_base/include/pj_base/sdk/data_source_plugin_base.hpp` | 1 | `bind()` resolves optional object write |
| `pj_base/include/pj_base/sdk/toolbox_plugin_base.hpp` | 2 | `bind()` resolves optional object read |
| `pj_base/include/pj_base/sdk/message_parser_plugin_base.hpp` | 3 | `bind()` resolves optional object write |
| `pj_datastore/src/plugin_data_host.cpp` | 1, 2 | Add three host classes + trampolines |
| `pj_datastore/src/plugin_data_host.hpp` | 1, 2 | Declare new host classes |
| `pj_base/include/pj_base/data_source_protocol.h` | 3 | `PJ_parser_binding_request_t` gains `object_topic` field |
| `pj_plugins/src/message_parser_host.cpp` | 3 | Delegated-ingest wiring resolves both services |
| `pj_plugins/docs/data-source-guide.md` | 1, 4 | Document new write surface + MCAP pattern |
| `pj_plugins/docs/toolbox-guide.md` | 2 | Document new read surface |
| `pj_plugins/docs/message-parser-guide.md` | 3 | Document optional second host |
| `pj_plugins/docs/ARCHITECTURE.md` | 1–3 | New services in service table |
| `pj_plugins/docs/REQUIREMENTS.md` | 1–3 | Object read/write in permissions table |

### Reused from existing SDK

- `PJ::sdk::Status`, `Expected<T>` — `pj_base/expected.hpp`
- `PJ::Timestamp`, `PJ::DatasetId` — `pj_base/types.hpp`
- `ServiceRegistry::optional<T>()` / `require<T>()` — already used for
  `pj.colormap.v1` (model to copy)
- Service-trait layout — `service_traits.hpp` (13 existing examples)
- Vtable-builder pattern — `plugin_data_host.cpp` (three existing hosts)
- C-ABI trampoline pattern (exception-safe, `PJ_error_t*` propagation) —
  every existing trampoline in `plugin_data_host.cpp`

### Key existing functions to mirror

- `DatastoreParserWriteHost` (`plugin_data_host.cpp:912`) — exact
  template for topic-scoped object write host.
- `toolboxReadSeriesArrow` — pattern for host-owned resources returned
  to plugin via opaque handle + release callback (the same shape used
  for `PJ_object_bytes_handle_t`).
- `SourceWriteHostView::appendArrowStream(ArrowStreamHolder&&)` —
  template for the `pushLazy(Fetch&&)` ergonomic overload (same
  "hide the ABI dance" idea).
- `ServiceRegistry::optional<ColorMapRegistryService>()` — template
  for all three new optional services.

---

## Verification

Each phase is a committable unit. After every phase:

```bash
./build.sh --debug && ./test.sh
```

Must stay at 52/52 Debug+ASAN green (+ new tests from that phase).
Release (60/60) must also pass. `./run_clang_tidy.sh` clean.

### Phase-specific end-to-end checks

- **Phase 1**: `mock_object_source` loads, registers two topics,
  pushes 100 owned payloads + 100 lazy payloads, `fetch_ctx_destroy`
  counters confirm no leaks when the ObjectStore clears.
- **Phase 2**: `mock_object_source` + a mock toolbox that reads every
  entry back; confirm byte-exact round-trip and that `ObjectBytes`
  handles survive a concurrent `pushOwned` that triggers eviction.
- **Phase 3**: mock CDR-ish parser that takes a `{"seq":7, "jpeg":"..."}`
  fake payload, emits `seq` to scalar host and `jpeg` bytes to object
  host. Both sinks receive the expected content.
- **Phase 6 (MCAP)**: open a real MCAP with scalars + eager markers
  + lazy images. Confirm the markers channel reports the right
  `entryCount` immediately after load (eager push completes during
  scan). Confirm the image channel's memory footprint is dominated
  by lazy closures (file-size-independent). Scrub to timestamps,
  confirm image bytes are produced by the fetch callback. Confirm
  evicting a lazy entry drops its refcount on the captured
  `shared_ptr<mcap::Reader>`. Video channels are skipped; the plugin
  logs them as "deferred".

### ASAN gates

- `ObjectBytes` destructor runs exactly once under every exit path
  (early return, exception, move-assign).
- `fetch_ctx_destroy` runs exactly once per lazy entry (tested via
  atomic counter in the mock).
- No `shared_ptr` cycles between plugin-captured context and store
  state.

---

## Out of scope

- **Video topics (end-to-end)** — compressed video (H.264, AV1, VP9)
  is explicitly deferred. Storing the bytes is trivial with the base
  surface, but without keyframe indexing the viewer cannot seek, and
  without the pj_media decoder pipeline the bytes cannot be rendered.
  Delivering only the storage half would plant a half-wired feature
  that looks supported but is not. The MCAP port skips video channels
  at open time and logs them as deferred.
- **Auxiliary topic indices** — `publish_keyframe_index` was
  deliberately removed from the object write vtable. Keyframe tracking
  is one instance of a more general problem: some consumers need
  auxiliary, per-topic lookup tables alongside the raw payloads
  (keyframe timestamps for video, spatial tiles for large point
  clouds, thumbnail timestamps for image-heavy datasets, chapter
  markers, etc.). A general mechanism — likely a named-side-channel
  API on the ObjectStore, e.g. `attachSideChannel(topic, kind_id,
  bytes)` / `getSideChannel(topic, kind_id)` — should be designed
  as a separate piece of work and landed after the base ObjectStore
  plugin surface is proven. Video topics are the most visible
  beneficiary, but they are not the only one.
- **Transformer plugins** (toolbox-side object write) — phase 5
  placeholder only.
- **Disk-backed object persistence** — deferred in
  OBJECT_STORE_DESIGN.md §10, still deferred here.
- **Compression of owned bytes** — same.
- **GOP-aware eviction for video** — OBJECT_STORE_DESIGN.md §10;
  current eviction is time/memory-only.
- **pj_media_core viewer decoder side** — lives in its own module,
  not a plugin concern. The plugin surface here is agnostic to what
  viewers do with the bytes.
- **`TimelineCursor`** (pj_base) — separate prerequisite tracked in
  `pj_media/docs/REQUIREMENTS.md` Prerequisites; unrelated to the
  plugin ABI.
- **Parser protocol version bump** — intentionally avoided by the
  optional-service design (see "Design decision").

---

## Resolved design questions

1. **Service naming.** Long, caller-scoped —
   `pj.source_object_write.v1`, `pj.parser_object_write.v1`,
   `pj.toolbox_object_read.v1`. Matches existing v4 naming discipline.
2. **Keyframe index + video topics.** Both deferred entirely.
   `publish_keyframe_index` is out of the vtable, and video topics are
   skipped by the MCAP port. Keyframe indexing is a special case of a
   more general auxiliary-index mechanism that deserves its own
   design; delivering only storage for video without decode/seek would
   be a half-wired feature. See the two "Deferred — …" bullets in Out
   of scope.
3. **Phase 6 scope.** Port the MCAP plugin directly. No synthetic
   canary. Real bag file is the validation target.
