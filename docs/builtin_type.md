# Builtin Types

Builtin types are PlotJuggler's canonical SDK vocabulary for object-like data.
They are the shim between third-party message families and the data shapes that
PlotJuggler can classify, store, decode, and render consistently.

A plugin that reads a ROS message, a Protobuf message, a JSON payload, or any
other source-specific format converts that input into one of these types. The
conversion removes source-specific naming and wire-layout details while keeping
the semantic value intact. For example, a ROS `sensor_msgs/Image` and another
image message schema can both become `PJ::sdk::Image`; a ROS
`sensor_msgs/PointCloud2` can become `PJ::sdk::PointCloud`.

The public headers live under:

```cpp
#include <pj_base/builtin/builtin_object.hpp>
#include <pj_base/builtin/image.hpp>
#include <pj_base/builtin/depth_image.hpp>
#include <pj_base/builtin/point_cloud.hpp>
#include <pj_base/builtin/compressed_point_cloud.hpp>
#include <pj_base/builtin/occupancy_grid.hpp>
#include <pj_base/builtin/mesh3d.hpp>
#include <pj_base/builtin/video_frame.hpp>
#include <pj_base/builtin/asset_video.hpp>
#include <pj_base/builtin/scene_entities.hpp>
#include <pj_base/builtin/robot_description.hpp>
#include <pj_base/builtin/image_annotations.hpp>
#include <pj_base/builtin/frame_transforms.hpp>
// Codecs — one per type, all share the canonical PJ.<Type> wire format under pj_base/proto/pj/.
#include <pj_base/builtin/image_codec.hpp>
#include <pj_base/builtin/depth_image_codec.hpp>
#include <pj_base/builtin/point_cloud_codec.hpp>
#include <pj_base/builtin/compressed_point_cloud_codec.hpp>
#include <pj_base/builtin/occupancy_grid_codec.hpp>
#include <pj_base/builtin/mesh3d_codec.hpp>
#include <pj_base/builtin/video_frame_codec.hpp>
#include <pj_base/builtin/asset_video_codec.hpp>
#include <pj_base/builtin/scene_entities_codec.hpp>
#include <pj_base/builtin/image_annotations_codec.hpp>
#include <pj_base/builtin/frame_transforms_codec.hpp>
```

## Design Principles

**Convert at the boundary.** DataSource and MessageParser plugins understand
third-party schemas. PlotJuggler internals consume builtin types. This keeps
ROS, dataset-specific, and vendor-specific details out of viewers and storage
policy.

**Unify when only the encoding differs.** Raw `rgb8`, `jpeg`, and `png` are all
images. They share the same consumer semantics, so they are represented by
`Image` with an `encoding` string rather than separate raw/compressed types.

**Split when the semantic value differs.** A `mono16` grayscale image and a
`16UC1` depth map can have similar byte layouts, but they do not mean the same
thing. Depth data is represented by `DepthImage` because consumers interpret it
as metric distance with camera intrinsics.

**Keep large buffers zero-copy capable.** Byte-backed types split metadata from
payload bytes. The SDK object stores the header fields PlotJuggler needs to
interpret the payload, such as image dimensions or point-cloud field layout, and
stores the payload itself as `Span<const uint8_t>` plus a `BufferAnchor`. The
span points at the bytes; the anchor keeps the underlying allocation alive while
consumers use it.

**Keep small objects owned.** `ImageAnnotations` and `FrameTransforms` own
their vectors, strings, and scalar fields directly. Future marker types should
follow the same pattern unless they grow payload-sized byte arrays. These values
are small enough that the zero-copy anchor pattern is unnecessary.

**Do not force one serialization path on every builtin.** Large byte-backed
types are views over source-native payload bytes whenever possible; they should
not be repacked just to produce a canonical blob. Small owned types may define
canonical codecs when storage or replay needs bytes. Those codecs serialize the
owned SDK value directly to the canonical protobuf-wire payload described by the
`.proto` contract. The schema and wire-format details stay private; public SDK
headers expose only SDK structs.

## Serialization Families

Builtin objects fall into two serialization families:

