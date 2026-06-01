# `pj/` protobuf schemas

Canonical wire-format contracts for PlotJuggler's builtin object types — the shim
between source-specific message families (ROS, Protobuf, JSON, ...) and the data
shapes that PlotJuggler can classify, store, and render consistently. Each schema
defines the authoritative on-disk / on-wire layout for a corresponding SDK struct
under `pj_base/include/pj_base/builtin/`. See `docs/builtin_type.md` for design
rationale.

## Schemas

### Shared primitives

- **`Color.proto`** — RGBA color (channels in `[0, 1]`).
  - `Color`
- **`Geometry.proto`** — spatial primitives reused across schemas.
  - `Point2`, `Point3`, `Vector2`, `Vector3`, `Quaternion`, `Pose`
- **`KeyValuePair.proto`** — opaque user-attached metadata slot.
  - `KeyValuePair`

### Frame graph

- **`FrameTransforms.proto`** — TF-style coordinate frame relationships so consumers can place data in a common world frame.
  - `FrameTransform`, `FrameTransforms`

### Camera calibration

- **`CameraInfo.proto`** — pinhole camera calibration (intrinsics, distortion, rectification, projection) so consumers can draw frustums, back-project depth, and rectify; correlated to an image topic by name.
  - `CameraInfo`

### Byte-backed raster builtins

- **`Image.proto`** — single 2D image, raw (`rgb8`, `mono16`, …) or compressed (`jpeg`, `png`, `qoi`) unified under a single `encoding` string.
  - `Image`
- **`DepthImage.proto`** — metric depth image with camera intrinsics + distortion so consumers can back-project pixels into 3D.
  - `DepthImage`
- **`OccupancyGrid.proto`** — 2D metric occupancy grid (maps, costmaps) placed in world coordinates via an origin pose + cell resolution.
  - `OccupancyGrid`
- **`OccupancyGridUpdate.proto`** — incremental sub-rectangle patch for a previously-published `OccupancyGrid`; placed by the consumer against the base grid (no own origin/resolution).
  - `OccupancyGridUpdate`
- **`Log.proto`** — a single textual log message (severity level + text + originating name) for a log/console panel; mirrors the core of Foxglove's `Log` (file/line omitted).
  - `Log`
- **`VideoFrame.proto`** — one frame of an inter-frame-coded video stream (`h264`, `h265`, `vp9`, `av1`) when per-frame `Image` messages would be wasteful. Field layout is wire-identical to `foxglove.CompressedVideo` (timestamp=1, frame_id=2, data=3, format=4), so one decoder parses both.
  - `VideoFrame`
- **`AssetVideo.proto`** — reference to a file-backed video plus typed playback metadata (path, MIME type, dimensions, frame rate) so consumers can size playback windows without opening the file.
  - `AssetVideo`

### Point clouds

- **`PointCloud.proto`** — uncompressed packed 3D point cloud with a self-describing per-channel field layout.
  - `PointField`, `PointCloud`
- **`CompressedPointCloud.proto`** — point cloud delivered in a format-specific compressed binary (e.g. Draco) when the uncompressed schema would be too large.
  - `CompressedPointCloud`

### 3D scene

- **`SceneEntities.proto`** — the workhorse for marker-style 3D visualization; batches procedural primitives sharing a frame and timestamp.
  - Primitives: `ArrowPrimitive`, `CubePrimitive`, `SpherePrimitive`, `CylinderPrimitive`, `LinePrimitive`, `TrianglePrimitive`, `TextPrimitive`, `AxesPrimitive`
  - Entity: `SceneEntity`
  - Batch: `SceneEntities`
- **`Mesh3D.proto`** — 3D mesh asset delivered in its native binary format (GLTF/GLB/STL/PLY/OBJ/USD/DAE) for URDF-style or scene-mesh visualization.
  - `Mesh3D`

### 2D image annotations (vector overlays)

- **`CircleAnnotation.proto`** — circle overlay in 2D image-pixel space.
  - `CircleAnnotation`
- **`PointsAnnotation.proto`** — points / line list / line strip / line loop overlay in 2D image-pixel space.
  - `PointsAnnotation`
- **`TextAnnotation.proto`** — text label overlay in 2D image-pixel space.
  - `TextAnnotation`
- **`ImageAnnotations.proto`** — bundle the annotations above for one image, with a shared timestamp and image reference.
  - `ImageAnnotations`
