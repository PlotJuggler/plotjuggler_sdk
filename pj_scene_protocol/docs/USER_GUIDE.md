# pj_scene_protocol User Guide

How to produce or consume marker / scene data over PlotJuggler's canonical wire format. This guide is for plugin developers (DataSource, MessageParser) and viewer authors who need to integrate visual overlays.

The module exposes one schema header and one codec header:

```cpp
#include "pj_scene_protocol/image_annotation.h"        // value types
#include "pj_scene_protocol/image_annotation_codec.h"  // writer + schema name
#include "pj_scene_protocol/scene_decoder.h"           // reader (consumers only)
```

Linking: `target_link_libraries(your_target PRIVATE pj_scene_protocol)`. The transitive `pj_base` dep is the only thing it pulls in.

For the wire format reference, type catalog, and design rationale, see `ARCHITECTURE.md` in this folder.

---

## 1. Producer recipe (loader / data source)

A loader fills an `ImageAnnotation` from its source format and serializes to canonical bytes before pushing into the host's data store.

```cpp
#include "pj_scene_protocol/image_annotation.h"
#include "pj_scene_protocol/image_annotation_codec.h"

PJ::ImageAnnotation buildAnnotation(const Detection& det) {
  PJ::ImageAnnotation ia;

  // Bounding box as a 4-point line loop.
  PJ::PointsAnnotation rect;
  rect.topology = PJ::AnnotationTopology::kLineLoop;
  rect.points = {
      {det.x_min, det.y_min}, {det.x_max, det.y_min},
      {det.x_max, det.y_max}, {det.x_min, det.y_max},
  };
  rect.color = paletteColor(det.class_id);  // see pj_media/demos/marker_palette as reference
  rect.thickness = 2.0;
  ia.points.push_back(std::move(rect));

  // Class label above the box.
  PJ::TextAnnotation label;
  label.position = {det.x_min, det.y_min - 4.0};
  label.text = det.class_name + " " + std::to_string(det.score);
  label.font_size = 14.0;
  label.color = {255, 255, 255, 255};
  ia.texts.push_back(std::move(label));

  return ia;
}

// In your loader's per-message callback:
auto bytes = PJ::serializeImageAnnotation(buildAnnotation(detection));
host.pushObject(topic_id, ts_ns, bytes.data(), bytes.size());
```

When you register the topic, stamp the schema name in `metadata_json`:

```cpp
TopicOptions opts;
opts.metadata_json = R"({"encoding":"foxglove.ImageAnnotations"})";
auto topic_id = host.registerTopic("/detections", opts);
```

That `encoding` value is the only signal the consumer side uses to dispatch the right decoder. If it is missing or misspelled, the data still arrives in the store but no viewer will pick it up.

---

## 2. Consumer recipe (viewer / sink)

A consumer reads bytes out of the store and decodes them with the canonical decoder.

```cpp
#include "pj_scene_protocol/scene_decoder.h"

auto decoder = PJ::makeSceneDecoder(PJ::kSchemaImageAnnotations);
if (!decoder) {
  // Topic's metadata_json said something other than "foxglove.ImageAnnotations".
  return;
}

auto result = decoder->decode(bytes.data(), bytes.size());
if (!result.has_value()) {
  // result.error() is a string with a wire-level reason.
  return;
}

const PJ::SceneFrame& sf = *result;
for (const PJ::ImageAnnotation& ia : sf.annotations) {
  for (const auto& pa : ia.points)   { renderPoints(pa); }
  for (const auto& ca : ia.circles)  { renderCircle(ca); }
  for (const auto& ta : ia.texts)    { renderText(ta);   }
}
```

The decoder is stateless — keep one per layer for the layer's lifetime, or build a fresh one per call (allocation is cheap). `decode()` does not throw.

---

## 3. Common pitfalls

**Schema-name mismatch.** `makeSceneDecoder("foxglove.image_annotations")` (lowercase) returns `nullptr`. Use the constant `kSchemaImageAnnotations` rather than a literal string. Same on the producer side — match the literal `"foxglove.ImageAnnotations"` exactly in `metadata_json`.

**Color drift.** `ColorRGBA{255, 0, 0, 255}` is the *most* a channel can drift; round-trip equality on individual `uint8` channels is not guaranteed exactly. If you compare in a test, allow ±1 LSB per channel — `image_annotation_codec_test.cpp::ColorEq` shows the pattern.

**Per-vertex `colors` semantics.** A `PointsAnnotation` honors `colors` only when `colors.size() == points.size()`. If `colors` is empty, the uniform `color` is splatted across all vertices. Anything else is implementation-defined; renderers may splat-last or ignore. Don't rely on the in-between case.

**`fill_color` only fires for `kLineLoop`.** Other topologies ignore `fill_color`. Setting an alpha-zero default fill is the convention for "no fill."

**Non-serialized fields.** `ImageAnnotation::timestamp` and `::image_topic` are populated by the consumer pipeline (timestamp comes from the store push; topic identity from the topic id). The codec does not round-trip them — equality on a freshly decoded annotation will see those fields as zero / empty. This is intentional; see `ARCHITECTURE.md §Wire format / Encoding rules`.

**`CircleAnnotation::radius`, not diameter.** The C++ surface is radius. The wire carries diameter. Don't double the value yourself when constructing.

**Empty annotations.** `serializeImageAnnotation()` on an `ImageAnnotation` with no primitives produces zero bytes. Pushing zero bytes is a valid "no overlays at this timestamp" signal; the decoder handles a non-empty buffer or returns an empty `SceneFrame`. Sending an empty buffer through `decode()` returns an error — guard the producer side or skip the push.

---

## 4. Translating from a custom message format

Per-source-format conversion is intentionally outside this module. A loader that reads, say, ROS 2 `vision_msgs/msg/Detection2DArray` is responsible for translating into `ImageAnnotation` itself.

For a working reference, see PJ4's `pj_media/demos/cdr_*_to_image_annotation.{h,cpp}`:

- `cdr_detection2d_to_image_annotation` — `vision_msgs/msg/Detection2DArray` → `ImageAnnotation`. Maps the first hypothesis's `class_id` to a stable palette colour and emits a `"<class> <score>"` text label above each bbox.
- `cdr_yolo_to_image_annotation` — `yolo_msgs/msg/DetectionArray` → `ImageAnnotation`. Same pattern, uses `class_name` for the label.
- `marker_palette` — FNV-1a class-id → `ColorRGBA` palette and label-string formatter. Reuse-friendly.

These adapters live in PJ4 because they consume PJ4-side fixtures (MCAP demo). The pattern transfers to any plugin: read your message, fill an `ImageAnnotation`, serialize with `serializeImageAnnotation()`.
