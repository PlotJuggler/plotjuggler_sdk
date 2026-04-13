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

/// Compute the expected pixel buffer size in bytes for a given format and dimensions.
/// Uses ceil(w/2), ceil(h/2) for chroma planes (correct for odd dimensions).
[[nodiscard]] inline size_t expectedBufferSize(int width, int height, PixelFormat format) noexcept {
  auto w = static_cast<size_t>(width);
  auto h = static_cast<size_t>(height);
  switch (format) {
    case PixelFormat::kRGB888:
    case PixelFormat::kBGR888:
      return w * h * 3;
    case PixelFormat::kRGBA8888:
    case PixelFormat::kBGRA8888:
      return w * h * 4;
    case PixelFormat::kMono8:
      return w * h;
    case PixelFormat::kMono16:
      return w * h * 2;
    case PixelFormat::kYUV420P: {
      size_t uv_w = (w + 1) / 2;
      size_t uv_h = (h + 1) / 2;
      return w * h + 2 * uv_w * uv_h;
    }
    case PixelFormat::kNV12: {
      size_t uv_h = (h + 1) / 2;
      return w * h + w * uv_h;
    }
  }
  return 0;
}

/// Decoded pixel buffer produced by ImageDecoder or VideoDecoder.
struct DecodedFrame {
  std::shared_ptr<std::vector<uint8_t>> pixels;
  int width = 0;
  int height = 0;
  PixelFormat format = PixelFormat::kRGB888;
  int64_t pts = -1;  // presentation timestamp from decoder (stream time_base units)

  [[nodiscard]] bool isNull() const noexcept {
    return pixels == nullptr || pixels->empty();
  }

  /// Check that pixels, dimensions, and format are mutually consistent.
  [[nodiscard]] bool isValid() const noexcept {
    return !isNull() && width > 0 && height > 0 && pixels->size() == expectedBufferSize(width, height, format);
  }
};

}  // namespace PJ
