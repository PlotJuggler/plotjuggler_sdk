# pj_media Technical Notes

Implementation-relevant technical knowledge for the pj_media module. This is a
knowledge base, not an architecture document. Architecture decisions will be
made during design and documented in a future ARCHITECTURE.md.

Sources: the standalone pj_media experiment (`~/ws_plotjuggler/pj_media/`) and
design discussions for the integrated module.

---

## 1. Qt 6.8 Video Rendering

### QMediaPlayer Limitations

QMediaPlayer is unsuitable for PlotJuggler's needs:

| Limitation | Detail |
|---|---|
| No frame-accurate seeking | Lands on nearest keyframe only |
| No frame stepping | No `stepForward()`/`stepBackward()` API |
| No low-latency streaming | RTSP has 2-3s latency; no native SRT |
| No external clock sync | Cannot tie playback to an external timeline |

A custom decode pipeline is required.

### QAbstractVideoBuffer (Public Again in 6.8)

Qt 6.8 made `QAbstractVideoBuffer` public (it was private in 6.0-6.7). This is
the bridge between a custom decode pipeline and Qt's rendering:

- Three virtuals: `format()`, `map()`, `unmap()`
- `QVideoFrame(std::unique_ptr<QAbstractVideoBuffer>)` takes ownership
- CPU-accessible buffers only via the public API
- True GPU texture passthrough requires `QHwVideoBuffer` (private/internal)

### QRhiWidget — Primary Render Path

`QRhiWidget` with custom shaders is the primary render path:

- QRhi abstracts over Vulkan, Metal, D3D11, OpenGL
- **YUV420P texture support**: 3 R8 textures (Y full-res, U half-res, V half-res)
  uploaded per frame. BT.709 YUV->RGB conversion in fragment shader. 75% less
  GPU memory than a single RGBA8 texture (1.5 bytes/pixel vs 4 bytes/pixel).
- Backward-compatible QImage (RGB) path kept for image viewers
- Zoom (mouse wheel) and pan (mouse drag) via view transform matrix in
  vertex shader — zero CPU-side pixel processing
- Optional zero-copy: import HW-decoded GPU surfaces directly as QRhi
  textures via `QRhiTexture::createFrom({nativeHandle, 0})`

### QRhiWidget Multi-Instance Lifecycle (Qt 6.8)

Qt 6.8 `QRhiWidget` has a critical initialization requirement: the
top-level window's backing store must be RHI-capable from its first
`show()`. If no `QRhiWidget` exists as a child when the window is
first shown, Qt creates a regular (non-RHI) backing store. Any
`QRhiWidget` added dynamically after that point will **never** get an
RHI instance — `rhi()` returns null permanently, producing continuous
"QRhiWidget: No QRhi" errors.

**Workaround**: add a zero-size bootstrap `QRhiWidget` in the window's
constructor, before `show()`:

```cpp
// Forces Qt to create an RHI-backed backing store for this window.
auto* bootstrap = new MediaViewerWidget(parent);
bootstrap->setMaximumSize(0, 0);
layout->addWidget(bootstrap);
```

Dynamically added `QRhiWidget` instances will then initialize
successfully.

**Additional lifecycle rules**:

- Override `releaseResources()` (Qt virtual) to delete all QRhi
  resources (pipeline, textures, buffers). Qt calls this when the
  underlying QRhi instance changes (reparenting, window move).
- Track `QRhi* rhi_cached_` in `initialize()`. If `rhi() != cached`,
  call `releaseResources()` and reinitialize — resources from the old
  QRhi are invalid.
- In `setFrame()` (or any method that calls `update()`), guard with
  `if (pipeline_ != nullptr)` — calling `update()` before init floods
  Qt with render requests that can't be fulfilled and starves the
  initialization path.
- At the end of `initialize()`, call `update()` if a frame is pending
  — otherwise the first frame set before init completes is lost.

### QVideoSink Threading

- `QVideoSink::setVideoFrame()` is thread-safe (internal `QMutex`)
- Each call replaces the previous frame (no buffering/queue)
- No built-in frame pacing — implement PTS-based timing externally
- Decode on worker thread, push to UI thread via `setVideoFrame()`

