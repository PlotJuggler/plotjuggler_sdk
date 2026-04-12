# Scene Types — Unified Type Catalog

## 1. Design Principles

### Separate storage backends

The existing columnar store remains unchanged for plottable time-series (scalar fields
over time). This document defines new types for 2D and 3D scene data. Each topic belongs
to exactly one storage kind; the two stores share the same timestamp, dataset, and topic
identity systems.

### Unify where the difference is just a field value

When two types differ only in a parameter (e.g., raw vs compressed image), they are one
type with an encoding/mode field. Separate types exist only when the data shape is
fundamentally different.

### 2D is a specialization of 3D

For scene primitives (markers, lines, meshes), the 2D variant is a 3D primitive with
z = 0. There is no separate 2D marker system.

Image annotations (pixel-space overlays) are a genuine separate type because they operate
in image pixel coordinates, not world coordinates.

### TF-style transform model

Transforms use explicit `parent_frame_id` / `child_frame_id` strings, following the
ROS TF convention. This matches PlotJuggler's robotics audience and avoids coupling
the transform graph to any path/entity hierarchy.

---

## 2. Common Building Blocks

These are not standalone types — they are embedded fields within the types below.

| Name | Fields | Notes |
|------|--------|-------|
| `Point3` | `double x, y, z` | Position in 3D. For 2D, z = 0. |
| `Vector3` | `double x, y, z` | Non-positional 3D vector (size, scale, velocity). |
| `Quaternion` | `double x, y, z, w` | Unit quaternion (Hamilton convention). |
| `Pose` | `Point3 position, Quaternion orientation` | Rigid body transform. |
| `Color` | `uint8 r, g, b, a` | RGBA color, [0..255]. |

---

## 3. Transforms

### FrameTransform

Defines a single parent-to-child coordinate frame relationship.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `parent_frame_id` | `string` | Parent frame name. |
| `child_frame_id` | `string` | Child frame name. |
| `translation` | `Point3` | Child origin in parent frame. |
| `rotation` | `Quaternion` | Child orientation in parent frame. |

Multiple FrameTransform entries over time form a transform tree. The viewer resolves
chains to compute world-frame positions.

Batch ingest: a single message may carry an array of FrameTransform (the full tree
snapshot at one timestamp), analogous to Foxglove's `FrameTransforms`.

---

## 4. Image

A single unified type for self-contained image frames: raw pixels or single-frame
compressed formats. Every Image message is independently displayable — no decoder
state carries across messages.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `frame_id` | `string` | Coordinate frame of the camera. |
| `width` | `uint32` | Image width in pixels. |
| `height` | `uint32` | Image height in pixels. |
| `encoding` | `string` | Pixel format or compression codec (see table). |
| `step` | `uint32` | Row stride in bytes. 0 for compressed formats. |
| `data` | `bytes` | Pixel data (raw) or compressed bitstream. |
| `depth_scale` | `double` | Optional. Meters per pixel unit. When present, the image is a depth map: `distance_meters = pixel_value * depth_scale`. Typical: `16UC1` with `depth_scale = 0.001` (mm), `32FC1` with `depth_scale = 1.0` (m). Depth + CameraCalibration can be converted to a PointCloud (derived transform or at ingest). |

### Encoding values

| Category | Encoding strings | Description |
|----------|-----------------|-------------|
| Raw | `mono8`, `mono16`, `rgb8`, `rgba8`, `bgr8`, `bgra8`, `16UC1`, `32FC1` | Uncompressed raster. `step` must be set. |
| Compressed | `jpeg`, `png`, `webp` | Single-frame compressed image. `step` = 0. |

### Segmentation images

A segmentation image is just a regular Image with integer-valued pixels (`mono8`,
`16UC1`). Each pixel value is a class ID. False-color rendering and transparency
are entirely viewer/UI concerns — the data model does not distinguish segmentation
from any other single-channel image.

### 2D layer compositing

A camera view in PJ can composite multiple image topics as layers. Layer ordering and
rendering mode (direct color, colormap, false-color with transparency) are viewer/UI
configuration — not encoded in the data model.

Layers are associated by `frame_id`: image topics sharing the same camera frame can
be composited. ImageAnnotation references its base image explicitly via `image_topic`.

Example topics for a single camera:

| Topic | Type | Notes |
|-------|------|---------|
| `/camera/image` | Image | Base RGB feed |
| `/camera/depth` | Image (with `depth_scale`) | Depth map, viewer colormaps it |
| `/camera/segmentation` | Image | Integer class IDs, viewer applies false colors |
| `/camera/annotations` | ImageAnnotation | Bounding boxes, labels |

