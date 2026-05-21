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
#include <pj_base/builtin/image_annotations.hpp>
#include <pj_base/builtin/frame_transforms.hpp>
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
| Byte-backed views | `Image`, `DepthImage`, `PointCloud` | Header fields live in the SDK struct; payload bytes live behind `Span<const uint8_t>` plus `BufferAnchor`. | No mandatory canonical codec; preserve zero-copy views over ROS, MCAP, compressed image, point-cloud, or plugin-owned payloads. If conversion is unavoidable, allocate a new payload and anchor it. |
| Owned values | `ImageAnnotations`, `FrameTransforms`; future marker types | SDK structs own their vectors/strings/scalars directly. | Add explicit codecs when canonical bytes are needed. Codecs serialize the owned value to the protobuf-wire payload described by the `.proto` contract, using shared private wire primitives. |

Canonical `.proto` files live under `pj_base/proto/pj` and act as the wire
format contract. `PJ.ImageAnnotations` describes the annotation codec payload.
`PJ.FrameTransforms` describes the transform codec payload. Shared geometry
primitives are grouped in `Geometry.proto`: `Point2`, `Point3`, `Vector2`,
`Vector3`, and `Quaternion`.

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

## Conversion Examples

| Source type | Canonical builtin type | Conversion intent |
|-------------|------------------------|-------------------|
| ROS `sensor_msgs/Image` | `Image` or `DepthImage` | Choose `DepthImage` when the semantic value is metric depth; otherwise use `Image`. |
| ROS `sensor_msgs/CompressedImage` | `Image` | Preserve compressed bytes and set `encoding` to the codec. |
| ROS `sensor_msgs/PointCloud2` | `PointCloud` | Map point fields, strides, density, endianness, and packed bytes. |
| Detection or tracking message | `ImageAnnotations` | Convert boxes, points, circles, and labels into pixel-space primitives. |
| ROS `tf2_msgs/TFMessage` | `FrameTransforms` | Convert transform batches into named parent/child frame relationships. |

The builtin type is the boundary object. After conversion, consumers should not
need to know which third-party schema produced it.
