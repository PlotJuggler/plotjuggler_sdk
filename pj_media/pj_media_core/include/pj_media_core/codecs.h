#pragma once

#include <memory>

#include "pj_media_core/codec_pipeline.h"

namespace PJ {

/// Strips CDR envelope from a ROS2 message. Finds JPEG/PNG marker
/// and returns the payload bytes as the output frame's pixels.
class CdrImageStripper : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// Strips the ROS2 compressedDepth header. Finds PNG signature
/// within the payload and returns it.
class CompressedDepthStripper : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// JPEG → RGB888 via turbojpeg.
class JpegCodec : public CodecStage {
 public:
  JpegCodec();
  ~JpegCodec() override;
  JpegCodec(const JpegCodec&) = delete;
  JpegCodec& operator=(const JpegCodec&) = delete;
  JpegCodec(JpegCodec&&) = delete;
  JpegCodec& operator=(JpegCodec&&) = delete;

  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;

 private:
  void* tj_ = nullptr;
};

/// PNG → RGB888 or RGBA8888 or Mono16 via libpng.
class PngCodec : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// Mono16 depth → RGB888 grayscale (normalized min/max).
/// Preserves width/height from the input frame.
class DepthToGrayscale : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// Mono8 segmentation class IDs → RGB888 with distinct colors per class.
/// Preserves width/height from the input frame.
class SegmentationPalette : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

// --- Pipeline builders ---

std::unique_ptr<CodecPipeline> makeJpegPipeline();
std::unique_ptr<CodecPipeline> makeDepthPipeline();

}  // namespace PJ
