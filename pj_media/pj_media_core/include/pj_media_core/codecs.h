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

/// PNG → RGB888 (8-bit) or RGBA8888 (with alpha) or Mono16 (16-bit grayscale).
/// Preserves Mono16 for downstream stages (e.g. DepthToGrayscale).
class PngCodec : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// Mono16 depth → RGB888 grayscale. Normalizes to [min, max] range.
/// Input must be kMono16 with pixels->size() >= width*height*2.
class DepthToGrayscale : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

/// Mono8 class IDs → RGB888 false-color. Each class ID (0-255) maps to
/// a distinct hue. Input must be kMono8 with pixels->size() >= width*height.
class SegmentationPalette : public CodecStage {
 public:
  Expected<DecodedFrame> decode(const DecodedFrame& input) const override;
};

// --- Pipeline builders ---

std::unique_ptr<CodecPipeline> makeJpegPipeline();     ///< JpegCodec only (raw JPEG input)
std::unique_ptr<CodecPipeline> makeCdrJpegPipeline();  ///< CdrImageStripper → JpegCodec
std::unique_ptr<CodecPipeline> makeDepthPipeline();    ///< CompressedDepthStripper → PngCodec → DepthToGrayscale

}  // namespace PJ
