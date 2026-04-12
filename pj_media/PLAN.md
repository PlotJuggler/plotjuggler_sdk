# pj_media Implementation Plan

Incremental milestones with automated validation. Each milestone
produces a commit with passing tests. File-backed 2D data first,
then video, then streaming.

## Completed milestones

| Milestone | Commit | What |
|-----------|--------|------|
| M1 (includes M2-M4) | `4102de8` | ObjectStore: register, push, query, concurrency, iteration, retention. 32 tests |
| M6 | `71ca718` | FrameSlot + CancelToken + DecodedFrame. 9 tests |
| M7 | `88dd423` | ImageDecoder (turbojpeg + raw). 6 tests |
| M8 + M15 | `0dd0160` | MCAP→ObjectStore integration (4 tests) + pj_media_qt with QRhiWidget (MediaViewerWidget: GPU rendering, zoom/pan, pre-compiled shaders) + demo binary. M15 merged into M8 |
| M9 + M10 | `59ab9e3` | VideoBackend abstraction + MpvBackend (libmpv) + VideoViewerWidget + mp4_video_viewer demo. API doc comments on all public headers |
| cleanup | `6a592b7` | Scrub throttle 60 Hz, VideoViewerWidget returns VideoBackend* (not MpvBackend*) |
| review fixes | *pending* | ARCHITECTURE.md updated for mpv/VideoBackend v1 strategy. PNG decode added to ImageDecoder |

### Build notes

- `pj_media_qt` requires Qt 6.8+ (gracefully skips if not found).
  Pass `-DQt6_DIR=/path/to/Qt/6.8.3/gcc_64/lib/cmake/Qt6` to cmake.
- Shaders are pre-compiled `.qsb` files (no ShaderTools build dep).
  Re-run `qsb --glsl 440 --hlsl 50 --msl 12` if shaders change.
- `dialog_engine_test` works with Qt 6.8 but may not build during
  partial builds. Run full `cmake --build` to ensure all targets.
- **Future**: compare system libmpv vs locally-built latest mpv
  (git submodule) for performance. VideoBackend abstraction allows
  swapping without widget changes.

## Test data

All in `pj_media/testdata/`:

| File | Size | Content |
|------|------|---------|
| `test_images.mcap` | 2.4 MB | Foxglove CompressedImage, 30 Hz, 90 msgs |
| `test_video.mcap` | 1.6 MB | Foxglove CompressedVideo, 30 Hz, 150 msgs |
| `test_480p.mp4` | 1.6 MB | H.264 480p |
| `test_1080p.mp4` | 7.8 MB | H.264 1080p |
| `potato.mcap` | 7.5 GB | ROS2 CompressedImage 30 Hz + depth + IMU, ~5 min (**gitignored**) |

---

## Phase 1 — ObjectStore

Foundation for all media storage. Implemented in `pj_datastore`, tested
with GTest. No Qt, no media dependencies. Each slice is one commit.

### M1: ObjectStore bare bones

**Scope**: `registerTopic`, `pushOwned`, `pushLazy`, `latestAt`, `at`,
`timeRange`, `entryCount`, `evictBefore`, `removeTopic`, `clear`.
Single-threaded. Owning `shared_ptr` handles.

**Files**:
- `pj_datastore/include/pj_datastore/object_store.hpp`
- `pj_datastore/src/object_store.cpp`
- `pj_datastore/tests/object_store_test.cpp`
- `pj_datastore/CMakeLists.txt` (add source + test)

**Tests** (GTest):
- Register topic, verify descriptor roundtrip
- Push 100 owned entries, verify `entryCount`, `timeRange`
- `latestAt` at exact timestamp, between timestamps, before first, after last
- `at(i)` for all valid indices, out of range returns nullopt
- `pushLazy` with fetch callback, verify resolve returns correct bytes
- Owning handle survives after `evictBefore` removes the entry from store
- `removeTopic` clears entries, subsequent queries return empty
- `clear` removes all topics
- Timestamp monotonicity: push out-of-order returns error
- Empty topic: all queries return empty/nullopt/zero