### Pixel Format Mapping

Key `AVPixelFormat` to Qt mappings (Qt handles YUV-to-RGB conversion
automatically in the renderer):

| FFmpeg `AVPixelFormat` | Qt `QVideoFrameFormat::PixelFormat` |
|---|---|
| `AV_PIX_FMT_NV12` | `Format_NV12` |
| `AV_PIX_FMT_YUV420P` | `Format_YUV420P` |
| `AV_PIX_FMT_YUV422P` | `Format_YUV422P` |
| `AV_PIX_FMT_P010` | `Format_P010` |
| `AV_PIX_FMT_RGBA` | `Format_RGBA8888` |
| `AV_PIX_FMT_BGRA` | `Format_BGRA8888` |

Keep frames in YUV420P (1.5 bytes/pixel) rather than RGBA (4 bytes/pixel) —
Qt handles conversion in the renderer.

---

## 2. Codec Details

### Support Matrix

| Codec | Latency | Compression vs H.264 | HW Decode Support | Status |
|---|---|---|---|---|
| H.264/AVC | Lowest | Baseline | Universal | Primary target |
| H.265/HEVC | Low | ~40% better | Wide (GPU 2018+) | Supported |
| AV1 | Medium | ~50% better | Growing (RTX 40+, RDNA 3+) | Supported |
| VP9 | Medium | ~35% better | Moderate | Supported |

VVC/H.266 has near-zero hardware support — not targeted.

### B-Frame Support

While Foxglove and Rerun conventions recommend no B-frames, the
FfmpegBackend fully supports B-frame streams:

- **PTS from AVFrame**: uses `AVFrame::pts` (presentation order) not
  packet DTS (decode order). This is critical for correct timestamp
  reporting with B-frame reordering.
- Test coverage: `test_1920_bframes.mp4` exercises B-frame handling
  in both forward and backward scrub.
- For MCAP-stored video, I+P only (no B-frames) is still recommended
  for simpler seeking.

### Annex B NAL Units

The wire format for H.264/H.265 streaming and MCAP storage:

- Start codes: `00 00 00 01` prefix before each NAL unit
- H.264 keyframes: SPS + PPS NAL units before IDR slice (NAL type 5)
- H.265 keyframes: VPS + SPS + PPS NAL units before IRAP slice
  (NAL types 16-21)
- AV1: Sequence Header OBU before KEY_FRAME OBU (low overhead bitstream)
- Each message contains exactly enough data to decode one frame

### NAL Parser for Keyframe Detection

Required for building keyframe indices from raw bitstreams (MCAP
CompressedVideo, live streams):

- H.264: IDR detection via NAL unit type 5
- H.265: IRAP detection via NAL unit types 16-21
- Parse first byte after start code to extract NAL type
- No need to parse slice headers or reference lists — only
  keyframe/non-keyframe classification is needed

---

## 3. Hardware Acceleration

### Runtime Detection

- Detect available HW backends at runtime, not compile time
- No compile-time flags or conditional compilation for HW accel
- Probe available backends via the decode library's hardware device API

### Fallback Chains

| Platform | Chain |
|---|---|
| Linux | VAAPI → CUDA/NVDEC → software |
| Windows | D3D11VA → software |
| macOS | VideoToolbox → software |

### GPU Zero-Copy Path

The ideal path avoids GPU-to-CPU-to-GPU round-trips:

1. HW decoder produces frames on GPU (VAAPI surface, D3D11 texture, etc.)
2. Export as DMA-BUF (Linux) or shared texture handle (Windows/macOS)
3. Import into QRhi as `QRhiTexture` via `createFrom()`
4. Render directly — no pixel copy

Fallback: transfer GPU frames to CPU (~0.5-2ms per 1080p frame), then upload
to QRhi texture. This is acceptable for typical robotics resolutions.

---

## 4. Seeking Strategy

### File-Based Seeking

1. Build keyframe index at file open time:
   - MP4: parse `stss` (Sync Sample) atom
   - MCAP: read Summary section at EOF for O(1) access to the full message
     index; classify keyframes via NAL type inspection
   - Store as sorted vector of `{timestamp, byte_offset}`
