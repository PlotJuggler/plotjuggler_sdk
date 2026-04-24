#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// A single stage in a codec pipeline.
///
/// Each stage receives a DecodedFrame and produces a DecodedFrame.
/// For the first stage, the input frame contains raw bytes from ObjectStore
/// (width=0, height=0, format unset). Subsequent stages receive the
/// output of the previous stage with dimensions and format populated.
class CodecStage {
 public:
  virtual ~CodecStage() = default;

  /// Decode/transform the input frame. For the first stage in a pipeline,
  /// only pixels/size are meaningful (raw bytes). For later stages,
  /// width/height/format carry the previous stage's output metadata.
  virtual Expected<DecodedFrame> decode(const DecodedFrame& input) const = 0;
};

/// Ordered chain of CodecStages applied in sequence.
class CodecPipeline {
 public:
  void addStage(std::unique_ptr<CodecStage> stage);

  /// Run all stages. The initial input is raw bytes from ObjectStore.
  Expected<DecodedFrame> decode(const uint8_t* data, size_t size) const;

  [[nodiscard]] size_t stageCount() const noexcept {
    return stages_.size();
  }

 private:
  std::vector<std::unique_ptr<CodecStage>> stages_;
};

}  // namespace PJ
