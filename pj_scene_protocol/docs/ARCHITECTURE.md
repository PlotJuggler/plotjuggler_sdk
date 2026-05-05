# pj_scene_protocol — Architecture

## Purpose and scope

`pj_scene_protocol` is the canonical wire format and codec for visual markers and scene primitives shared across PlotJuggler's data sources and viewers. It is the SDK boundary that lets a plugin author **produce** marker data (e.g. detection bounding boxes, labelled points) or **consume** it without dragging in any visualization stack.

Today the module covers 2D image annotations (points, lines, polygons, circles, text). It is named for forthcoming scope: 3D scene primitives (arrows, cubes, lines, meshes, text) are documented as the next addition, and the type system is laid out to accommodate them next to the 2D types without breaking existing wire bytes.

**In scope:**
- Schema (vocabulary types — `Point2`, `ColorRGBA`, `ImageAnnotation`, `SceneFrame`, …).
- A canonical wire format (`foxglove.ImageAnnotations` Protobuf) and a hand-rolled writer + reader for it.
- The schema-name string constant that producers stamp on stored topics.

**Out of scope (deliberately):**
- Per-source-format conversion. Translating from CDR `vision_msgs/Detection2DArray`, YOLO message types, CSV, RLDS, etc. into `ImageAnnotation` happens **loader-side**, never inside this module. PJ4's `pj_media/demos/cdr_*_to_image_annotation.{h,cpp}` are reference adapters.
- Storage / time-anchoring of scene frames (lives in PJ4's `pj_media/core/ScenePipelineSource` + `ObjectStore` from `pj_datastore`).
- Rendering (lives in PJ4's `pj_media/qt/MediaViewerWidget`).

This split keeps `pj_scene_protocol` linkable by a streaming-source plugin or a one-off ROS bag converter without pulling FFmpeg, QRhi shaders, or anything else PlotJuggler's host happens to need.

## Type catalog

All types are POD-shaped, default-constructible, and compare with `operator==`. They live in `pj_scene_protocol/image_annotation.h`.

| Type | Purpose |
|---|---|
| `Point2 {x, y}` | 2D point in image-pixel coordinates (origin top-left), `double` precision. |
| `ColorRGBA {r,g,b,a: uint8}` | 8-bit-per-channel color. `a == 0` is transparent. |
| `AnnotationTopology` (enum) | Vertex topology for `PointsAnnotation`: `kPoints`, `kLineList` (segments 0-1, 2-3, …), `kLineStrip` (polyline), `kLineLoop` (closes back; 4-point loop = rectangle). |
| `PointsAnnotation` | Vertices + topology + uniform `color` + optional per-vertex `colors` + `fill_color` (for `kLineLoop`) + `thickness`. |
| `CircleAnnotation` | `center` + `radius` (the wire format carries diameter; see below) + `thickness` + outline `color` + `fill_color`. |
| `TextAnnotation` | Anchor `position`, `text`, `font_size`, `color`. |
| `ImageAnnotation` | Bag of `points` + `circles` + `texts` for one image at one timestamp; refers to its base image via `image_topic`. |
| `SceneFrame` | Top-level decoder output. Wraps `vector<ImageAnnotation>`; future expansion will add 3D primitives, grids, etc. as sibling fields. |

## Wire format

The canonical wire format is **Foxglove `ImageAnnotations` Protobuf**. Conforming to it gives free interop with Foxglove Studio and other tools that consume the same schema.

The schema-name string is published as:

```cpp
inline constexpr std::string_view kSchemaImageAnnotations = "foxglove.ImageAnnotations";
```

Producers stamp this in the topic's `metadata_json` under the key `encoding`:

```json
{"encoding":"foxglove.ImageAnnotations"}
```

Consumers pass the same string to `makeSceneDecoder()` to obtain the matching decoder. A factory typo returns `nullptr` rather than misbehaving silently.

### Field numbers

The writer at `src/image_annotation_codec.cpp` and the reader at `src/scene_decoder_protobuf.cpp` agree on the following Protobuf field numbers (matching the published Foxglove schema):

| Message | Fields |
|---|---|
| `foxglove.ImageAnnotations` | `1: repeated CircleAnnotation`, `2: repeated PointsAnnotation`, `3: repeated TextAnnotation` |
| `foxglove.PointsAnnotation` | `2: type (enum)`, `3: repeated Point2`, `4: outline_color`, `5: repeated outline_colors`, `6: fill_color`, `7: thickness (double)` |
| `foxglove.CircleAnnotation` | `2: position (Point2)`, `3: diameter (double)`, `4: thickness (double)`, `5: fill_color`, `6: outline_color` |
| `foxglove.TextAnnotation` | `2: position (Point2)`, `3: text (string)`, `4: font_size (double)`, `5: text_color`, *6: background_color (skipped on write, skipped on read)* |
| `foxglove.Point2` | `1: x (double)`, `2: y (double)` |
| `foxglove.Color` | `1: r (double)`, `2: g (double)`, `3: b (double)`, `4: a (double)` — components in `[0, 1]` |

Topology enum mapping: `kPoints=1`, `kLineLoop=2`, `kLineStrip=3`, `kLineList=4`. The Foxglove enum reserves `0` for `UNKNOWN`; the writer never emits 0.

The wire types used are `VARINT(0)`, `I64(1)`, and `LEN(2)`. `I32(5)` is unused on write and skipped if encountered on read.

### Encoding rules / round-trip behavior

- **Color quantization is lossy.** `ColorRGBA` stores `uint8 [0, 255]`; the wire stores `double [0, 1]`. The writer divides by 255.0; the reader multiplies. A round-trip can drift up to 1 LSB on each channel. Tests assert with 1-LSB tolerance.
- **`CircleAnnotation::radius` ↔ wire `diameter`.** The writer emits `radius * 2`; the reader halves on read. The C++ surface always exposes radius.
- **Empty `colors` is preserved.** A `PointsAnnotation` with `colors.empty()` emits zero field-5 entries. Emitting a default `Color` for an empty vector would smuggle a phantom entry into the reader, breaking per-vertex coloring semantics. There is a regression test (`EmptyColorsVectorDoesNotInjectDefaultEntry`).
- **`ImageAnnotation::timestamp` and `::image_topic` do not cross the wire.** Those fields belong to the surrounding transport (the timestamp arrives via `ObjectStore`'s push; the topic identity is the topic). They are populated on read by the consumer pipeline, not by the codec.
- **`TextAnnotation::background_color` is intentionally absent from the C++ struct.** The wire format defines field 6, but the schema struct has no equivalent. The writer never emits it; the reader skips it.

## API surface

```cpp
// Schema constant (wire-format identifier).
inline constexpr std::string_view kSchemaImageAnnotations = "foxglove.ImageAnnotations";

// Producer side.
[[nodiscard]] std::vector<uint8_t> serializeImageAnnotation(const ImageAnnotation& ia);

// Consumer side.
class ISceneDecoder {
 public:
  virtual ~ISceneDecoder() = default;
  virtual Expected<SceneFrame> decode(const uint8_t* data, size_t size) = 0;
};
std::unique_ptr<ISceneDecoder> makeSceneDecoder(std::string_view schema_name);
```

`Expected<T>` is `pj_base/expected.hpp`. A decode failure returns an error string; `decode()` does not throw.

`makeSceneDecoder` returns `nullptr` if the schema name does not match `kSchemaImageAnnotations`. It is the caller's signal that the topic's `metadata_json` is wrong.

The decoder is stateless. The expected pattern is one decoder instance per scene/annotation layer for the lifetime of that layer.

## Design rationale

**Single canonical decoder.** There is exactly one decoder kind: Protobuf for `foxglove.ImageAnnotations`. Adding ROS-message decoders, CSV decoders, etc. inside this module was rejected — the consumer side must not grow N decoders for every robotics message dialect that exists. Per-source-format conversion is loader-side and writes canonical bytes into the store.

**Hand-rolled wire codec.** No `protoc`, no generated code, no libprotobuf. The reader is ~300 lines; the writer is ~200 lines. At this size the dependency cost outweighs the codegen convenience, and the explicit code makes wire-level decisions (field numbers, default values, `radius`/`diameter`) reviewable.

**Schema-name = version.** Following Foxglove's own convention, schemas are versioned by changing the type name. There is no in-band version field. If the wire shape ever needs to change incompatibly, a new constant (e.g. `kSchemaImageAnnotationsV2`) and a new decoder kind are added; old data keeps working with the old name.

**Pure value types.** No virtual base, no PIMPL, no allocators. The schema header includes only `<cstdint>`, `<string>`, `<vector>`, and `pj_base/types.hpp` (for `Timestamp`). A plugin author can include this header without a build-system thought.

## What is not here, and where it lives

| Concern | Lives in |
|---|---|
| Per-source-format conversion (CDR `vision_msgs/Detection2DArray`, `yolo_msgs/DetectionArray`, …) | PJ4 `pj_media/demos/cdr_*_to_image_annotation.{h,cpp}` (reference adapters); plugin loaders for production use |
| Class-id → palette mapping (FNV-1a hash) | PJ4 `pj_media/demos/marker_palette.{h,cpp}` |
| `MediaSource` integration: pulling annotation bytes out of an `ObjectStore`, decoding at a timestamp | PJ4 `pj_media/core/scene_pipeline_source.{h,cpp}` |
| Compositing base image with overlays, layer fusion | PJ4 `pj_media/core/composite_media_source.{h,cpp}` |
| Rendering: 5-pipeline QRhi (image, points, 1 px lines, thick lines, text) | PJ4 `pj_media/qt/media_viewer_widget.{h,cpp}` |

See `USER_GUIDE.md` for the producer and consumer code paths.
