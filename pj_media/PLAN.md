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
| review fixes | `d90807d` | ARCHITECTURE.md updated for mpv/VideoBackend v1 strategy. PNG decode added to ImageDecoder |
| M11 | `7866fa8` | Dual-store test: ObjectStore + DataEngine from same MCAP. Multi-channel demo (color + depth) |
| CodecPipeline | `10f86b3` | CodecPipeline + codecs + split pj_media_qt libs |
| QRhi fix | `dd101e7` | Multi-instance QRhiWidget init (bootstrap widget + lifecycle) |
| M13 | `43248a1` | Streaming: 4 tests (retention window, memory cap, pause/scrub, concurrent) + simulated_stream demo |
| simplify | `31a8ef7` | Codex + agent review — rename pipelines, remove dead files |
| FFmpeg backend | `5510b3d` | FfmpegDecoder: AVCodecContext wrapper with HW-accel probing, CancelToken, YUV420P output. Tests with real MP4 |
| FFmpeg backend v2 | `d49b8df` | FfmpegBackend: dual-backend mp4_video_viewer demo (mpv + ffmpeg) |
| scrub overhaul | `eaea822` | FfmpegBackend scrub: forward threshold (100 frames), decodeSkip, direction-aware partial filter, seek throttle 30 Hz, min decode time 60ms, B-frame PTS fix, processEvents delivery, CancelToken wired to decoder |
| YUV GPU pipeline | `2660f03` | FfmpegDecoder outputs YUV420P (no CPU RGB conversion). MediaViewerWidget: BT.709 YUV->RGB fragment shader, 3 R8 textures. 75% GPU memory reduction vs RGBA8. Backward-compatible QImage path kept |
| thumbnail cache | `d67cdb8` | ThumbnailCache: background thread pre-decodes 1 frame/sec at open. Auto-scales to max 1920px for 4K. JPEG quality 85 (~90KB/frame 1080p). YUV420P throughout. Instant backward scrub feedback |
| scrub refinement | `bb23e16` | Target refinement within same GOP for backward scrub. Keyframe-then-refine strategy eliminates forward jumps |
| integration tests | `b7b6490` | 23 integration tests: play, forward/backward scrub at 480p/1080p/4K/1920p-B-frames, pause/unpause, bidirectional, close safety, responsiveness, settle behavior |
| M17+M18: streaming video | `0601835` | StreamingVideoDecoder: ObjectStore + FfmpegDecoder bridge. H.264 NAL utils (isH264Keyframe, extractH264SpsPps, makeH264CodecParams). Incremental keyframe index, forward-path optimization, same-timestamp cache, eviction-resilient live decode. video_stream_demo with live/scrub toggle, 500-frame buffer. 23 tests (5 h264_utils + 18 streaming_video_decoder) |
| B-frame support | `b1cfbd6` | DTS-keyed ObjectStore storage for B-frame videos. Removed drain() from seek path (was O(n²) with B-frame reorder). Negative DTS fix via std::optional sentinels. Startup burst for instant B-frame playback. 6 additional tests |
| MediaSource abstraction | `f7a8a7a` | MediaSource interface (`setTimestamp` + `takeFrame`), ImagePipelineSource (synchronous CodecPipeline + ObjectStore), MediaViewerWidget integration (`setMediaSource`, `setTimestamp`), RGB/RGBA/BGR DecodedFrame upload path, `makeCdrJpegPipeline()` factory. Docs: ARCHITECTURE.md §5 rewritten, PlaybackController replaced, MpvBackend deprecated |
| Demo migrations | `2fc91ea` | Image demos (simulated_stream, multi_channel_viewer, mcap_image_viewer) migrated to ImagePipelineSource + `setMediaSource()`. `expectedBufferSize()` + `isValid()` added to DecodedFrame. Odd-dimension YUV420P buffer overflow fixed in ffmpeg_decoder, thumbnail_cache, media_viewer_widget. 18 decoded_frame tests |
| TDD review fixes | `a182b2b` | avFrameToDecodedFrame returns `Expected<DecodedFrame>` (C2). ThumbnailCache buildThread checks FFmpeg return values (C4). ThumbnailCache reopen clears stale frames (H1). EAGAIN retry in FfmpegDecoder (H2). StreamingVideoDecoder returns error on evicted mid-GOP entries (H3). ffmpeg_decoder_test updated to expect YUV420P. 6 thumbnail_cache tests |
| Medium review fixes | `18bead3` | DepthToGrayscale + SegmentationPalette validate buffer size and format. ImagePipelineSource deduplicates same-timestamp requests. FfmpegDecoder header doc corrected to YUV420P |
| Final review fixes | `699736b` | Widget UV plane sizing fixed to `(w+1)/2`. BGR/BGRA→RGBA channel swap. MCAP test dangling iterator fix (`schemas()` returns by value) |
| ASAN infrastructure | `f616213` | Skip RTLD_DEEPBIND under ASAN via `PJ_ASAN_ACTIVE` define. 10 plugin tests fixed. ASAN results: 57/58 (was 45/58) |
| Docs update | `6a26139` | PLAN.md milestones + roadmap updated. ARCHITECTURE.md FileVideoSource/StreamingVideoSource marked planned. TECHNICAL_NOTES.md §11 Lessons Learned |
| FileVideoSource + StreamingVideoSource | `e2db9b5` | FileVideoSource wraps FfmpegBackend (4 tests). StreamingVideoSource wraps StreamingVideoDecoder + worker thread (3 tests). mp4_video_viewer + video_stream_demo migrated. All 5 demos use MediaSource uniformly |
| LSAN suppression | `461cbd0` | Suppress VAAPI driver av_malloc leak in ffmpeg_decoder_test. 60/60 ASAN, 60/60 release |

