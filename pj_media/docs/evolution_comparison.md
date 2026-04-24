# pj_media Evolution: Experiment → Production

Comparison between the early standalone experiment (`~/ws_plotjuggler/pj_media/`)
and the production module (`plotjuggler_core/pj_media/`).

---

## 1. Scope and Integration

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Repo** | Standalone repository | Submodule of `plotjuggler_core` |
| **ObjectStore** | None — standalone video engine | Core dependency — all media reads from ObjectStore |
| **DataEngine** | None | Dual-store: ObjectStore (media) + DataEngine (scalars) from same source |
| **Plugin system** | None | Designed for pj_plugins DataSource/MessageParser integration |
| **Timeline** | Own `TimelineBridge` with bidirectional sync | Main thread drives widgets directly — no callback subscription model |

The experiment was a **self-contained video engine**. The production version is a
**viewer module** that reads from pj_datastore and is driven by PlotJuggler's main loop.

---

## 2. Architecture

### Experiment: Pipeline + Event-Driven

```
SourceThread → PacketQueue → DecodeThread → decoded_frames_ → DisplayTimer (UI)
                                                                    ↓
                                                              frameReady(signal)
```

- 3 threads: source I/O, decode, UI display
- `PacketQueue` (bounded, blocking) decouples I/O from decode
- `FrameBuffer` (memory+time budgeted ring) for live rewind
- `PlaybackController` is a monolithic state machine (play/pause/seek/step)
- Qt signals/slots for frame delivery (`frameReady(QVideoFrame)`)
- `TimelineBridge` for bidirectional sync with external timeline

### Production: MediaSource + Pull-Based

```
Main thread:
  widget->setTimestamp(ts)
    → source->setTimestamp(ts)   [sync or post to worker]
  widget->render()
    → source->takeFrame()        [returns latest decoded frame]
    → GPU upload + draw
```

- **No PacketQueue** — ObjectStore IS the packet store
- **No FrameBuffer** — ObjectStore retention budget replaces it
- **No PlaybackController** — MediaSource is a thin adapter, each impl manages its own threading
- **No Qt signals for frame delivery** — pull-based `takeFrame()` polling
- **No TimelineBridge** — main thread drives timestamps directly

### Why the change

The experiment's `PlaybackController` was a monolithic orchestrator that
conflicted with `FfmpegBackend`'s self-contained threading. The production
version replaced it with `MediaSource` — a thin 2-method interface
(`setTimestamp` + `takeFrame`) where each implementation manages its own
complexity. This eliminated the need for `PacketQueue`, `FrameBuffer`,
and `TimelineBridge` as separate components.

The shift from push-based (signals) to pull-based (polling) frame
delivery was driven by the `video_player_lab` finding that Qt's event
queue causes stale-frame interleaving during rapid scrub. The FrameSlot
latest-wins mailbox pattern solved this definitively.

---

## 3. Video Sources

| Source | Experiment | Production |
|--------|-----------|------------|
| **Local files (MP4/MKV)** | `FFmpegVideoSource` (unified class) | `FfmpegBackend` → `FileVideoSource` |
| **MCAP images** | `McapVideoSource` (lazy packet reads) | ObjectStore `pushLazy` + `ImagePipelineSource` |
| **MCAP video** | `McapVideoSource` (NAL parsing) | ObjectStore + `StreamingVideoDecoder` → `StreamingVideoSource` |
| **SRT streaming** | `FFmpegVideoSource` (via FFmpeg protocols) | Not yet — would be a DataSource plugin |
| **RTSP** | `FFmpegVideoSource` | Not yet — same |
| **WebRTC/WHEP** | `WebRtcVideoSource` (libdatachannel) | Not yet — same |
| **HLS/DASH** | `FFmpegVideoSource` | Not yet — same |

The experiment's `FFmpegVideoSource` handled everything (files + all
streaming protocols) in one class. Production splits the concern:
**DataSource plugins** handle transport/ingest into ObjectStore, and
**MediaSource** implementations handle decode/display from ObjectStore.
File-based video bypasses ObjectStore entirely via `FfmpegBackend`.

The experiment had **3 source implementations** (FFmpeg, MCAP, WebRTC).
Production has **3 MediaSource implementations** (ImagePipeline, FileVideo,
StreamingVideo) plus the DataSource plugin pattern for future ingest.

---