| Family | Current types | Storage model | Codec policy |
|--------|---------------|---------------|--------------|
| Byte-backed views | `Image`, `DepthImage`, `PointCloud`, `CompressedPointCloud`, `OccupancyGrid`, `OccupancyGridUpdate`, `Mesh3D`, `VideoFrame` | Header fields live in the SDK struct; payload bytes live behind `Span<const uint8_t>` plus `BufferAnchor`. | No mandatory canonical codec; preserve zero-copy views over ROS, MCAP, compressed image, point-cloud, or plugin-owned payloads. If conversion is unavoidable, allocate a new payload and anchor it. |
| Owned values | `ImageAnnotations`, `FrameTransforms`, `SceneEntities`, `AssetVideo`, `RobotDescription`, `CameraInfo`; future marker types | SDK structs own their vectors/strings/scalars directly. | Add explicit codecs when canonical bytes are needed. Codecs serialize the owned value to the protobuf-wire payload described by the `.proto` contract, using shared private wire primitives. `RobotDescription` carries source-format text as-is (no canonical codec) — the format hint distinguishes URDF / SDF / MJCF. |

Canonical `.proto` files live under `pj_base/proto/pj` and act as the wire
format contract. One file per top-level message, each named after its message
(`Image.proto`, `SceneEntities.proto`, `FrameTransforms.proto`, …). Shared
geometry primitives are grouped in `Geometry.proto`: `Point2`, `Point3`,
`Vector2`, `Vector3`, `Quaternion`, and `Pose`. See
[`pj_base/proto/pj/README.md`](../pj_base/proto/pj/README.md) for the family
grouping (raster, point-cloud, scene, 2D annotation, …).

The codecs do not expose generated Protobuf types in public SDK headers. The
current implementation does not require generated Protobuf code or a Protobuf
runtime dependency; it uses private field-tagged wire primitives and maps only
between bytes and SDK structs.

## Type Erasure and Classification

`BuiltinObjectType` is the a-priori tag a parser reports for a schema. It lets
the host decide that a topic produces images, point clouds, depth images, image
annotations, frame transforms, or no builtin object.

| Type | Concrete type | Purpose |
|------|---------------|---------|
| `kNone` | none | Scalar-only schema or unknown object. |
| `kImage` | `PJ::sdk::Image` | Raw or compressed image data. |
| `kPointCloud` | `PJ::sdk::PointCloud` | Packed 3D point records. |
| `kDepthImage` | `PJ::sdk::DepthImage` | Depth pixels plus camera intrinsics. |
| `kImageAnnotations` | `PJ::sdk::ImageAnnotations` | Pixel-space overlay primitives. |
| `kFrameTransforms` | `PJ::sdk::FrameTransforms` | Named 3D frame relationships. |
| `kOccupancyGrid` | `PJ::sdk::OccupancyGrid` | 2D metric occupancy grid (maps, costmaps) in world coordinates. |
| `kCompressedPointCloud` | `PJ::sdk::CompressedPointCloud` | Point cloud delivered in a format-specific compressed binary (e.g. Draco). |
| `kMesh3D` | `PJ::sdk::Mesh3D` | 3D mesh asset in its native binary format (GLTF/GLB/STL/PLY/OBJ/USD/DAE). |
| `kVideoFrame` | `PJ::sdk::VideoFrame` | One frame of an inter-frame-coded video stream (h264/h265/vp9/av1). |
| `kSceneEntities` | `PJ::sdk::SceneEntities` | Procedural 3D scene primitives (arrows, cubes, lines, text, …). |
| `kAssetVideo` | `PJ::sdk::AssetVideo` | File-backed video reference plus typed playback metadata. |
| `kRobotDescription` | `PJ::sdk::RobotDescription` | Raw URDF/SDF/MJCF text + format hint. |
| `kCameraInfo` | `PJ::sdk::CameraInfo` | Pinhole camera calibration (intrinsics K, distortion D, rectification R, projection P). |
| `kOccupancyGridUpdate` | `PJ::sdk::OccupancyGridUpdate` | Incremental sub-rectangle patch for a previously-published `OccupancyGrid`. |
| `kLog` | `PJ::sdk::Log` | Textual log message (severity level + text + originating name). |