### Known limitations

- **4K backward scrub density**: still limited by GOP size. With typical 2-second GOPs, backward scrub at 4K shows cached thumbnails (1920px) between keyframes. Acceptable for interactive use.
- **H.264 only** in StreamingVideoDecoder. H.265 and AV1 keyframe detection not yet implemented (FfmpegDecoder can decode anything FFmpeg supports, but NAL utils are H.264-specific).

### Remaining roadmap (prioritized for PlotJuggler integration)

**Next:**

1. **WebRTC live streaming demo** — end-to-end live video from a local
   webcam via WebRTC, displayed in MediaViewerWidget.

   **Phase A — Webcam capture DataSource (no WebRTC yet):**
   A demo that captures from the local webcam (`/dev/video0` on Linux)
   via FFmpeg's `avdevice` (v4l2 input), encodes H.264 on-the-fly,
   pushes annex-B NAL units into ObjectStore at real-time rate, and
   displays via `StreamingVideoSource` + `MediaViewerWidget`. This
   validates the live streaming pipeline end-to-end without network
   complexity. Retention buffer enables scrub-back on paused stream.

   **Phase B — WebRTC/WHEP receiver:**
   A WHEP client DataSource (using `libdatachannel`) that connects to
   a WebRTC endpoint, depacketizes H.264 RTP → annex-B, and pushes
   into ObjectStore. The display side is identical to Phase A. The
   webcam is served by an external WHEP sender (e.g., GStreamer
   `webrtcsink` or a simple test server).

   **Phase C — Integrated WebRTC demo:**
   Single demo binary that captures webcam (Phase A), optionally
   serves it via WebRTC, and displays locally. Can also connect to
   a remote WHEP endpoint.

   Demo: `webcam_viewer` — opens local camera, shows live feed with
   pause/scrub into retained buffer.

**High priority:**

2. **pj_plugins integration** — wire ObjectStore write host to DataSource
   plugins for MCAP/ROS2 video and image ingest.

**Medium priority:**

3. **MediaIndexRegistry** — centralized keyframe index for file-backed
   CompressedVideo topics (C ABI `publish_keyframe_index`).

**Low priority (deferred):**

4. Compositor (multi-layer overlay) — deferred until annotation test data.
5. SceneDecoder (CDR/Protobuf annotations) — deferred.
6. H.265/AV1 NAL utils — extend when needed.

**Removed:**

