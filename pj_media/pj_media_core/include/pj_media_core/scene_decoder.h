#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_media_core/scene_frame.h"

namespace PJ {

/// MCAP schema names recognized by the scene decoder factory. Use these
/// constants instead of raw string literals so a typo can't silently disable
/// a decoder.
inline constexpr std::string_view kSchemaDetection2DArray = "vision_msgs/msg/Detection2DArray";
inline constexpr std::string_view kSchemaYoloDetectionArray = "yolo_msgs/msg/DetectionArray";
inline constexpr std::string_view kSchemaFoxgloveImageAnnotations = "foxglove.ImageAnnotations";

/// True if the given schema name is one this module can decode.
constexpr bool isSupportedSceneSchema(std::string_view schema) {
  return schema == kSchemaDetection2DArray || schema == kSchemaYoloDetectionArray ||
         schema == kSchemaFoxgloveImageAnnotations;
}

/// Decodes wire-format bytes (one per topic message) into a SceneFrame of
/// vector primitives. Stateless — one instance per scene/annotation layer.
///
/// Concrete implementations:
///   - CDR decoder for ROS 2 native schemas (e.g. vision_msgs/msg/Detection2DArray)
///   - Protobuf decoder for Foxglove schemas (e.g. foxglove.ImageAnnotations)
class ISceneDecoder {
 public:
  virtual ~ISceneDecoder() = default;
  virtual Expected<SceneFrame> decode(const uint8_t* data, size_t size) = 0;
};

/// Factory: returns a decoder for the given MCAP schema name, or nullptr if
/// the schema is not supported. Caller owns the returned decoder.
///
/// Currently supported (see kSchema* constants above):
///   - vision_msgs/msg/Detection2DArray — CDR (ROS 2 native)
///   - yolo_msgs/msg/DetectionArray     — CDR (yolo_ros)
///   - foxglove.ImageAnnotations        — Protobuf (Foxglove Studio)
std::unique_ptr<ISceneDecoder> makeSceneDecoder(std::string_view schema_name);

}  // namespace PJ
