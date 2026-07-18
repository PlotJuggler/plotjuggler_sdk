# Builtin object types

Most plugins emit **scalar time series** (a named field → a number over time). But
PlotJuggler also has a canonical vocabulary of **object-like** types — images,
point clouds, grids, transforms, markers, etc. If your data is one of these, emit
the matching builtin type (as serialized bytes on an *object topic*) instead of
faking it with scalar fields. This is the shim that keeps ROS/protobuf/vendor
specifics out of PlotJuggler's viewers and storage.

Reference: `docs/builtin_type.md`, headers in `pj_base/include/pj_base/builtin/`,
object store ABI in `V4_STORE.md`.

## Decision: builtin type or raw scalars?

1. **Is the payload a number (or a few named numbers) over time?** → raw scalar
   fields (`writeHost().appendRecord(...)`). No builtin. Done.
2. **Is it object-like?** Match it to a builtin:
   - image (raw or compressed) → `Image`; depth + intrinsics → `DepthImage`
   - 3D points → `PointCloud`, or `CompressedPointCloud` (opaque compressed)
   - 2D overlays on an image → `ImageAnnotations`
   - 3D frame graph → `FrameTransforms`; camera calibration → `CameraInfo`
   - occupancy/cost/ESDF → `OccupancyGrid` (+`OccupancyGridUpdate`) or `VoxelGrid`
   - marker-style 3D primitives → `SceneEntities`; pose arrays → `PosesInFrame`
   - mesh asset → `Mesh3D`; inter-frame video → `VideoFrame`
   - URDF/SDF/MJCF text → `RobotDescription`; log line → `Log`
   - findings/regions/events overlaid on a time-series plot → `PlotMarkers`
3. **Object-like but no builtin matches?** Emit meaningful scalars, or — when a
   consumer that understands your format exists — a clearly-identified *custom*
   object topic (the ObjectStore is deliberately payload-opaque). Never mislabel a
   custom payload as a builtin. `BuiltinObjectType::kNone` means
   "scalar/unknown classification", not "storage forbidden".

The full list (17 concrete types) and their exact fields are in
`docs/builtin_type.md` and one header each under
`pj_base/include/pj_base/builtin/` (the `builtin_object.hpp` enum is the
authoritative roster — trust it over prose that may lag behind newer additions).

## How you emit one

Object topics are separate from scalar topics and go through an object-write view.
You obtain that view from the base class — for a DataSource it is
`objectWriteHost()`, which returns a **nullable** `const SourceObjectWriteHostView*`
(it is null when the host has no ObjectStore; a MessageParser/Toolbox has its own
`ParserObjectWriteHostView` / `ToolboxHostView` object methods). Always null-check
it, then register a topic once and push per sample:

```cpp
#include <pj_base/builtin/image_annotations_codec.hpp>   // the matching codec

const auto* objectHost = objectWriteHost();          // DataSource accessor; nullable
if (!objectHost) return PJ::unexpected("object store unavailable");

PJ::sdk::ImageAnnotations anno = /* fill in */;
std::vector<uint8_t> bytes = PJ::serializeImageAnnotations(anno);   // PJ:: (not PJ::sdk::); infallible

// metadata_json is opaque to the store but read by VIEWERS to pick a renderer —
// an empty "{}" makes the topic unrenderable. Build it with MediaMetadataBuilder
// (pj_base/sdk/media_metadata.hpp):
const std::string meta = PJ::sdk::MediaMetadataBuilder().mediaClass("image_annotations").build();

auto topic = objectHost->registerTopic("overlays", meta);
if (!topic) return PJ::unexpected(topic.error());
auto st = objectHost->pushOwned(*topic, timestamp_ns, bytes);      // host copies the bytes
if (!st) return PJ::unexpected(st.error());
```

Each builtin has a `*_codec.hpp` under `PJ::` (not `PJ::sdk::`) with
`serializeXxx()` / `deserializeXxx()`. Note the asymmetry: `serializeXxx()` returns
a plain `std::vector<uint8_t>` (serialization can't fail), while `deserializeXxx()`
returns `PJ::Expected<...>`. The `RobotDescription` type is the exception — it
carries its source text as-is and has no codec.

**MessageParsers emit objects differently — by returning, not pushing.** A parser's
`SchemaHandler.parse_object` returns an `ObjectRecord{optional<Timestamp> ts,
BuiltinObject object}` — the host handles storage. Declare the handler's
`object_type` (`BuiltinObjectType::kImage`, …) so the host picks its ingest policy
*before* decoding any bytes, and propagate the incoming payload's `BufferAnchor`
into the returned object so large payloads stay zero-copy. See
`references/message-parser.md`.

## Two storage families → two byte strategies

- **Byte-backed** (Image, DepthImage, PointCloud, CompressedPointCloud,
  OccupancyGrid(+Update), VoxelGrid, Mesh3D, VideoFrame): potentially megabytes.
  Keep the payload a zero-copy `Span<const uint8_t>` anchored by a `BufferAnchor`
  (typically a `shared_ptr<vector<uint8_t>>`); only allocate new bytes if a
  conversion is unavoidable.
- **Owned values** (ImageAnnotations, FrameTransforms, SceneEntities,
  RobotDescription, CameraInfo, PosesInFrame, Log, PlotMarkers): small (hundreds of
  bytes) — the struct owns its `std::vector`s/strings directly; eager copy is fine.

## Owned vs lazy push

- **`pushOwned(topic, ts, bytes)`** — the host copies immediately; you may free your
  buffer as soon as the call returns.
- **`pushLazy(topic, ts, fetch)`** — the host keeps your closure and calls it only
  when a consumer reads that entry. Capture heavy state **by value** (e.g. a
  `shared_ptr<FileReader>`), so it stays alive as long as the entry does. Use this
  for large payloads you would rather read from a file on demand than hold in RAM.

```cpp
auto st = objectHost->pushLazy(*topic, ts,
    [reader = std::make_shared<FileReader>(path)] { return reader->readBytes(); });
if (!st) return PJ::unexpected(st.error());
```

The store owns the closure until the entry is evicted/removed, then destroys it
exactly once. Do not keep a raw pointer into anything the closure captured.

## Frames & correlation (gotcha)

- `PointCloud`/`Image`/`CameraInfo` carry a `frame_id`. To place moving-frame data
  in a fixed frame, also emit `FrameTransforms` at the same timestamp; a 3D viewer
  TF-transforms via the frame graph.
- `ImageAnnotations.image_topic` explicitly names the base image topic; camera
  calibration correlates by **name convention** (`<ns>/camera_info` ↔
  `<ns>/image_raw`), not by a reference.
- **Association fields ride *outside* the serialized bytes.** The annotations
  codec deliberately does not encode `image_topic` (or a timestamp) into the
  payload — the store keys entries by (topic, timestamp), and association is a
  topic/metadata convention. Don't expect a codec round-trip to carry them.

## Codec round-trips are not bit-exact

Some codecs normalize (e.g. circle radius↔diameter, RGBA float↔`uint8`). Do not
assert byte-for-byte equality after serialize→deserialize. See
`docs/image_annotations_format.md` for a worked example.