## 4. Video Decoding

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Decoder class** | `VideoDecoder` — RAII AVFrame wrapper | `FfmpegDecoder` — outputs YUV420P contiguous buffer |
| **Frame type** | `DecodedFrame` wrapping `AVFrame*` (ref-counted) | `DecodedFrame` struct with `shared_ptr<vector<uint8_t>>` |
| **HW accel** | VAAPI → CUDA → D3D11VA → VT → software | VAAPI → CUDA → software |
| **Pixel format** | Preserves source format (many) | Always YUV420P (GPU shader handles conversion) |
| **Zero-copy GPU** | Optional via `setKeepHWFrames(true)` | Not yet (CPU upload via QRhi) |
| **Scrub optimization** | Basic seek + decode forward | Forward threshold, decodeSkip, direction-aware partials, ThumbnailCache |
| **B-frame support** | Basic | Full (DTS-keyed storage, reorder buffer burst, PTS from AVFrame) |

The experiment kept frames as `AVFrame*` wrappers — flexible but leaks
FFmpeg types into the public API. Production copies YUV planes into a
plain `shared_ptr<vector<uint8_t>>` — simpler ownership, no FFmpeg
dependency in consumers.

Production's scrub engine is significantly more sophisticated:
`FfmpegBackend` has forward threshold (100 frames), `decodeSkip` for
intermediate frames, direction-aware partial suppression, target
refinement within GOP, and `ThumbnailCache` for instant backward
feedback. The experiment had none of these.

---

## 5. Timestamps

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Internal unit** | **Microseconds** (`int64_t`) | **Nanoseconds** (`int64_t`) |
| **Type alias** | `Timestamp = int64_t` (µs) | `PJ::Timestamp = int64_t` (ns, from pj_base) |
| **Constants** | `kNoTimestamp = INT64_MIN` | `INT64_MIN` used ad-hoc |
| **Conversion helpers** | `usFromSeconds()`, `secondsFromUs()` | Manual `/ 1'000'000'000.0` |
| **VideoBackend** | Not applicable | `double seconds` (matches FFmpeg convention) |

The experiment chose microseconds for sub-frame precision with
manageable numbers. Production uses nanoseconds to match pj_datastore's
`Timestamp` type (ROS convention). This created a dual-unit API where
`MediaSource` speaks nanoseconds and `VideoBackend` speaks seconds.

---

## 6. Frame Delivery

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Mechanism** | Qt signal `frameReady(QVideoFrame&)` | Pull-based `MediaSource::takeFrame()` |
| **Stale frame handling** | Not addressed (event queue can pile up) | FrameSlot latest-wins (structurally impossible to show stale frames) |
| **Live vs scrub** | Different timer behavior | Same `setTimestamp + takeFrame` interface for both |
| **Multi-camera** | Not addressed | Each widget owns its own MediaSource independently |

The experiment used Qt signals, which the `video_player_lab` proved
causes stale-frame interleaving during rapid scrub. Production's pull
model eliminates this structurally.

---

## 7. Image Handling

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **JPEG decode** | Via FFmpeg MJPEG decoder | Via turbojpeg (faster, lighter) |
| **PNG decode** | Via FFmpeg PNG decoder | Via libpng |
| **Pipeline** | Part of VideoSource/VideoDecoder | Separate `CodecPipeline` with composable stages |
| **CDR stripping** | In McapVideoSource | `CdrImageStripper` codec stage |
| **Depth/segmentation** | Not addressed | `DepthToGrayscale`, `SegmentationPalette` stages |

Production's `CodecPipeline` is more flexible — stages compose
arbitrarily (CDR strip → JPEG decode, or depth strip → PNG decode →
grayscale colormap). The experiment used FFmpeg for everything, which
is heavier for simple JPEG/PNG.

---

## 8. Widget Rendering

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Primary** | `RhiVideoWidget` (QRhiWidget) | `MediaViewerWidget` (QRhiWidget) |
| **Fallback** | `FFmpegVideoBuffer` (QAbstractVideoBuffer) | QImage path |
| **Shader** | BT.709 YUV→RGB fragment shader | Same BT.709 YUV→RGB fragment shader |
| **Zoom/pan** | View transform matrix in vertex shader | Same |
| **Pixel formats** | 11 mappings (YUV420P, NV12, P010, etc.) | YUV420P + RGBA (simpler, decoder normalizes) |
| **Frame input** | `setFrame(DecodedFrame)` via signal | `setFrame(DecodedFrame)` direct or `MediaSource::takeFrame()` poll |