2. Seek to nearest preceding keyframe
3. Flush decoder state (mandatory after every seek)
4. Decode forward from keyframe to target frame, discarding intermediate
   frames
5. Return the target frame

### FfmpegBackend Scrub Optimizations

The FfmpegBackend implements several optimizations beyond the basic
seek algorithm:

- **Forward threshold** (`kForwardThreshold=100`): when the target is
  within 100 frames of the current decode position, the backend
  continues decoding forward instead of seeking. This avoids the
  costly seek+flush+decode-from-keyframe cycle for small forward
  jumps during interactive scrub.

- **decodeSkip**: intermediate frames between the keyframe and the
  target are decoded without HW transfer or sws_scale. Only the
  final target frame gets full decode with pixel conversion. This
  reduces per-frame cost during forward decode significantly.

- **Min decode time** (60ms): after starting a decode, the backend
  will not check for cancellation until at least 60ms have elapsed.
  This prevents thrashing on fast scrub where every frame would be
  cancelled before producing a result.

- **Seek throttle** (33ms / 30 Hz): seek requests are rate-limited
  with duplicate elimination. Multiple seek requests within the
  throttle window are collapsed to the most recent target.

- **B-frame PTS fix**: uses `AVFrame::pts` (presentation timestamp
  from the decoder output) instead of the packet PTS. Packet PTS
  is decode-order for B-frame streams, which causes incorrect
  timestamp reporting.

- **Direction-aware partial filter**: the `scrub_backward_` flag
  suppresses partial publications during backward scrub. Forward
  scrub publishes partials for smooth feedback. During backward
  scrub, only the keyframe (instant feedback) and the final target
  (completion) are published.

- **Target refinement**: for backward scrub within the same GOP,
  the keyframe is published immediately for instant backward visual
  feedback, then intermediate frames are decoded via decodeSkip,
  and the exact target replaces the keyframe. This eliminates
  visible forward jumps.

- **processEvents() delivery**: frame delivery uses
  `QCoreApplication::processEvents()` instead of Qt event queue
  signals. This ensures frames are delivered synchronously without
  event queue latency.

- **CancelToken integration**: the CancelToken is wired through to
  the decoder layer, allowing cooperative cancellation at natural
  checkpoints (between `avcodec_receive_frame` calls).

### ThumbnailCache

The ThumbnailCache provides instant backward scrub feedback by
pre-decoding frames at file open time:

- **Background pre-decode**: a dedicated thread decodes 1 frame per
  second of video duration at open time. For a 60-second video, this
  produces ~60 cached thumbnails.

- **Auto-scale**: frames wider than 1920px (e.g., 4K) are scaled to
  1920px width before caching. This bounds memory usage while keeping
  sufficient quality for scrub preview.

- **JPEG compression**: cached frames are stored as JPEG at quality
  85. Typical sizes: ~90KB/frame for 1080p, ~133KB/frame for
  4K-scaled-to-1920. A 60-second 1080p video uses ~5.4MB of cache.

- **YUV420P throughout**: JPEG stores in YUV natively. Decompression
  outputs YUV420P directly (via `tjDecompressToYUV2`). The same
  BT.709 shader renders both cached and live frames, eliminating
  color mismatches between the two paths.

- **Usage pattern**: during backward scrub, the FfmpegBackend first
  checks the ThumbnailCache for a frame near the target timestamp.
  If found, it is delivered immediately as the "keyframe" feedback,
  then the full-resolution decode replaces it.

### GOP-Aware Buffer Eviction

For streaming buffers holding compressed video:

- Track GOP boundaries (keyframe positions) in the buffer
- Evict entire GOPs as a unit — never remove a keyframe while its
  dependent P-frames remain
- Eviction order: oldest GOP first
- When evicting, update the seekable time range reported to the viewer

### Memory Budget Reference

| Resolution | GOP Size (frames) | Format | Single GOP Decoded |
|---|---|---|---|
| 480p (640x480) | 30 | YUV420P | ~14 MB |
| 1080p (1920x1080) | 30 | YUV420P | ~93 MB |
| 4K (3840x2160) | 30 | YUV420P | ~372 MB |

These are decoded frame sizes. Compressed data in the buffer is typically
10-50x smaller. The LRU cache of decoded frames is what drives memory
consumption.