**Validation**: `./build.sh --debug && ./test.sh`

### M2: ObjectStore concurrency

**Scope**: Add `shared_mutex` per `ObjectSeries`. Read methods acquire
shared lock; write methods acquire exclusive lock. Returned handles are
owning — reader releases lock immediately after handle copy.

**Files**:
- `pj_datastore/src/object_store.cpp` (add locking)
- `pj_datastore/tests/object_store_concurrency_test.cpp`

**Tests**:
- 2 reader threads polling `latestAt` in a loop while main thread
  pushes 10000 entries — no crash, no data race (ASAN + TSAN clean)
- Reader holds a `ResolvedObjectEntry` handle, writer evicts the
  underlying entry — handle remains valid (shared_ptr keeps bytes alive)
- `listTopics` concurrent with `registerTopic` — no crash

**Validation**: `./build.sh --debug && ./test.sh` (ASAN enabled in
debug builds)

### M3: ObjectStore entry iteration

**Scope**: `indexAt`, `entryTimestamps` (with `EntryTimestampsView`
RAII type).

**Files**:
- `pj_datastore/include/pj_datastore/object_store.hpp` (add types)
- `pj_datastore/src/object_store.cpp`
- `pj_datastore/tests/object_store_iteration_test.cpp`

**Tests**:
- `indexAt` edge cases: empty topic, before t_min, after t_max, exact
  match, between entries
- `entryTimestamps` view: size matches `entryCount`, `operator[]`
  returns correct timestamps, iterators work with range-for
- View stability: entries visible via view don't change while view
  is alive (writer blocks until view is dropped)
- View + `indexAt` round-trip: `at(indexAt(ts))` matches
  `latestAt(ts)`

### M4: ObjectStore retention

**Scope**: `RetentionBudget`, `setRetentionBudget`, automatic eviction
inside push, `memoryUsage`.

**Files**:
- `pj_datastore/include/pj_datastore/object_store.hpp` (add types)
- `pj_datastore/src/object_store.cpp`
- `pj_datastore/tests/object_store_retention_test.cpp`

**Tests**:
- Time-window budget: push entries spanning 10 s with 2 s window,
  verify oldest entries evicted, `timeRange` width ≤ 2 s
- Memory budget: push 1 MB entries with 5 MB cap, verify
  `memoryUsage` stays ≤ 5 MB
- Combined budget: both axes active, verify tighter constraint wins
- Lazy entries contribute zero to `memoryUsage`
- Default budget (both zero): no eviction after 10000 pushes
- Budget change: `setRetentionBudget` takes effect on next push,
  not retroactively

### M5: ObjectStore C ABI

**Scope**: `PJ_object_write_host_vtable_t` (register, push_owned,
push_lazy, set_retention_budget, publish_keyframe_index).
`PJ_object_bytes_handle_t` read host (read_object_latest_at,
get_object_bytes, release_object_bytes, lookup_object_topic,
list_object_topics).

**Files**:
- `pj_base/include/pj_base/object_store_api.h` (C ABI types)
- `pj_datastore/include/pj_datastore/object_store_host.hpp` (vtable impl)
- `pj_datastore/src/object_store_host.cpp`
- `pj_datastore/tests/object_store_cabi_test.cpp`

**Tests**:
- C ABI round-trip: register topic via vtable, push_owned via vtable,
  read_object_latest_at via vtable, verify bytes match
- push_lazy via vtable with C function pointer + fetch_ctx, verify
  resolve works and fetch_ctx_destroy is called on eviction