`BuiltinObject` is `std::any`. Producers store a concrete builtin value in it;
consumers recover the concrete type with `std::any_cast<T>(&object)` or ask
`typeOf(object)` for the type supported by the current SDK build.

## Image

`Image` is a self-contained image payload. It covers raw pixel buffers and
single-frame compressed payloads. The `encoding` string tells consumers how to
interpret `data`.

Use `Image` when the decoded value is a color or luminance image: camera frames,
screenshots, thumbnails, JPEG/PNG-compressed image messages, or raw image
messages.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the image. |
| `width` | `uint32_t` | Image width in pixels. |
| `height` | `uint32_t` | Image height in pixels. |
| `encoding` | `std::string` | Raw pixel layout or compression codec. |
| `row_step` | `uint32_t` | Bytes per row for raw encodings; `0` for compressed payloads. |
| `is_bigendian` | `bool` | Meaningful for multi-byte raw encodings such as `mono16`. |
| `data` | `Span<const uint8_t>` | Raw pixel bytes or compressed payload bytes. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |
| `compressed_depth_min` | `std::optional<float>` | ROS compressed-depth quantization metadata, when present. |
| `compressed_depth_max` | `std::optional<float>` | ROS compressed-depth quantization metadata, when present. |

Common raw encodings are `rgb8`, `rgba8`, `bgr8`, `bgra8`, `mono8`, and
`mono16`. Common compressed encodings are `jpeg`, `png`, and `qoi`.
`compressedDepth` is supported for ROS-style compressed depth image payloads.

`Image::encoding` is intentionally open-ended. `CommonImageEncoding` documents
the encodings known to the SDK and provides string conversion helpers, but
plugins may still emit conventional source-specific encodings when needed.

## DepthImage

`DepthImage` is a self-contained depth map. Pixels represent distance from the
camera, and the object carries the camera intrinsics needed to interpret pixels
geometrically.

Use `DepthImage` when consumers should treat the payload as metric depth rather
than luminance. A ROS depth image such as `16UC1` or `32FC1` is a typical source
for this type.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the depth frame. |
| `width` | `uint32_t` | Image width in pixels. |
| `height` | `uint32_t` | Image height in pixels. |
| `encoding` | `std::string` | Depth pixel representation, such as `16UC1` or `32FC1`. |
| `data` | `Span<const uint8_t>` | Depth pixel bytes. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |
| `K` | `std::array<double, 9>` | 3x3 row-major camera intrinsic matrix. |
| `distortion_model` | `std::string` | Empty for rectified images; otherwise identifies the distortion model. |
| `D` | `std::vector<double>` | Distortion coefficients for `distortion_model`. |

`K` follows the usual camera matrix convention:

```text
[ fx   0   cx ]
[  0  fy   cy ]
[  0   0    1 ]
```

Helpers in `pj_base/builtin/depth_image_utils.hpp` derive common matrices such as
rectification rotation and projection matrix when a consumer wants them.

## PointCloud

`PointCloud` is a packed array of point records. Each point occupies
`point_step` bytes, and `fields` describes where each channel lives inside one
point record.

Use `PointCloud` for converted point-cloud messages such as ROS
`sensor_msgs/PointCloud2`, LiDAR packets that have been assembled into points,
or any source that produces a packed point buffer.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the cloud. |
| `width` | `uint32_t` | Number of points per row, or total points for unorganized clouds. |
| `height` | `uint32_t` | Number of rows; `1` for unorganized clouds. |
| `point_step` | `uint32_t` | Bytes per point. |
| `row_step` | `uint32_t` | Bytes per row. Usually `point_step * width` when tightly packed. |
| `is_bigendian` | `bool` | Whether packed field values are big-endian. |
| `is_dense` | `bool` | `false` when some points may be invalid, typically NaN-filled. |
| `frame_id` | `std::string` | Source coordinate frame for the points; needed by 3D consumers to resolve TF to a fixed frame. |
| `fields` | `std::vector<PointField>` | Channel layout for each point. |
| `data` | `Span<const uint8_t>` | Packed point bytes. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |

Each `PointField` describes one channel:

| Field | Type | Notes |
|-------|------|-------|
| `name` | `std::string` | Channel name, such as `x`, `y`, `z`, `intensity`, `rgb`, `ring`, or `time`. |
| `offset` | `uint32_t` | Byte offset of this channel inside one point. |
| `datatype` | `PointField::Datatype` | One of signed/unsigned integer or floating-point scalar types. |
| `count` | `uint32_t` | Number of elements of `datatype`; usually `1`. |

The point layout intentionally mirrors common robotics formats while avoiding a
ROS-specific enum in the SDK type.

## ImageAnnotations

`ImageAnnotations` contains vector overlays in image-pixel coordinates. It is
used for detections, labels, tracked points, masks expressed as outlines, and
other lightweight 2D overlays that are drawn on top of an image.

Use `ImageAnnotations` when the coordinates are pixels in a specific image, not
world coordinates. The type references the base image topic through
`image_topic`, allowing a renderer to associate annotations with their image
stream.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `Timestamp` | Timestamp associated with the annotation set. |
| `image_topic` | `std::string` | Topic of the image these annotations overlay. |
| `points` | `std::vector<PointsAnnotation>` | Points, line lists, line strips, and line loops. |
| `circles` | `std::vector<CircleAnnotation>` | Filled or stroked circles in image-pixel space. |
| `texts` | `std::vector<TextAnnotation>` | Text labels anchored at pixel positions. |

`PointsAnnotation` supports four topologies:

| Topology | Meaning |
|----------|---------|
| `kPoints` | Each point is independent. |
| `kLineList` | Consecutive pairs form independent line segments. |
| `kLineStrip` | Points form a connected polyline. |
| `kLineLoop` | Like a line strip, but the last point connects back to the first. |

Colors are RGBA `uint8_t` values. If `PointsAnnotation::colors` is empty, the
uniform `color` applies to every vertex. If `colors.size() == points.size()`,
per-vertex colors are used. `fill_color` applies to closed loops and circles
when its alpha channel is non-zero.

`pj_base/builtin/image_annotations_codec.hpp` serializes and deserializes this
type using the canonical `PJ.ImageAnnotations` protobuf wire format.
See [image_annotations_format.md](image_annotations_format.md) for the field
mapping and compatibility rules.

## FrameTransforms

`FrameTransforms` contains a batch of time-stamped 3D transforms between named
reference frames. It is used for TF-style data where consumers need to place
objects, point clouds, camera frustums, or markers in a shared frame graph.

Use `FrameTransforms` when the semantic value is a parent/child frame
relationship: a translation vector and quaternion rotation from
`parent_frame_id` to `child_frame_id` at a specific timestamp.

| Field | Type | Notes |
|-------|------|-------|
| `transforms` | `std::vector<FrameTransform>` | Transform records carried by one source payload. |

Each `FrameTransform` contains:

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `Timestamp` | Timestamp associated with the transform. |
| `parent_frame_id` | `std::string` | Name of the parent reference frame. |
| `child_frame_id` | `std::string` | Name of the child reference frame. |
| `translation` | `Vector3` | Child-frame origin in parent-frame coordinates. |
| `rotation` | `Quaternion` | Child-frame orientation relative to the parent frame. |

`pj_base/builtin/frame_transforms_codec.hpp` serializes and deserializes this type
using the canonical `PJ.FrameTransforms` protobuf wire format.

## OccupancyGrid

`OccupancyGrid` is a 2D metric occupancy grid placed in world coordinates. It
covers ROS-style nav maps, costmaps, and any rasterized 2D probability /
cost layer with a metric resolution and world placement.

