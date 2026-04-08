# pj_media Requirements

## 1. Purpose

Media storage and retrieval engine for PlotJuggler Core. Handles image, video,
and scene data alongside time-series data in pj_datastore. Provides lazy,
on-demand frame access synchronized with the global timeline.

pj_media does not decode eagerly or hold multi-GB media in memory. At ingest
it stores lightweight handles; actual pixel data is decoded only when a viewer
requests a specific frame at a specific time.

## 2. Goals

- Lazy storage: store lightweight handles at ingest, decode on demand — never
  load multi-GB media into memory

- Unified query interface: viewer asks "give me the frame at time T for
  channel C" regardless of whether the backend is a file, a buffer, or a
  live stream

- Time synchronization: media channels share the same nanosecond timeline as
  pj_datastore time-series

- Multi-camera: multiple named channels per data source, each independently
  queryable

- GPU-accelerated decode: hardware video decoding with software fallback

- Streaming with buffered replay: live mode shows the latest frame; when
  paused, the buffer becomes seekable

## 3. Use Cases

- **One-shot file import** — open an MCAP file containing images or
  CompressedVideo, a LeRobot dataset (MP4 + Parquet), or RLDS TFRecords.
  Build a lightweight index of frame handles. No pixel data is decoded until
  a viewer requests a specific timestamp.

- **Live streaming** — receive frames from ROS 2 image topics, RTSP cameras,
  GStreamer pipelines, or V4L2 local cameras. Display the latest frame in
  real time. Accumulate compressed bytes in a bounded buffer for replay
  when paused.

- **Synchronized scrubbing** — user drags the global time slider. The video
  panel resolves the handle at the new timestamp, decodes the frame, and
  displays it. Works identically for file-backed and buffer-backed data.

- **Multi-camera robotics** — a robotics dataset with multiple RGB cameras,
  depth cameras, and segmentation overlays per camera. Each is a separate
  named channel. The viewer composites layers per camera.

- **Paused stream replay** — during a live session the user pauses. The
  buffer of recent compressed data becomes seekable. The user scrubs through
  the last N seconds of buffered frames, then resumes live.

## 4. Functional Requirements

### 4.1 Data Types

All media data types are defined in [datatypes_2D.md](datatypes_2D.md):

| Type | Role |
|------|------|
| Image | Raw, compressed, depth, and segmentation frames (self-contained) |
| VideoFrame | H.264 / H.265 / AV1 / VP9 encoded frames (GOP-dependent) |

This document does not duplicate type definitions. See datatypes_2D.md for
field-level schemas, encoding rules, and design rationale.

### 4.2 Lazy Handle Model

At ingest, each frame is represented by a lightweight handle rather than
decoded pixel data:

- A handle stores: timestamp, channel name, and an opaque reference to the
  data's location (file offset, buffer pointer, or callback).

- Metadata (width, height, encoding) is available eagerly when cheap (images
  carry it inline) and lazily when expensive (video requires parsing
  SPS/VPS from the bitstream).

- When a viewer requests a frame, the handle is resolved: the backend seeks
  to the data, reads compressed bytes, decodes, and writes pixels into a
  caller-provided buffer. The caller passes a mutable reference to a
  pre-allocated pixel buffer, allowing memory reuse across frames. The
  buffer type is Qt-independent.

- Handle implementations vary by backend:
  - MCAP: file handle + message index entry
  - LeRobot MP4: file path + presentation timestamp (PTS)
  - RLDS TFRecord: shard path + record byte offset
  - Live buffer: buffer pointer + byte range

### 4.3 Streaming and Buffered Replay

Sources: ROS 2 image/video topics, RTSP, GStreamer pipelines, V4L2 local
cameras.

- **Live mode**: always display the latest decoded frame. No random access
  during live playback.

- **Buffer accumulation**: while live, accumulate compressed bytes alongside
  their timestamps in a bounded buffer.

- **Eviction**: buffer is bounded by a time window (configurable, tens of
  seconds) OR a memory cap, whichever is reached first. For video, eviction
  is GOP-aware — entire GOPs are evicted as a unit; no partial GOP removal
  and no re-encoding.

