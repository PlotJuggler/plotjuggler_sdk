#pragma once

#include <cstddef>
#include <cstdint>

#include "pj_base/expected.hpp"
#include "pj_media_core/cancel_token.h"
#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// Stateless image decoder: JPEG via turbojpeg, PNG via libpng, raw pixels via memcpy.
///
/// One instance per image layer. Multiple instances are safe (no shared state).
/// See ARCHITECTURE.md §4.2.
class ImageDecoder {
 public:
  ImageDecoder();
  ~ImageDecoder();

  ImageDecoder(const ImageDecoder&) = delete;
  ImageDecoder& operator=(const ImageDecoder&) = delete;
  ImageDecoder(ImageDecoder&&) = delete;
  ImageDecoder& operator=(ImageDecoder&&) = delete;

  /// Decode JPEG data to RGB888. Checks cancel token between header parse and pixel decode.
  Expected<DecodedFrame> decodeJpeg(const uint8_t* data, size_t size, const CancelTokenPtr& cancel = nullptr) const;

  /// Decode PNG data to RGB888 or RGBA8888 (depending on source alpha channel).
  static Expected<DecodedFrame> decodePng(const uint8_t* data, size_t size);

  /// Wrap raw pixel data (mono8, rgb8, rgba8, etc.) into a DecodedFrame. No decoding, just copy.
  static Expected<DecodedFrame> decodeRaw(const uint8_t* data, size_t size, int width, int height, PixelFormat format);

 private:
  void* tj_handle_ = nullptr;
};

}  // namespace PJ