Use `OccupancyGrid` when the value is a regular 2D grid whose cells carry
8-bit signed occupancy (`-1` unknown, `0..100` percent occupied). Frame
graph navigation builtins use this rather than `Image` because the
renderer cares about cell-to-world placement, not pixel layout.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the map. |
| `frame_id` | `std::string` | Frame in which `origin` is expressed. |
| `origin` | `Pose` | World pose of cell (0, 0). The grid lies in `origin`'s local xy-plane. |
| `resolution` | `double` | Cell size in meters (square cells). |
| `width` | `uint32_t` | Number of columns (cells along x). |
| `height` | `uint32_t` | Number of rows (cells along y). |
| `data` | `Span<const uint8_t>` | Row-major cell bytes; size must equal `width * height`. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |

`pj_base/builtin/occupancy_grid_codec.hpp` serializes and deserializes this
type using the canonical `PJ.OccupancyGrid` protobuf wire format.

## OccupancyGridUpdate

`OccupancyGridUpdate` is the incremental counterpart to `OccupancyGrid`: a
row-major sub-rectangle patch into a previously-published base grid (ROS
`map_msgs/OccupancyGridUpdate`, e.g. `<base>/costmap_updates`).

It deliberately carries **no** `origin` / `resolution` — a patch is not
independently placeable. A stateful consumer pairs the update with its base
grid (by topic-name convention, `<base>/costmap_updates` ↔ `<base>/costmap`)
and positions it at the base's `origin + (x, y) * resolution`. This keeps the
producer stateless and cross-topic-blind; all accumulation / placement lives in
the consumer. The patch is a self-contained snapshot at its own timestamp, so it
stores and decodes like any other object (no replay required at decode time).

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp of the update. |
| `frame_id` | `std::string` | Must match the base grid's frame. |
| `x` | `int32_t` | Column offset (cells) of the patch top-left into the base grid. |
| `y` | `int32_t` | Row offset (cells) of the patch top-left into the base grid. |
| `width` | `uint32_t` | Patch width in cells. |
| `height` | `uint32_t` | Patch height in cells. |
| `data` | `Span<const uint8_t>` | Row-major signed-8-bit cells; size must equal `width * height`. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |

`pj_base/builtin/occupancy_grid_update_codec.hpp` serializes and deserializes
this type using the canonical `PJ.OccupancyGridUpdate` protobuf wire format.

## CompressedPointCloud

`CompressedPointCloud` carries a point cloud delivered in a format-specific
compressed binary (e.g. Draco). It is distinct from `PointCloud` because
the wire layout is opaque to PlotJuggler — `data` plus `format` must be
handed to the matching decoder library, which produces a decompressed
point set on the host side. Same reasoning that separates `VideoFrame`
from `Image`.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the cloud. |
| `frame_id` | `std::string` | Frame in which the cloud is expressed once decoded. |
| `format` | `std::string` | Codec identifier, lowercase. Recognized values include `"draco"`. |
| `data` | `Span<const uint8_t>` | Compressed payload bytes. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |

`pj_base/builtin/compressed_point_cloud_codec.hpp` serializes and deserializes
this type using the canonical `PJ.CompressedPointCloud` wire format.

## Mesh3D

`Mesh3D` references a 3D mesh asset delivered in its native binary format.
The renderer hands `data` + `format` (or the contents at `url`) to a
mesh-loader library (Assimp, tinygltf, …); PlotJuggler does not parse the
asset itself. Distinct from `SceneEntities`'s `TrianglePrimitive` because
asset formats can carry richer scene content — materials, textures,
skinning, animations — that is not expressible as raw triangle soup.

Asset source: exactly one of `data` (with `anchor` keeping the bytes
alive) or `url` should be populated. When `data` is used, `format` is
required; when `url` is used, `format` may be inferred from the file
extension.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with the asset. |
| `frame_id` | `std::string` | Frame in which `pose` is expressed. |
| `id` | `std::string` | Republishing with the same id replaces the previous entry on the topic. |
| `pose` | `Pose` | Placement of the asset's local origin in `frame_id`. |
| `scale` | `Vector3` | Per-axis scale factor. Defaults to `(1, 1, 1)`. |
| `format` | `std::string` | `"gltf"`, `"glb"`, `"stl"`, `"ply"`, `"obj"`, `"usd"`, `"dae"`. |
| `data` | `Span<const uint8_t>` | Embedded asset bytes; non-empty implies `format` is required. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |
| `url` | `std::string` | External URL to the asset; used when `data` is empty. |
| `color` | `ColorRGBA` | Applied when `override_color` is true. |
| `override_color` | `bool` | When true, ignore embedded material color and tint with `color`. |