Video is NOT part of this type — see VideoFrame (section 4b).

---

## 4b. VideoFrame

A single encoded video frame (one NAL unit group for H.264/H.265, one set of OBUs
for AV1). Unlike Image, video frames are **not self-contained**: delta frames (P-frames)
depend on a preceding keyframe (I-frame) and all intermediate frames for decoding.

This fundamental difference drives separate storage and query semantics:

- **Seeking**: latest-at must find the nearest preceding keyframe, then decode forward.
- **Eviction**: cannot evict a keyframe without invalidating its dependent delta frames.
- **Decoder lifecycle**: stateful — the consumer maintains a codec decoder across frames.
- **Joining mid-stream**: consumer must wait for the next keyframe before displaying.

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Presentation timestamp (nanoseconds since epoch). |
| `frame_id` | `string` | Coordinate frame of the camera. |
| `codec` | `string` | `h264`, `h265`, `av1`, `vp9`. |
| `data` | `bytes` | Encoded frame data (see codec rules below). |

### Codec rules

**No B-frames.** Only I-frames and P-frames are supported. This guarantees presentation
order equals decode order, eliminating the need for separate DTS/PTS and reorder buffers.

Each message must contain exactly enough data to decode **exactly one frame**.

| Codec | Keyframe `data` must include | Bitstream format |
|-------|------------------------------|------------------|
| `h264` | SPS + PPS NAL units before IDR slice | Annex B (start codes `00 00 00 01`) |
| `h265` | VPS + SPS + PPS NAL units before IDR/IRAP slice | Annex B |
| `av1` | Sequence Header OBU before KEY_FRAME OBU | Low overhead bitstream (spec section 5.2) |
| `vp9` | (implicit in keyframe) | Native |

Keyframes are self-contained: a decoder can be fully initialized from any keyframe
message without prior state. Delta frames require the decoder to have processed
the preceding keyframe and all intermediate frames.

### No explicit keyframe flag

Following both Rerun and Foxglove, there is no `is_keyframe` boolean in the schema.
Keyframe detection is determined by parsing the bitstream (NAL unit type for H.264/H.265,
OBU type for AV1, frame header for VP9). This avoids the risk of the flag contradicting
the actual bitstream content.

### Why not unified with Image?

| Concern | Image | VideoFrame |
|---------|-------|------------|
| Self-contained per message? | Yes | No (delta frames need prior state) |
| Latest-at query | Return one message, display it | Find keyframe, decode forward |
| Eviction | Any message independently | GOP-aware (keyframe protects dependents) |
| Consumer decoder state | None (stateless) | Persistent codec decoder |
| Width/height known? | Always (in schema) | Only from bitstream (SPS/VPS) |

---

## 5. Camera Calibration

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `frame_id` | `string` | Camera coordinate frame. |
| `width` | `uint32` | Image width in pixels. |
| `height` | `uint32` | Image height in pixels. |
| `distortion_model` | `string` | `plumb_bob`, `rational_polynomial`, `kannala_brandt`, `fisheye62`. |
| `D` | `double[]` | Distortion coefficients. |
| `K` | `double[9]` | 3x3 intrinsic matrix (row-major). |
| `R` | `double[9]` | 3x3 rectification matrix (row-major). |
| `P` | `double[12]` | 3x4 projection matrix (row-major). |

---

## 6. Point Cloud

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `frame_id` | `string` | Coordinate frame. |
| `pose` | `Pose` | Origin of the cloud relative to `frame_id`. |
| `point_stride` | `uint32` | Bytes per point in `data`. |
| `fields` | `FieldDescriptor[]` | Describes each channel (name, offset, type). |
| `data` | `bytes` | Packed point data. |

FieldDescriptor: `{ string name, uint32 offset, uint8 type }`.
Type enum: UINT8, UINT16, UINT32, INT8, INT16, INT32, FLOAT32, FLOAT64.

Standard field names: `x`, `y`, `z`, `r`, `g`, `b`, `a`, `intensity`, `ring`,
`nx`, `ny`, `nz` (normals).

This is compatible with ROS `sensor_msgs/PointCloud2` and Foxglove `PointCloud`.

---

## 7. Scene Primitives

### ScenePrimitive (common header + variant payload)

Every scene primitive shares a common header. The type-specific data lives in a
discriminated variant payload. This avoids Foxglove's "8 parallel arrays" pattern
while keeping compile-time type safety.

