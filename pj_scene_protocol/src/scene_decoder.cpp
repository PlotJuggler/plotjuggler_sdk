#include "pj_scene_protocol/scene_decoder.h"

#include <memory>
#include <string_view>

namespace PJ {

// Single decoder kind, defined in scene_decoder_protobuf.cpp.
std::unique_ptr<ISceneDecoder> makeSceneDecoderProtobufImageAnnotations();

std::unique_ptr<ISceneDecoder> makeSceneDecoder(std::string_view schema_name) {
  if (schema_name == kSchemaImageAnnotations) {
    return makeSceneDecoderProtobufImageAnnotations();
  }
  return nullptr;
}

}  // namespace PJ
