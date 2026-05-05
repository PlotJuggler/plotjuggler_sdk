#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "pj_base/expected.hpp"
#include "pj_scene_protocol/image_annotation.h"
#include "pj_scene_protocol/image_annotation_codec.h"  // for kSchemaImageAnnotations

namespace PJ {

/// Decodes canonical wire-format bytes (foxglove.ImageAnnotations Protobuf —
/// the schema documented in `pj_media/docs/datatypes_2D.md §8` and serialized
/// by `pj_scene_protocol::serializeImageAnnotation`) into a `SceneFrame` of
/// vector primitives. Stateless — one instance per scene/annotation layer.
///
/// There is exactly ONE decoder kind. Per-source-format conversion (e.g. CDR
/// `vision_msgs/msg/Detection2DArray` → canonical bytes) lives loader-side and
/// is invisible to pj_media.
class ISceneDecoder {
 public:
  virtual ~ISceneDecoder() = default;
  virtual Expected<SceneFrame> decode(const uint8_t* data, size_t size) = 0;
};

/// Factory: returns the canonical Protobuf decoder. The `schema_name` argument
/// is checked against `kSchemaImageAnnotations` so a typo on the loader side
/// surfaces as a nullptr at construction.
std::unique_ptr<ISceneDecoder> makeSceneDecoder(std::string_view schema_name);

}  // namespace PJ