- **MpvBackend / VideoViewerWidget / pj_media_qt_video** — removed.
  FfmpegBackend + FileVideoSource is the only video path.

### Design note: MediaSource replaces PlaybackController

The original plan called for a monolithic `PlaybackController` per widget
that would own decoders, worker threads, FrameSlot, compositor, and
CancelToken management. This conflicted with `FfmpegBackend`, which is
already a self-contained subsystem (owns its own thread, seek throttle,
thumbnail cache, cancellation). Wrapping it in PlaybackController would
have meant two layers of threading and cancellation logic.

Instead, `MediaSource` is a thin adapter interface matching how
PlotJuggler's main thread drives widgets: the application calls
`widget->setTimestamp(ts)`, and the widget polls `source->takeFrame()`
at render rate. Each `MediaSource` implementation manages its own
threading and cancellation internally. No forced uniformity.

The `TimelineCursor` subscription model (ARCHITECTURE.md §9 old) is also
superseded — the main thread drives timestamps directly, not via
callbacks.

### Build notes

- `pj_media_qt` requires Qt 6.8+ (gracefully skips if not found).
  Pass `-DQt6_DIR=/path/to/Qt/6.8.3/gcc_64/lib/cmake/Qt6` to cmake.
- Shaders are pre-compiled `.qsb` files (no ShaderTools build dep).
  Re-run `qsb --glsl 440 --hlsl 50 --msl 12` if shaders change.
- `dialog_engine_test` works with Qt 6.8 but may not build during
  partial builds. Run full `cmake --build` to ensure all targets.
- **MpvBackend has been removed.** FfmpegBackend + FileVideoSource is
  the only video path.

## Test data

All in `pj_media/testdata/`:

| File | Size | Content |
|------|------|---------|
| `test_images.mcap` | 2.4 MB | Foxglove CompressedImage, 30 Hz, 90 msgs |
| `test_video.mcap` | 1.6 MB | Foxglove CompressedVideo, 30 Hz, 150 msgs |
| `test_480p.mp4` | 1.6 MB | H.264 480p |
| `test_1080p.mp4` | 7.8 MB | H.264 1080p |
| `test_1920_bframes.mp4` | ~10 MB | H.264 1920p with B-frames (reorder buffer) |
| `test_4k.mp4` | ~25 MB | H.264 4K (3840x2160) |
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
read path, ImageDecoder, MediaSource, QRhiWidget.

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

This milestone creates the first `ImagePipelineSource` (synchronous
decode via CodecPipeline) and `MediaViewerWidget` (QRhiWidget with
BT.709 YUV→RGB shader and RGB DecodedFrame upload path).

**Files**:
- `pj_media/pj_media_core/include/pj_media_core/media_source.h`
- `pj_media/pj_media_core/include/pj_media_core/image_pipeline_source.h`
- `pj_media/pj_media_core/src/image_pipeline_source.cpp`
- `pj_media/pj_media_qt/include/pj_media_qt/media_viewer_widget.h`
- `pj_media/pj_media_qt/src/media_viewer_widget.cpp`
- `pj_media/pj_media_qt/CMakeLists.txt`
- `pj_media/demos/mcap_image_viewer.cpp` (demo binary)
- `pj_media/pj_media_core/tests/decoded_frame_test.cpp`

**Demo**: `mcap_image_viewer <test_images.mcap>` — opens the file,
shows images, slider scrubs through the timeline.

**Tests**:
- ImagePipelineSource: construct with ObjectStore + topic + pipeline,
  setTimestamp → takeFrame returns decoded image frame. No Qt.
- expectedBufferSize: covers all PixelFormats including odd-dimension
  YUV420P. isValid checks size/format/dimension consistency.
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
- Concurrent reader (MediaSource polling `latestAt`) during push →
  ASAN clean, no stale frames

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
| turbojpeg | system pkg or conan | pj_media_core (ImageDecoder, ThumbnailCache) |
| ~~libmpv~~ | ~~system pkg~~ | ~~removed~~ |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | system pkg | pj_media_core (FfmpegDecoder, FfmpegBackend) |

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
