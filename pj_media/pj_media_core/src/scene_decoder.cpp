#include "pj_media_core/scene_decoder.h"

#include <memory>
#include <string_view>

namespace PJ {

// Format-specific factories implemented in their own translation units.
std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrDetection2DArray();
std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrYoloDetectionArray();
std::unique_ptr<ISceneDecoder> makeSceneDecoderProtobufImageAnnotations();

std::unique_ptr<ISceneDecoder> makeSceneDecoder(std::string_view schema_name) {
  if (schema_name == kSchemaDetection2DArray) {
    return makeSceneDecoderCdrDetection2DArray();
  }
  if (schema_name == kSchemaYoloDetectionArray) {
    return makeSceneDecoderCdrYoloDetectionArray();
  }
  if (schema_name == kSchemaFoxgloveImageAnnotations) {
    return makeSceneDecoderProtobufImageAnnotations();
  }
  return nullptr;
}

}  // namespace PJ
