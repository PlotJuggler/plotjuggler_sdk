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
- Upload RGBA8 texture per frame, display via fullscreen-triangle shader
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

### No B-Frames Policy

Matches both Foxglove and Rerun conventions:

- Only I-frames (keyframes) and P-frames (delta frames)
- Presentation order equals decode order — no reorder buffer needed
- No separate DTS/PTS tracking required
- Simplifies seeking: decode forward from keyframe without sorting

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

## 9. Open Design Questions

These questions are identified but intentionally unresolved. They will be
addressed during architecture design.

### MessageParser Extension vs New Plugin Family

The current MessageParser contract is stateless across messages: each
`parse()` call is independent. Video decoding is inherently stateful — the
decoder must process all frames from the last keyframe to produce the
current frame.

Options:

- Extend MessageParser with an optional stateful mode for video
- Introduce a new plugin family (e.g., MediaParser) with a stateful contract
- Keep MessageParser stateless but have the frame store manage decoder state
  internally, with the parser only responsible for extracting raw bytes

### Mixed Scalar + Media Output from a Single Parse Call

An MCAP file may contain both scalar time-series topics and image/video
topics. A single DataSource plugin handles the file. When the DataSource
dispatches a message to a parser:

- Scalar fields go to pj_datastore via the existing write host
- Media handles go to the frame store via a media write host

The parse contract must support producing both output types from a single
invocation. Whether this requires a combined write host or two separate hosts
passed to the parser is a design decision.

### Metadata Availability: Eager vs Lazy

- Image messages carry width, height, and encoding inline — metadata is
  always available eagerly at ingest time.
- VideoFrame messages encode dimensions in the bitstream (SPS for H.264,
  Sequence Header for AV1). Extracting metadata requires parsing the
  bitstream.
- Trade-off: parsing the first keyframe eagerly (once per channel) gives
  better UX (viewer knows dimensions before the first frame request) but adds
  ingest cost. Deferring to first resolve keeps ingest fast.
- Suggested resolution: parse the first keyframe eagerly (one parse per
  channel, not per frame), defer for subsequent keyframes.