---

## 5. Implementation Insights

Lessons from the standalone pj_media experiment and the mcap_player prototype.

### Timestamp Unit Conversion

FFmpeg internally uses stream `time_base` (e.g., 1/90000 for MPEG-TS,
1/1000 for MKV), not nanoseconds. pj_media uses nanoseconds consistently
(matching pj_datastore). Conversion must happen at the codec interface using
`av_rescale_q(pts, stream_time_base, {1, 1'000'000'000})` for numerically
accurate results. Avoid manual multiplication — it loses precision for
non-power-of-two time bases.

### Seeking Requires Decoding Forward

After seeking to the nearest keyframe before the target timestamp, the
decoder must decode forward — discarding all intermediate frames — until it
reaches the target. For a GOP of 30 frames this means up to 29 wasted
decodes. For long GOPs (250+ frames) this can take hundreds of milliseconds.

This is why short GOPs matter for interactive scrubbing. Foxglove recommends
keyframes every ~1 second. LeRobot uses GOP=2 (keyframe every 2 frames),
achieving near-random-access with significant compression.

### Clock-Based Playback Scheduling

For smooth file playback, the display timer maintains a playback clock:

- On play: record `wall_start = steady_clock::now()` and `pts_start`
- Each tick: `expected_pts = pts_start + (now - wall_start)`
- Present the frame whose PTS is nearest-before `expected_pts`

Live mode skips the clock entirely — always show the latest frame, drop
older ones. This is the fundamental behavioral split between file and
streaming playback.

### StreamingVideoDecoder: Lessons Learned

**Keyframe index must track by timestamp, not count.** During steady-state
streaming with retention, `ObjectStore::entryCount()` stays constant
(push+evict). A count-based scan cursor (`if (count > last_scanned) scan`)
silently stops indexing new keyframes. Track by timestamp instead:
`indexAt(last_scanned_ts_) + 1` finds new entries regardless of count.

**Same-timestamp caching is critical.** When the display polls at 60 Hz
but frames arrive at 30 Hz, every other `decodeAt()` call requests the
same timestamp. Without caching, this triggers a full keyframe→target
seek+decode. For 1920p video with large GOPs, this causes visible
stuttering. Solution: cache `last_frame_` and return it immediately when
`target_ts == last_decoded_ts_`.

**Forward decode must survive keyframe eviction.** In live mode, the
original keyframe gets evicted while the decoder continues forward.
The decoder state is still valid — it just needs new packets. The forward
path must not require the keyframe index (`keyframe_timestamps_` may be
empty) or the original keyframe entry (may be evicted). Only backward
seeks require a keyframe still in the store.

**SPS/PPS extradata required for VAAPI.** Opening `FfmpegDecoder` with
just `codec_id = AV_CODEC_ID_H264` and no `extradata` causes VAAPI
"Failed to sync surface" errors. The fix: extract SPS+PPS NAL units
from the first keyframe and set them as `AVCodecParameters::extradata`
via `extractH264SpsPps()`. This lets VAAPI properly size its surface pool.

**Never drain() during live streaming startup.** FFmpeg's `avcodec_send_packet(nullptr)`
signals EOF and puts the codec in drain state. Subsequent `avcodec_send_packet` calls
return `AVERROR_EOF` until `avcodec_flush_buffers` is called. With B-frames, the
decoder returns EAGAIN for the first ~30 packets (reorder buffer filling). If you
drain after each failed decode, you poison the codec state, forcing a full
flush+re-decode-from-keyframe on every call — O(n^2) total. Fix: treat EAGAIN as
normal, update `last_decoded_ts_` to track position, and let the forward path feed
more packets on subsequent calls.

**DTS-keyed ObjectStore storage for B-frame videos.** When ingesting from MP4
containers with B-frames, use DTS (always monotonic) as the ObjectStore
timestamp, not PTS (non-monotonic). FFmpeg's decoder expects packets in decode
order and handles reordering internally — `AVFrame::pts` on the output gives
the correct presentation timestamp. For production streaming (per VideoFrame
spec), B-frames are disallowed, so PTS == DTS and this is a non-issue.