**Common header** (present on every primitive):

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | When this primitive is valid. |
| `frame_id` | `string` | Coordinate frame. |
| `id` | `string` | Primitive identifier. Same (topic, id) replaces prior primitive. |
| `lifetime_ns` | `int64` | Auto-expiry duration. 0 = persist until replaced/deleted. |
| `frame_locked` | `bool` | If true, re-transform every frame as TF updates. |
| `pose` | `Pose` | Position and orientation in `frame_id`. |
| `color` | `Color` | Uniform color. Type-specific semantics (see payload). |
| `payload` | `variant` | One of the payload types below. |

**Deletion**: a primitive with the same `(topic, id)` replaces the previous one.
To delete, publish a deletion marker (a special payload kind or a flag).

**SceneUpdate** (optional container): a single message carrying
`ScenePrimitive[]` + `SceneDeletion[]` for atomic multi-primitive updates. A topic
can carry either individual ScenePrimitive messages or SceneUpdate batches — the
storage layer treats them equivalently.

For 2D visualization in world space, use z = 0 in all positions.

### Payload: ArrowData

| Field | Type |
|-------|------|
| `shaft_length` | `double` |
| `shaft_diameter` | `double` |
| `head_length` | `double` |
| `head_diameter` | `double` |

### Payload: CubeData

| Field | Type | Notes |
|-------|------|-------|
| `size` | `Vector3` | Full extents along each axis. |

### Payload: SphereData

| Field | Type | Notes |
|-------|------|-------|
| `size` | `Vector3` | Diameter along each axis (ellipsoid if non-uniform). |

### Payload: CylinderData

| Field | Type | Notes |
|-------|------|-------|
| `size` | `Vector3` | Bounding box: x/y = diameter, z = height. |
| `bottom_scale` | `double` | 0..1, ratio of bottom face diameter. |
| `top_scale` | `double` | 0..1, ratio of top face diameter. 0 = cone. |

### Payload: MarkersData

Generic vertex-based primitive with a topology enum that maps 1:1 to OpenGL draw modes.
Covers points, lines, polylines, and closed polygons (outlines).

| Field | Type | Notes |
|-------|------|-------|
| `topology` | `enum` | OpenGL draw mode (see table). |
| `thickness` | `double` | Line width or point size. |
| `scale_invariant` | `bool` | If true, `thickness` is in screen pixels. |
| `points` | `Point3[]` | Vertex positions. |
| `colors` | `Color[]` | Per-vertex colors. Empty = use header `color`. |
| `indices` | `uint32[]` | Optional index buffer to avoid vertex duplication. |

| Topology | GL equivalent | Use case |
|----------|---------------|----------|
| `POINTS` | `GL_POINTS` | Scatter plots, waypoints |
| `LINE_LIST` | `GL_LINES` | Disconnected segments (pairs: 0-1, 2-3, ...) |
| `LINE_STRIP` | `GL_LINE_STRIP` | Connected path (0-1, 1-2, ..., n−1-n) |
| `LINE_LOOP` | `GL_LINE_LOOP` | Closed polygon outline (like LINE_STRIP + closing n-0) |

A rectangle is `LINE_LOOP` with 4 points. A polygon outline is `LINE_LOOP` with N points.

### Payload: MeshData

| Field | Type | Notes |
|-------|------|-------|
| `points` | `Point3[]` | Vertex positions. |
| `colors` | `Color[]` | Per-vertex colors. Empty = use header `color`. |
| `indices` | `uint32[]` | Triangle index buffer. Triples: (0,1,2), (3,4,5), ... |

### Payload: TextData

| Field | Type | Notes |
|-------|------|-------|
| `billboard` | `bool` | If true, always faces the camera. |
| `font_size` | `double` | Height of one line of text. |
| `scale_invariant` | `bool` | If true, `font_size` is in screen pixels. |
| `text` | `string` | |

### Payload: ModelData

| Field | Type | Notes |
|-------|------|-------|
| `scale` | `Vector3` | Scale factor per axis. |
| `override_color` | `bool` | If true, use header `color` instead of embedded materials. |
| `media_type` | `string` | MIME type (e.g. `model/gltf-binary`). |
| `data` | `bytes` | Embedded model data. |

---

## 8. Image Annotations

Overlays rendered in image pixel coordinates. These reference an image topic and draw
on top of it. They are not part of the 3D scene graph.

### ImageAnnotation

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `image_topic` | `string` | Topic of the image to overlay on. |
| `points` | `PointsAnnotation[]` | |
| `circles` | `CircleAnnotation[]` | |
| `texts` | `TextAnnotation[]` | |