Nearly identical GPU rendering. The key difference is that production
normalizes everything to YUV420P before the widget, so the widget only
needs 2 paths (YUV + RGB). The experiment passed through many pixel
formats, requiring 11 mappings.

---

## 9. Testing

| Aspect | Experiment | Production |
|--------|-----------|------------|
| **Test count** | 15 test executables | 14 test files, 60+ test cases |
| **UI tests** | Widgeteer UI automation | None (headless only) |
| **Live stream tests** | SRT sender/receiver, WebRTC/WHEP | Simulated stream (push to ObjectStore) |
| **Performance** | `test_benchmark` | Integration tests with timing assertions |
| **ASAN** | Unknown | Full ASAN coverage (60/60 pass) |

---

## 10. What Was Carried Forward

| Component | From Experiment | In Production | Notes |
|-----------|----------------|---------------|-------|
| QRhiWidget + BT.709 shader | `RhiVideoWidget` | `MediaViewerWidget` | Nearly identical shader code |
| FFmpeg HW-accel fallback chain | `VideoDecoder` | `FfmpegDecoder` | Same probe pattern |
| H.264 NAL keyframe detection | `NalParser` | `h264_utils.h` | Simplified (H.264 only, no H.265 yet) |
| Zoom/pan via vertex shader | `RhiVideoWidget` | `MediaViewerWidget` | Same transform math |
| MCAP lazy reading pattern | `McapVideoSource` | ObjectStore `pushLazy` closures | Same concept, different mechanism |

## 11. What Was Dropped

| Component | Why |
|-----------|-----|
| `PlaybackController` state machine | Replaced by MediaSource thin adapters |
| `PacketQueue` (bounded blocking queue) | ObjectStore replaces it |
| `FrameBuffer` (memory+time budgeted ring) | ObjectStore retention budget replaces it |
| `TimelineBridge` (bidirectional sync) | Main thread drives directly |
| Qt signal frame delivery | Causes stale frames during scrub (proven in video_player_lab) |
| `FFmpegVideoBuffer` (QAbstractVideoBuffer) | QRhi-only path sufficient |
| WebRTC/WHEP source | Deferred to DataSource plugin |
| SRT/RTSP/HLS source | Deferred to DataSource plugin |
| `SourceFactory` (URI dispatch) | Not needed — DataSource plugins handle transport |
| Microsecond timestamps | Nanoseconds to match pj_datastore |

## 12. What Was Added (Not in Experiment)

| Component | Why |
|-----------|-----|
| `MediaSource` interface | Uniform pull-based frame delivery |
| `ImagePipelineSource` | Image-specific path (no video decoder overhead) |
| `FileVideoSource` / `StreamingVideoSource` | MediaSource adapters for video |
| `CodecPipeline` / `CodecStage` | Composable image decode stages |
| `ThumbnailCache` | Instant backward scrub feedback |
| `expectedBufferSize()` / `isValid()` | Centralized YUV buffer sizing |
| Direction-aware scrub | Suppress backward partials, forward threshold |
| B-frame support | DTS-keyed storage, reorder buffer burst |
| `CdrImageStripper` / `DepthToGrayscale` / `SegmentationPalette` | ROS2 data support |
| ObjectStore integration | Dual-store model (media + scalars) |

---

## 13. Summary

The experiment was a **standalone video engine** with a traditional
pipeline architecture (source → queue → decode → buffer → display).
It was feature-rich (SRT, WebRTC, HLS) but monolithic — the
`PlaybackController` state machine and Qt signal delivery created
complexity that didn't fit PlotJuggler's main-thread-driven model.

The production version is a **viewer module** integrated with
pj_datastore. It replaced the pipeline with ObjectStore (storage) +
MediaSource (frame delivery) — a simpler architecture where each
component manages its own threading. The scrub engine is significantly
more sophisticated (forward threshold, decodeSkip, ThumbnailCache,
direction-aware partials), and the frame delivery model (pull-based
FrameSlot) eliminates the stale-frame problem that plagued the
signal-based approach.

The streaming protocol support (SRT, WebRTC, RTSP) was deferred —
it will return as DataSource plugins that push into ObjectStore,
rather than as VideoSource implementations that bypass the storage layer.