**ObjectStore indices shift after eviction.** A cached `last_decoded_index_`
becomes stale after eviction removes entries from the front. Always
re-derive the index via `indexAt(last_decoded_ts_)` instead of caching it.

### Zoom and Pan via GPU Transform

With QRhiWidget rendering, zoom and pan require only a view transform matrix
in the vertex shader (scale + translate). No pixel reprocessing.

Cursor-anchored zoom (keeps the point under the cursor fixed):
```
pan += cursor_pos * (1/new_zoom - 1/old_zoom)
```

When zoom <= 1.0, reset pan to zero (video fits entirely in widget).

---

## 6. Streaming Protocols

### Latency Characteristics

| Protocol | Typical Latency | Notes |
|---|---|---|
| Raw TCP/UDP | <50ms | No error recovery |
| SRT | 120-500ms (tunable) | Best balance for robotics |
| WebRTC/WHEP | 200-500ms | Complex setup, feature-rich |
| RTSP | 1-3s | Surveillance cameras, widely supported |
| RTMP | 1-3s | Legacy ingest |
| HLS/DASH | 2-6s | Too high for interactive robotics |

### SRT as Primary Live Protocol

SRT (Secure Reliable Transport) offers the best balance for robot-to-desktop:

- Latency tunable via `SRTO_LATENCY` socket option
- ARQ retransmission for reliability over lossy networks
- Payload: raw H.264 Annex B NAL units
- Available on Conan (`srt/1.5.4`)

### ROS 2 Image Topics

`sensor_msgs/Image` and `sensor_msgs/CompressedImage` arrive as individual
messages, each independently displayable. No inter-frame dependencies.
Timestamps are in the message header (nanoseconds).

`foxglove.CompressedVideo` messages in MCAP carry one video frame each with
nanosecond timestamps, using Annex B encoding.

---

## 7. Lazy Handle Implementations

How the lazy handle model (REQUIREMENTS.md section 4.2) maps to concrete
backends:

### MCAP Handle

- Stored: file handle + message index entry (chunk offset + message offset)
- Resolve: seek to chunk, decompress if needed, read message bytes, parse
  according to schema (Image or VideoFrame)
- For VideoFrame: find nearest preceding keyframe in the index, decode
  forward
- MCAP Summary section provides O(1) access to the full index without
  scanning the data section

### LeRobot MP4 Handle

- Stored: MP4 file path + presentation timestamp (PTS)
- Resolve: open MP4, seek to nearest keyframe before PTS, decode forward to
  target PTS
- Timestamp mapping: Parquet `timestamp` column provides nanosecond
  timestamps; PTS provides the seek target within the MP4
- One MP4 file per camera per episode (v2) or per chunk (v3)

### RLDS TFRecord Handle

- Stored: TFRecord shard file path + record byte offset
- Resolve: seek to offset, read record, extract image tensor from
  FeaturesDict
- Images are stored as raw tensors `(H, W, 3) uint8` — no video codec
  decoding needed
- No inter-frame dependencies; every record is self-contained

### Live Buffer Handle

- Stored: buffer pointer + byte range within the ring buffer
- Resolve: read compressed bytes from the range, decode
- For video: the byte range covers a single encoded frame; the decoder must
  have been initialized from the preceding keyframe
- Handle is valid only while the buffer has not evicted the referenced data;
  the frame store must handle invalidation gracefully

---

## 8. Reference Documents

- [dataset_format_comparison.md](dataset_format_comparison.md) — detailed
  comparison of MCAP, RLDS, LeRobot, and Zarr formats covering data models,
  timestamps, image/video storage, I/O, and ecosystem
- [datatypes_2D.md](datatypes_2D.md) — complete type catalog: Image,
  VideoFrame, CameraCalibration, ImageAnnotation, PointCloud, ScenePrimitive,
  Grid, FrameTransform

---

## 9. Resolved Design Questions

These questions were identified early and resolved during architecture design.

### MessageParser Extension vs New Plugin Family

**Resolved:** Keep MessageParser stateless. Parsers are codec-agnostic
envelope peelers (CDR, Protobuf, JSON). All video decoder state lives in
pj_media's decoder classes (`FfmpegDecoder`, `StreamingVideoDecoder`),
never in any plugin. See ARCHITECTURE.md §4 and REQUIREMENTS.md §4.4.