- **Paused replay**: when the user pauses, the buffer becomes seekable. Frame
  handles pointing into the buffer support the same resolve interface as
  file-backed handles. Resuming returns to live mode.

### 4.4 Frame Store Interface

A single interface used by viewers to obtain decoded frames:

- `resolve(channel, timestamp)` returns decoded pixels for the frame at or
  nearest-before the requested timestamp.
- The backend behind the interface is either a ring buffer (streaming) or a
  file-backed lazy reader (dataset import). The viewer does not distinguish
  between them.
- A small LRU cache keyed by (channel, timestamp) avoids re-decoding when the
  user scrubs back to a recently viewed frame. The cache is capped by memory.
  The cache stores full-resolution decoded frames — display scaling is a
  viewer concern and is cheap; re-decoding is expensive. Window resize must
  not invalidate the cache.
- For per-frame codecs (JPEG, PNG), decode cost dominates over I/O. The lazy
  model saves memory; the LRU cache saves decode cost.
- The viewer must support zoom (mouse wheel) and pan (mouse drag) on the
  rendered frame. With GPU rendering this is essentially free via a view
  transform matrix in the vertex shader.

### 4.5 Time Synchronization

Media channels share the global timeline with pj_datastore:

- Timestamps are nanoseconds since epoch (`int64_t`), matching pj_datastore's
  Timestamp type.
- MCAP: nanosecond timestamps are carried directly in each message.
- MP4: requires an external timestamp mapping. For LeRobot datasets, the
  Parquet `timestamp` column provides the mapping. For standalone MP4 files,
  a user-provided epoch offset may be needed.
- Live streams: use source-provided timestamps when available (e.g., ROS
  message header stamps). Fall back to time-of-arrival when no source
  timestamp exists.

### 4.6 DataSource and Parser Integration

A single DataSource plugin can produce both time-series data (written to
pj_datastore) and media data (written to the frame store):

- The DataSource/MessageParser decoupling from pj_plugins applies to media:
  the DataSource handles transport (open file, connect to stream); the parser
  handles format-specific decoding.
- For media, the parser produces lazy handles rather than eagerly decoded
  pixel data. This differs from the time-series path where parsers write
  scalar values directly.
- Open design question: video decoding is inherently stateful (decoder state
  spans frames within a GOP). The current MessageParser contract is stateless
  across messages. Whether to extend MessageParser with stateful capabilities
  or introduce a new mechanism is deferred to architecture design.
- A single parse invocation may produce both scalar fields (written to
  pj_datastore) and media handles (written to frame store) from the same
  message. The parse contract must support mixed output.

## 5. Non-Functional Requirements

- C++20, consistent with pj_base and pj_datastore
- Qt 6 is optional at the library level — rendering is a GUI-side concern;
  pj_media provides decoded pixel buffers, not Qt widgets
- GPU-accelerated video decode with software fallback; specific library
  choice is an implementation decision
- Support multiple concurrent cameras at typical robotics resolutions and
  frame rates
- No audio support
- Timestamps are `int64_t` nanoseconds, matching pj_datastore
- Clean under AddressSanitizer (ASAN) in debug builds
- Builds with -Wall -Wextra -Werror

## 6. Module Contract

- Viewers interact through the frame store interface — they never touch
  backend internals (file readers, buffer pointers, codec state).
- DataSource plugins write media handles through a media write host, parallel
  to the existing pj_datastore write host for time-series.
- Channel identifiers are strings (topic names), consistent with pj_datastore
  topic naming.
- The frame store does not own or manage the global timeline. It reads the
  current time from the same source as pj_datastore consumers.

## 7. Deferred / Out of Scope

- Audio
- 3D rendering (point clouds, scene primitives, and grids are rendered by the
  GUI layer, not pj_media)
- Specific codec library choice — implementation decision
- Reverse playback
- Recording and encoding (pj_media is decode/display only)
- Specific performance numbers beyond "comfortable multi-camera robotics use"
- Plugin marketplace delivery of media-specific extensions
- Persistence of frame store state (save/load decoded frames to disk)
