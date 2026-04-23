#include "pj_media_core/codec_pipeline.h"

namespace PJ {

void CodecPipeline::addStage(std::unique_ptr<CodecStage> stage) {
  stages_.push_back(std::move(stage));
}

Expected<DecodedFrame> CodecPipeline::decode(const uint8_t* data, size_t size) const {
  if (stages_.empty()) {
    return unexpected("empty pipeline");
  }

  DecodedFrame current;
  current.pixels = std::make_shared<std::vector<uint8_t>>(data, data + size);

  for (size_t i = 0; i < stages_.size(); ++i) {
    if (i > 0 && current.isNull()) {
      return unexpected("intermediate stage produced null frame");
    }
    auto result = stages_[i]->decode(current);
    if (!result.has_value()) {
      return result;
    }
    current = std::move(*result);
  }

  return current;
}

}  // namespace PJ