### Mixed Scalar + Media Output from a Single Parse Call

**Resolved:** Two-host `parse()` signature. The parser receives both a
scalar write host (`PJ_parser_write_host_t`) and an object write host
(`PJ_object_write_host_t`). Either may be NULL. This requires parser
ABI v2, which is an outstanding prerequisite (see REQUIREMENTS.md
Prerequisites). V1 uses direct ingest only.

### Metadata Availability: Eager vs Lazy

**Resolved:** Parse the first keyframe eagerly (one parse per channel,
not per frame). `StreamingVideoDecoder` extracts SPS/PPS from the first
keyframe via `extractH264SpsPps()` and initializes the decoder. For
file-backed sources, `FfmpegBackend` gets dimensions from
`AVCodecParameters` at open time. No lazy metadata deferral.

---

## 10. MediaSource Pattern

The `MediaSource` interface is the uniform frame-delivery contract
between decoder backends and `MediaViewerWidget`. It replaces the
originally-planned `PlaybackController` (which would have been a
monolithic orchestrator conflicting with `FfmpegBackend`'s
self-contained threading).

**Interface:**
```cpp
class MediaSource {
 public:
  virtual ~MediaSource() = default;
  virtual void setTimestamp(int64_t ts_ns) = 0;
  virtual std::optional<DecodedFrame> takeFrame() = 0;
};
```

**Key properties:**
- `setTimestamp()` is called by the main thread when the global time
  changes. It may decode synchronously (images) or post to an internal
  worker thread (video).
- `takeFrame()` is called by the main thread at render rate. Returns
  the latest decoded frame, or nullopt if nothing new.
- No `cancel()` in the public interface — each implementation manages
  cancellation internally.
- The widget calls `update()` after `setTimestamp()` to trigger a repaint.

**Three implementations:**
- `ImagePipelineSource` — synchronous decode via CodecPipeline. Done.
- `FileVideoSource` — wraps FfmpegBackend (self-contained threading). Planned.
- `StreamingVideoSource` — wraps StreamingVideoDecoder + worker thread. Planned.

---

## 11. Lessons Learned (Code Review Fixes)

Bugs found during a 4-agent parallel code review (API design, silent
failures, type design, Codex) and fixed via TDD.

**YUV420P chroma plane sizing must use ceiling division.** For
YUV420P, chroma planes are `ceil(w/2) x ceil(h/2)`, not `w/2 x h/2`.
Truncating integer division causes buffer overflow on odd-dimension
video (e.g. 1921x1081). Fixed by adding `expectedBufferSize()` to
`decoded_frame.h` — a single source of truth for all YUV/NV12 buffer
size calculations. Every allocation site must use this function.

**`avFrameToDecodedFrame` must propagate errors, not return null
frames as success.** When HW transfer or sws_getContext fails,
returning `DecodedFrame{}` wraps a null frame in `Expected<T>` as a
"success" — callers have no way to distinguish it from a real frame.
Changed return type to `Expected<DecodedFrame>` with error strings.

**MCAP's `schemas()` and `channels()` return by value.** Calling
`reader.schemas().find(id)` creates an iterator into a temporary map
that is destroyed at the semicolon. Classic dangling iterator UB —
heap-use-after-free under ASAN. Fix: cache the map in a local variable
before iterating.

**ThumbnailCache must clear frames on reopen.** `buildAsync()` called
`stop()` but never cleared `frames_` or `total_bytes_`. Building from
a new file appended thumbnails after the old ones, violating the sort
invariant and returning stale frames from the previous video.

**Codec stages must validate input format and buffer size.** Pipeline
stages like `SegmentationPalette` and `DepthToGrayscale` compute read
lengths from `width*height` but never check `pixels->size()`. With
malformed input, this is an out-of-bounds read.

**RTLD_DEEPBIND is incompatible with AddressSanitizer.** `dlopen` with
`RTLD_DEEPBIND` conflicts with ASAN's runtime interceptors. Fixed by
defining `PJ_ASAN_ACTIVE` when sanitizers are enabled and skipping
the flag in that case.
