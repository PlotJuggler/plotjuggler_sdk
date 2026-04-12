#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace PJ {

/// Pixel format tag for decoded image data.
enum class PixelFormat : uint8_t {
  kRGB888,
  kRGBA8888,
  kBGR888,
  kBGRA8888,
  kMono8,
  kMono16,
  kYUV420P,
  kNV12,
};

/// Decoded pixel buffer produced by ImageDecoder or VideoDecoder.
struct DecodedFrame {
  std::shared_ptr<std::vector<uint8_t>> pixels;
  int width = 0;
  int height = 0;
  PixelFormat format = PixelFormat::kRGB888;

  [[nodiscard]] bool isNull() const {
    return pixels == nullptr || pixels->empty();
  }
};

}  // namespace PJ