`pj_base/builtin/mesh3d_codec.hpp` serializes and deserializes this type
using the canonical `PJ.Mesh3D` protobuf wire format.

## VideoFrame

`VideoFrame` carries a single frame of a compressed video stream
(h264/h265/vp9/av1) when per-frame `Image` payloads would be wasteful.
Unlike `Image`, a video frame may have inter-frame dependencies
(P-frames, B-frames, etc.); consumers must maintain decoder state across
frames within a stream.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Frame presentation timestamp. |
| `frame_id` | `std::string` | Camera frame. Optical axis: `+x` right, `+y` down, `+z` into scene. |
| `format` | `std::string` | Codec identifier, lowercase. `"h264"`, `"h265"`, `"vp9"`, `"av1"`. |
| `data` | `Span<const uint8_t>` | Bitstream bytes for this frame. |
| `anchor` | `BufferAnchor` | Keeps `data` alive when it references shared storage. |

`pj_base/builtin/video_frame_codec.hpp` serializes and deserializes this
type using the canonical `PJ.VideoFrame` protobuf wire format.

## AssetVideo

`AssetVideo` is the entry-point handle for video assets ingested by data
loaders that point at an external media file — LeRobot datasets, MP4
loaders, and similar. Producers push exactly one `AssetVideo` per topic;
the ObjectStore timestamp of that entry equals `time_origin_ns` so
timeline UIs naturally see the asset's start instant.

Unlike `VideoFrame` (a single frame of a streamed payload), `AssetVideo`
carries no pixel data — it references the file by path and surfaces
decode-routing metadata (media type, dimensions, frame rate) without
forcing the consumer to open the file just to size a playback window.

| Field | Type | Notes |
|-------|------|-------|
| `time_origin_ns` | `std::optional<Timestamp>` | Wall-clock instant of the first frame. Absent means the asset is not aligned to wall clock. |
| `start_ns` | `std::optional<int64_t>` | In-file offset (ns) where the playable window begins. Absent means "play from the start of the file". |
| `end_ns` | `std::optional<int64_t>` | In-file offset (ns) where the playable window ends. Absent means "play to the end of the file". |
| `file_path` | `std::string` | Absolute path or path relative to a consumer-known root. |
| `media_type` | `std::string` | MIME type hint. Empty means probe the file. |
| `width` | `uint32_t` | Pixel width. `0` means unknown. |
| `height` | `uint32_t` | Pixel height. `0` means unknown. |
| `frame_rate` | `double` | Nominal FPS. `0` or NaN means unknown. |

When both `start_ns` and `end_ns` are absent the whole file is the playable
window. When present, consumers must clamp seek requests to
`[start_ns, end_ns]` and bound timeline UI to that range. This is how
producers expose one clip out of a file that holds many concatenated
clips — for example LeRobot v3.0, where a single MP4 per camera packs
many episodes back-to-back and `[from_timestamp, to_timestamp]` in the
episode metadata maps directly to `[start_ns, end_ns]`.

The total file duration is *not* carried in the message — the decoder
backend reports it.

`pj_base/builtin/asset_video_codec.hpp` serializes and deserializes this
type using the canonical `PJ.AssetVideo` protobuf wire format.

## SceneEntities

`SceneEntities` is the workhorse for marker-style 3D visualization — the
equivalent of ROS's `visualization_msgs/MarkerArray`. A `SceneEntity`
bundles heterogeneous primitives sharing a `frame_id` and timestamp;
`SceneEntities` is the batch container shipped on a topic.

Use `SceneEntities` when the value is procedural 3D scene content
expressible as a small set of primitives: arrows, cubes, spheres,
cylinders, line strips/loops/lists, triangles, text labels, coordinate
axes glyphs, or model (mesh asset) references.