### PointsAnnotation

Uses the same type-enum pattern as LinePrimitive, but in 2D pixel coordinates.

| Field | Type | Notes |
|-------|------|-------|
| `type` | `enum` | `POINTS`, `LINE_STRIP`, `LINE_LOOP`, `LINE_LIST`. |
| `points` | `Point2[]` | Pixel coordinates (origin = top-left). |
| `thickness` | `double` | Stroke width in pixels. |
| `color` | `Color` | Uniform color. |
| `colors` | `Color[]` | Per-point/segment colors. |
| `fill_color` | `Color` | Fill color (for LINE_LOOP = polygon). |

`Point2`: `{ double x, y }` in pixels.

### CircleAnnotation

| Field | Type | Notes |
|-------|------|-------|
| `center` | `Point2` | Center in pixels. |
| `radius` | `double` | Radius in pixels. |
| `color` | `Color` | Outline color. |
| `thickness` | `double` | Outline width in pixels. |
| `fill_color` | `Color` | Fill color. |

### TextAnnotation

| Field | Type | Notes |
|-------|------|-------|
| `position` | `Point2` | Anchor position in pixels. |
| `font_size` | `double` | Font size in pixels. |
| `color` | `Color` | Text color. |
| `text` | `string` | |

---

## 9. Grid (Occupancy / Costmap)

| Field | Type | Notes |
|-------|------|-------|
| `timestamp` | `int64` | Nanoseconds since epoch. |
| `frame_id` | `string` | Coordinate frame. |
| `pose` | `Pose` | Origin of grid cell (0,0). |
| `columns` | `uint32` | Number of columns. |
| `rows` | `uint32` | Number of rows. |
| `cell_size` | `Vector2` | Size of each cell in meters `{ x, y }`. |
| `cell_stride` | `uint32` | Bytes per cell in `data`. |
| `row_stride` | `uint32` | Bytes per row in `data`. |
| `fields` | `FieldDescriptor[]` | Per-cell channel layout (same as PointCloud). |
| `data` | `bytes` | Packed cell data. |

`Vector2`: `{ double x, y }`.

---

## 10. Summary — Complete Type List

| # | Type | Category | Self-contained? | Blob-like? |
|---|------|----------|-----------------|------------|
| 1 | FrameTransform | Transform | Yes | No |
| 2 | Image | 2D | Yes | Yes (pixel data). Optional `depth_scale` for depth maps. |
| 3 | VideoFrame | 2D | No (GOP deps) | Yes (encoded frame) |
| 4 | CameraCalibration | 2D | Yes | No |
| 5 | ImageAnnotation | 2D overlay | Yes | No |
| 6 | PointCloud | 3D | Yes | Yes (packed points) |
| 7 | ScenePrimitive (+ optional SceneUpdate) | 3D markers | Yes | No |
| 8 | Grid | 2D/3D | Yes | Yes (packed cells) |

ScenePrimitive payload variants:
ArrowData, CubeData, SphereData, CylinderData, MarkersData, MeshData, TextData, ModelData.

---

## 11. What We Chose Not To Include (and why)

| Omitted type | Reason |
|--------------|--------|
| Separate `CompressedImage` / `RawImage` | Unified into Image with encoding field. |
| Separate `DepthImage` | Image with optional `depth_scale` field. |
| Separate `SegmentationImage` | Just an integer Image (`mono8`, `16UC1`). False colors are a viewer concern. |
| Separate `Points3D` / `Points2D` | MarkersData with `topology = POINTS`. |
| Separate `LineStrips2D` / `Boxes2D` / `Arrows2D` | 3D primitives with z = 0. |
| `Tensor` | Out of scope — not a scene type. Can be revisited. |
| `BarChart` / `SeriesLines` | Belongs in the time-series/plotting layer, not scene types. |
| `JointState` | Plottable as time-series (scalars). Robot model visualization uses SceneEntity. |
| `VoxelGrid` | Deferred. Can be added later as a Grid variant or new type. |
| `Asset3D` (standalone) | Covered by ModelPrimitive inside SceneEntity. |
| `AssetVideo` (whole file) | Deferred. Pre-recorded MP4 import is a file-source concern, not a scene type. |
| `LaserScan` | Converted to PointCloud at ingest time (polar → cartesian). |
| `GraphNodes` / `GraphEdges` | Rerun-specific. Deferred. |
| `Log` | Deferred. Can be added later. |
| `GeoPoint` / `GeoJSON` | Deferred. Geospatial types can be added later. |
