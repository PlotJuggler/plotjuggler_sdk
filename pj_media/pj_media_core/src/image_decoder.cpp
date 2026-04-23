#include "pj_media_core/image_decoder.h"

#include <png.h>
#include <turbojpeg.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace PJ {

ImageDecoder::ImageDecoder() : tj_handle_(tjInitDecompress()) {}

ImageDecoder::~ImageDecoder() {
  if (tj_handle_ != nullptr) {
    tjDestroy(tj_handle_);
  }
}

Expected<DecodedFrame> ImageDecoder::decodeJpeg(const uint8_t* data, size_t size, const CancelTokenPtr& cancel) const {
  if (tj_handle_ == nullptr) {
    return unexpected("turbojpeg not initialized");
  }
  if (data == nullptr || size == 0) {
    return unexpected("empty input");
  }

  int width = 0;
  int height = 0;
  int subsamp = 0;
  if (tjDecompressHeader2(
          static_cast<tjhandle>(tj_handle_), const_cast<uint8_t*>(data), static_cast<unsigned long>(size), &width,
          &height, &subsamp) != 0) {
    return unexpected(std::string("JPEG header parse failed: ") + tjGetErrorStr());
  }

  if (cancel != nullptr && cancel->isCancelled()) {
    return unexpected("cancelled");
  }

  auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  if (tjDecompress2(
          static_cast<tjhandle>(tj_handle_), const_cast<uint8_t*>(data), static_cast<unsigned long>(size),
          pixels->data(), width, width * 3, height, TJPF_RGB, TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
    return unexpected(std::string("JPEG decode failed: ") + tjGetErrorStr());
  }

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = PixelFormat::kRGB888;
  return frame;
}

namespace {

struct PngReadContext {
  const uint8_t* data;
  size_t size;
  size_t offset;
};

void pngReadCallback(png_structp png, png_bytep out, png_size_t count) {
  auto* ctx = static_cast<PngReadContext*>(png_get_io_ptr(png));
  if (ctx->offset + count > ctx->size) {
    png_error(png, "read past end of buffer");
    return;
  }
  std::memcpy(out, ctx->data + ctx->offset, count);
  ctx->offset += count;
}

}  // namespace

Expected<DecodedFrame> ImageDecoder::decodePng(const uint8_t* data, size_t size) {
  if (data == nullptr || size < 8) {
    return unexpected("empty input");
  }
  if (png_sig_cmp(data, 0, 8) != 0) {
    return unexpected("not a PNG file");
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png == nullptr) {
    return unexpected("png_create_read_struct failed");
  }
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    return unexpected("png_create_info_struct failed");
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    return unexpected("PNG decode failed");
  }

  PngReadContext ctx{data, size, 0};
  png_set_read_fn(png, &ctx, pngReadCallback);
  png_read_info(png, info);

  auto width = static_cast<int>(png_get_image_width(png, info));
  auto height = static_cast<int>(png_get_image_height(png, info));
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  // Normalize to 8-bit RGB or RGBA
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
    png_set_tRNS_to_alpha(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  png_read_update_info(png, info);

  bool has_alpha = (png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA) != 0;
  int channels = has_alpha ? 4 : 3;
  auto row_bytes = static_cast<size_t>(width) * static_cast<size_t>(channels);

  auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(height) * row_bytes);
  std::vector<png_bytep> row_ptrs(static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    row_ptrs[static_cast<size_t>(y)] = pixels->data() + static_cast<size_t>(y) * row_bytes;
  }

  png_read_image(png, row_ptrs.data());
  png_destroy_read_struct(&png, &info, nullptr);

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = has_alpha ? PixelFormat::kRGBA8888 : PixelFormat::kRGB888;
  return frame;
}

Expected<DecodedFrame> ImageDecoder::decodeRaw(
    const uint8_t* data, size_t size, int width, int height, PixelFormat format) {
  if (data == nullptr || size == 0) {
    return unexpected("empty input");
  }

  int channels = 0;
  switch (format) {
    case PixelFormat::kMono8:
      channels = 1;
      break;
    case PixelFormat::kMono16:
      channels = 2;
      break;
    case PixelFormat::kRGB888:
    case PixelFormat::kBGR888:
      channels = 3;
      break;
    case PixelFormat::kRGBA8888:
    case PixelFormat::kBGRA8888:
      channels = 4;
      break;
    default:
      return unexpected("unsupported raw pixel format");
  }

  auto expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
  if (size < expected_size) {
    return unexpected("raw buffer too small");
  }

  auto pixels = std::make_shared<std::vector<uint8_t>>(expected_size);
  std::memcpy(pixels->data(), data, expected_size);

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = format;
  return frame;
}

}  // namespace PJ