| Field on `SceneEntity` | Type | Notes |
|------------------------|------|-------|
| `timestamp` | `Timestamp` | Stamp used together with `lifetime_ns` to control expiry. |
| `frame_id` | `std::string` | Frame the entity's primitives are expressed in. |
| `id` | `std::string` | Republishing with the same `(topic, id)` replaces the previous entity. |
| `lifetime_ns` | `int64_t` | `0` means persist until replaced; otherwise expire `lifetime_ns` after `timestamp`. |
| `frame_locked` | `bool` | When true, track `frame_id` as it moves; when false, stamp into the fixed frame at publish time. |
| `arrows` / `cubes` / `spheres` / `cylinders` / `lines` / `triangles` / `texts` / `axes` / `models` | `std::vector<…Primitive>` | Heterogeneous primitive lists. `models` references a mesh asset by `url` or inline `data`. |

The `SceneEntities` batch also carries `deletions` (`std::vector<SceneEntityDeletion>`):
removal commands that let a snapshot-based producer express the removal half of a
stateful stream (e.g. ROS Marker `DELETE` / `DELETEALL`). A deletion is either
`kMatchingId` (remove the entity with the given `id`) or `kAll` (clear the topic).

Each primitive carries its own `Pose`, geometry-specific size or shape
fields, and color (or per-vertex colors, where applicable). See
`pj_base/include/pj_base/builtin/scene_entities.hpp` for the per-primitive
fields and `pj_base/proto/pj/SceneEntities.proto` for the wire contract.

`pj_base/builtin/scene_entities_codec.hpp` serializes and deserializes
this type using the canonical `PJ.SceneEntities` protobuf wire format.

## RobotDescription

`RobotDescription` carries a robot kinematic + visual model as the raw source-
format text plus a `format` hint string. The SDK does not parse the document;
downstream consumers (notably the 3D viewer) do the format-specific parsing
and asset resolution.

Use `RobotDescription` when the message represents a kinematic / visual model
description: a ROS `/robot_description` topic with `std_msgs/String` payload
containing URDF XML, an SDF world, an MJCF model, or any future textual robot-
description format.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp the description was observed. |
| `topic` | `std::string` | Source topic name (e.g. `/robot_description`). Empty if not topic-sourced. |
| `format` | `std::string` | Format hint set by the producer after validation. Examples: `urdf`, `sdf`, `mjcf`. Open-ended like `Image::encoding`. |
| `text` | `std::string` | Raw source text. Consumers parse according to `format`. |

Design notes:

- **No canonical codec.** The format space is open and growing; embedding a
  format-specific codec in the SDK would multiply schemas without payoff.
  Consumers parse the text directly with format-specific libraries (e.g.
  TinyXML for URDF / SDF / COLLADA, mjcf parsers for MJCF).
- **No embedded mesh bytes.** URDF/SDF reference meshes via `package://` URIs
  or relative paths; mesh resolution is consumer-side (search paths, MCAP
  attachments, sidecar directories). Embedding meshes in the SDK type would
  force assumptions about that resolution and bloat ObjectStore for the
  common case of a single robot referenced by thousands of TF samples.
- **Producer responsibility.** A parser emitting `RobotDescription` should
  validate the text matches `format` (e.g. for URDF, that the root element is
  `<robot>`) before emission. Generic `std_msgs/String` payloads on unrelated
  topics should not surface as RobotDescription.

## CameraInfo

`CameraInfo` carries pinhole camera calibration — intrinsics, distortion,
rectification, and projection — for one camera frame (ROS
`sensor_msgs/CameraInfo`). Consumers use it to draw camera frustums, back-project
depth pixels into 3D, and rectify or overlay onto images.