- Handle acquire/release: release_object_bytes frees the handle,
  double-release is UB (document, don't test)
- `struct_size` forward-compat: host accepts a vtable with larger size
  (newer plugin on older host ignores trailing slots)
- `lookup_object_topic` returns correct handle, 0 for unknown name
- `publish_keyframe_index` stores timestamps, retrievable via
  C++ `MediaIndexRegistry` API

---

## Phase 2 — pj_media core (images from file)

First visual output: read JPEG images from an MCAP file, display
them in a Qt widget with a timeline slider. Validates: ObjectStore
read path, ImageDecoder, FrameSlot, PlaybackController, QRhiWidget.

### M6: pj_media_core foundation types

**Scope**: `FrameSlot`, `CancelToken`, `DecodedFrame`, `CompositeFrame`
types. Pure C++, no Qt, no external deps beyond pj_base.

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/frame_slot.h`
- `pj_media/pj_media_core/include/pj_media_core/cancel_token.h`
- `pj_media/pj_media_core/include/pj_media_core/decoded_frame.h`
- `pj_media/pj_media_core/CMakeLists.txt`
- `pj_media/pj_media_core/tests/frame_slot_test.cpp`
- `pj_media/pj_media_core/tests/cancel_token_test.cpp`

**Tests**:
- FrameSlot: store + take returns frame; take without store returns
  nullopt; store overwrites previous (latest-wins); concurrent
  store/take from two threads (ASAN clean)
- CancelToken: initial state is not cancelled; cancel() flips state;
  shared_ptr sharing works across threads

### M7: ImageDecoder

**Scope**: Stateless decoder dispatching to turbojpeg (JPEG) and
raw pixel copy. Takes raw bytes, returns `DecodedFrame`.

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/image_decoder.h`
- `pj_media/pj_media_core/src/image_decoder.cpp`
- `pj_media/pj_media_core/tests/image_decoder_test.cpp`
- `pj_media/pj_media_core/CMakeLists.txt` (link turbojpeg)

**Tests**:
- Decode a known JPEG buffer (extract one message from
  `test_images.mcap` at build time, or embed a small test JPEG) →
  verify width, height, pixel format, non-null data pointer
- Decode raw RGB bytes → verify passthrough
- Decode empty/corrupt buffer → returns error, no crash
- Decode JPEG with CancelToken pre-cancelled → returns early

### M8: MCAP → ObjectStore loader + image display demo

**Scope**: End-to-end integration. Load `test_images.mcap`, push
entries into ObjectStore via direct ingest (pushLazy with MCAP seek
callbacks), display images in a Qt widget with a slider.

This milestone creates the first `PlaybackController` (single worker
thread) and `MediaViewerWidget` (QOpenGLWidget rendering decoded
RGB images — simpler than QRhiWidget for the first milestone).

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/playback_controller.h`
- `pj_media/pj_media_core/src/playback_controller.cpp`
- `pj_media/pj_media_qt/include/pj_media_qt/media_viewer_widget.h`
- `pj_media/pj_media_qt/src/media_viewer_widget.cpp`
- `pj_media/pj_media_qt/CMakeLists.txt`
- `pj_media/demos/mcap_image_viewer.cpp` (demo binary)
- `pj_media/pj_media_core/tests/playback_controller_test.cpp`

**Demo**: `mcap_image_viewer <test_images.mcap>` — opens the file,
shows images, slider scrubs through the timeline.

**Tests**:
- PlaybackController: construct with ObjectStore + topic, request
  timestamp, poll FrameSlot → returns decoded image frame. No Qt.
- Direction-aware cancel-store: rapid forward requests → FrameSlot
  receives frames. Rapid backward requests → no stale forward frames
  leak through.
- Integration: load `test_images.mcap` → ObjectStore, verify
  `entryCount` matches expected (90), `latestAt` at midpoint returns
  bytes, ImageDecoder produces a frame with expected dimensions.

**Validation**: `./test.sh` + manual run of demo binary with
`test_images.mcap`.

---

## Phase 3 — Video via libmpv

Video playback using libmpv behind a `VideoBackend` abstraction.
libmpv opens files directly — it does NOT read from ObjectStore
(it has its own demuxer, decoder, and cache). Synchronization with
the global timeline is via seeking.

### M9: VideoBackend abstraction + libmpv implementation

**Scope**: Define a `VideoBackend` interface that pj_media_qt uses.
Implement `MpvBackend` as the v1 backend. The abstraction allows
swapping to a custom FFmpeg pipeline later without changing the widget
or controller.

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/video_backend.h`
  (abstract interface: open, seek, pause, stepForward, stepBackward,
  duration, position; frame delivery via OpenGL FBO or callback)
- `pj_media/pj_media_qt/include/pj_media_qt/mpv_backend.h`
- `pj_media/pj_media_qt/src/mpv_backend.cpp`
- `pj_media/pj_media_qt/CMakeLists.txt` (link libmpv via pkg-config)

**VideoBackend interface** (sketch):
```cpp
class VideoBackend {
 public:
  virtual ~VideoBackend() = default;
  virtual bool open(const std::string& path) = 0;
  virtual void seek(double seconds) = 0;
  virtual void setPaused(bool paused) = 0;
  virtual double duration() const = 0;
  virtual double position() const = 0;
  // Frame delivery: backend renders into the widget's GL context
  virtual void renderFrame(int fbo_id, int width, int height) = 0;
};
```

`MpvBackend` wraps the libmpv pattern from
`video_player_lab/src/mpv_widget.cpp`: `mpv_create`, `mpv_initialize`,
`mpv_render_context_create`, `mpv_render_context_render`.

**Tests**:
- `MpvBackend::open(test_480p.mp4)` → `duration() > 0`
- `seek(1.0)` → `position()` near 1.0
- `setPaused(true/false)` toggles state

**Note**: These tests need a display context (or offscreen GL).
If headless testing is problematic, mark them as integration tests
and rely on the demo binary for validation.

### M10: Video viewer widget + demo

**Scope**: `VideoViewerWidget` — a QOpenGLWidget that owns a
`MpvBackend` and renders video frames. Synced to timeline via
`seek()` calls from a slider.

**Files**:
- `pj_media/pj_media_qt/include/pj_media_qt/video_viewer_widget.h`
- `pj_media/pj_media_qt/src/video_viewer_widget.cpp`
- `pj_media/demos/mp4_video_viewer.cpp`

**Demo**: `mp4_video_viewer <test_480p.mp4>` — opens MP4, plays
video, slider scrubs, pause/resume.

**Validation**: manual run with `test_480p.mp4` and `test_1080p.mp4`.

---

## Phase 4 — Multi-source integration

Prove the architecture handles multiple data types from the same
file, rendered in synchronized widgets.

### M11: MCAP multi-channel demo (image + scalar)

**Scope**: Load `potato.mcap`, display the RGB camera image in a
`MediaViewerWidget` AND plot IMU scalars in a time-series view
(or just print them). Both channels read from the same MCAP file,
one via ObjectStore (images), one via DataEngine (scalars).

This validates the dual-store model: `DataEngine` for scalars,
`ObjectStore` for media, same DataSource, same timeline.

**Files**:
- `pj_media/demos/mcap_multi_viewer.cpp`

**Tests**:
- Integration: load `potato.mcap`, verify ObjectStore has image
  topic with ~8400 entries and DataEngine has IMU topics.

### M12: Multi-layer compositor

**Scope**: Compositor class that combines a base image with an
annotation overlay (for now, synthetic — draw bounding boxes on top
of the decoded image). Single worker thread decodes all layers
sequentially, composites, writes to FrameSlot.

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/compositor.h`
- `pj_media/pj_media_core/src/compositor.cpp`
- `pj_media/pj_media_core/tests/compositor_test.cpp`

**Tests**:
- Two layers (base image + synthetic overlay), compositor produces
  a blended frame with expected dimensions
- Single layer (base only) → output equals input
- Missing layer data (one topic has no entry at requested time) →
  compositor uses available layers, no crash

---

## Phase 5 — Streaming

Prove live⊥scrub architecture works with push-driven data.

### M13: Simulated streaming source

**Scope**: A test harness that pushes synthetic JPEG frames into
ObjectStore at 30 Hz from a background thread, simulating a live
camera. The `MediaViewerWidget` displays the latest frame.

**Files**:
- `pj_media/demos/simulated_stream.cpp`
- `pj_media/pj_media_core/tests/streaming_test.cpp`

**Tests**:
- Push 300 frames (10 s at 30 Hz) with 2 s retention window →
  verify `timeRange` width stays ≤ 2 s throughout
- Pause (stop pushing) → `timeRange` is frozen, scrub through
  retained buffer → all frames accessible
- Resume pushing → old frames evicted, new frames appear
- Concurrent reader (PlaybackController polling `latestAt`) during
  push → ASAN clean, no stale frames

### M14: Live mode viewer

**Scope**: Wire the simulated stream to a `MediaViewerWidget` in
live mode. Slider shows live edge. Pause freezes the buffer for
scrub. Resume returns to live.

**Files**:
- `pj_media/demos/live_viewer.cpp`

**Validation**: manual run — video streams, pause freezes, scrub
works within retained window, resume returns to live edge.

---

## Phase 6 — Polish

### M15: QRhiWidget upgrade

**Scope**: Replace `QOpenGLWidget` with `QRhiWidget` for the image
viewer. Custom YUV-to-RGB shader. Zoom (mouse wheel) and pan
(mouse drag) via vertex shader transform matrix.

**Files**:
- `pj_media/pj_media_qt/src/media_viewer_widget.cpp` (rewrite)
- `pj_media/pj_media_qt/shaders/yuv_to_rgb.vert`
- `pj_media/pj_media_qt/shaders/yuv_to_rgb.frag`

**Validation**: manual run — verify zoom/pan works, YUV rendering
is correct (no color artifacts).

### M16: SceneDecoder + annotation overlay

**Scope**: Deserialize CDR/Protobuf scene primitives and image
annotations. Render as overlays on the base image.

Deferred until there is concrete test data with annotations.

---

## Build integration

### CMake structure

```
pj_media/
├── pj_media_core/
│   ├── CMakeLists.txt          # static lib, no Qt
│   ├── include/pj_media_core/
│   ├── src/
│   └── tests/
├── pj_media_qt/
│   ├── CMakeLists.txt          # static lib, Qt 6.8+
│   ├── include/pj_media_qt/
│   ├── src/
│   └── shaders/
├── demos/
│   ├── CMakeLists.txt
│   └── *.cpp
├── testdata/
│   └── *.mcap, *.mp4
├── docs/
│   └── ARCHITECTURE.md, REQUIREMENTS.md, ...
└── PLAN.md
```

### Dependencies to add

| Dependency | Source | Used by |
|------------|--------|---------|
| turbojpeg | system pkg or conan | pj_media_core (ImageDecoder) |
| libmpv | system pkg (`pkg_check_modules`) | pj_media_qt (MpvBackend) |

### .gitignore additions

```
pj_media/testdata/potato.mcap
```

---

## Milestone dependency graph

```
M1 → M2 → M3 → M4 → M5   (ObjectStore)
                 ↓
M6 → M7 → M8              (images from file)
            ↓
           M11 → M12       (multi-source, compositor)
            ↓
           M13 → M14       (streaming)
            ↓
           M15 → M16       (polish)

M9 → M10                   (video via libmpv, independent of M3-M5)
```

M9-M10 (libmpv video) can proceed in parallel with M3-M5
(ObjectStore iteration/retention/C ABI) since libmpv bypasses
ObjectStore entirely.