Like `OccupancyGridUpdate`, it is correlated to its image / depth topic by
topic-name convention (`<ns>/camera_info` ↔ `<ns>/image_raw`); the object itself
carries no topic linkage. It is an owned value (small matrices and a distortion
vector, no byte blob), so no `BufferAnchor` is needed.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp_ns` | `Timestamp` | Timestamp associated with this calibration. |
| `frame_id` | `std::string` | Camera optical frame. |
| `width` | `uint32_t` | Image width in pixels. |
| `height` | `uint32_t` | Image height in pixels. |
| `distortion_model` | `std::string` | e.g. `plumb_bob`, `rational_polynomial`, `equidistant`; empty when rectified. |
| `D` | `std::vector<double>` | Distortion coefficients; size depends on the model. |
| `K` | `std::array<double, 9>` | 3x3 row-major intrinsics `[fx 0 cx; 0 fy cy; 0 0 1]`. |
| `R` | `std::array<double, 9>` | 3x3 row-major rectification (identity for monocular). |
| `P` | `std::array<double, 12>` | 3x4 row-major projection / camera matrix. |

Sub-window fields (binning, ROI) from `sensor_msgs/CameraInfo` are intentionally
omitted; they are additive later if a consumer needs them.
`pj_base/builtin/camera_info_codec.hpp` serializes and deserializes this type
using the canonical `PJ.CameraInfo` protobuf wire format.

## Log

`Log` is a single textual log message, for a log/console panel. It mirrors the
core of Foxglove's `Log` schema (and `rcl_interfaces/Log` / `rosgraph_msgs/Log`).

| Field on `Log` | Type | Notes |
|----------------|------|-------|
| `timestamp_ns` | `Timestamp` | Time of the log message. |
| `level` | `Log::Level` | `kUnknown`/`kDebug`/`kInfo`/`kWarning`/`kError`/`kFatal` (values match Foxglove). |
| `message` | `std::string` | Log text. |
| `name` | `std::string` | Originating process / node / logger name. |

Foxglove's source-location fields (`file`, `line`) are intentionally omitted.
`pj_base/builtin/log_codec.hpp` serializes and deserializes this type using the
canonical `PJ.Log` protobuf wire format.

## Conversion Examples

| Source type | Canonical builtin type | Conversion intent |
|-------------|------------------------|-------------------|
| ROS `sensor_msgs/Image` | `Image` or `DepthImage` | Choose `DepthImage` when the semantic value is metric depth; otherwise use `Image`. |
| ROS `sensor_msgs/CompressedImage` | `Image` | Preserve compressed bytes and set `encoding` to the codec. |
| ROS `sensor_msgs/PointCloud2` | `PointCloud` | Map point fields, strides, density, endianness, and packed bytes. |
| Draco-compressed cloud | `CompressedPointCloud` | Forward the opaque blob plus `"draco"` format; decoding happens on the host. |
| ROS `nav_msgs/OccupancyGrid` | `OccupancyGrid` | Map metadata (resolution, origin) into the struct; keep cell bytes zero-copy. |
| URDF / `visualization_msgs/Marker` mesh resource | `Mesh3D` | Embed `data` (with `format`) or point at `url`; preserve `pose` and `scale`. |
| ROS `nav_msgs/Path`, marker arrays | `SceneEntities` | Map polylines to `LinePrimitive`, arrows to `ArrowPrimitive`, etc. |
| H.264/H.265/VP9/AV1 stream frame | `VideoFrame` | Forward one frame's bitstream bytes plus the codec identifier. |
| MP4 / MKV / AV1 dataset file | `AssetVideo` | Push once per topic with the file path and metadata; consumers seek into the file by tracker time. |
| Detection or tracking message | `ImageAnnotations` | Convert boxes, points, circles, and labels into pixel-space primitives. |
| ROS `tf2_msgs/TFMessage` | `FrameTransforms` | Convert transform batches into named parent/child frame relationships. |
| ROS `std_msgs/String` on `/robot_description` (or matching name) carrying URDF XML | `RobotDescription` | Validate root element matches `format`, then carry the raw text + format hint. No mesh resolution at parse time. |
| ROS `sensor_msgs/CameraInfo` | `CameraInfo` | Map K / D / R / P plus dimensions; correlate to the image topic by name. Sub-window (binning / ROI) is dropped. |
| ROS `map_msgs/OccupancyGridUpdate` | `OccupancyGridUpdate` | Forward the cell-space patch (`x`/`y`/`width`/`height` + bytes); the consumer pairs it with the base grid and supplies origin/resolution. |

The builtin type is the boundary object. After conversion, consumers should not
need to know which third-party schema produced it.
