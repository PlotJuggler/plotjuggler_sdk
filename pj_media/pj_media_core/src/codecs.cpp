#include "pj_media_core/codecs.h"

#include <png.h>
#include <turbojpeg.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace PJ {

// ---------------------------------------------------------------------------
// CdrImageStripper
// ---------------------------------------------------------------------------

Expected<DecodedFrame> CdrImageStripper::decode(const DecodedFrame& input) const {
  if (input.isNull() || input.pixels->size() < 16) {
    return unexpected("CDR data too small");
  }
  const auto* data = input.pixels->data();
  auto size = input.pixels->size();

  for (size_t i = 0; i + 3 < size; ++i) {
    bool is_jpeg = data[i] == 0xFF && data[i + 1] == 0xD8 && data[i + 2] == 0xFF;
    bool is_png = i + 7 < size && data[i] == 0x89 && data[i + 1] == 0x50 && data[i + 2] == 0x4E && data[i + 3] == 0x47;

    if (is_jpeg || is_png) {
      size_t payload_size = size - i;
      if (i >= 4) {
        uint32_t len = 0;
        std::memcpy(&len, data + i - 4, 4);
        if (len > 0 && len <= size - i) {
          payload_size = len;
        }
      }
      DecodedFrame out;
      out.pixels = std::make_shared<std::vector<uint8_t>>(data + i, data + i + payload_size);
      return out;
    }
  }
  return unexpected("no JPEG/PNG marker found in CDR envelope");
}

// ---------------------------------------------------------------------------
// CompressedDepthStripper
// ---------------------------------------------------------------------------

Expected<DecodedFrame> CompressedDepthStripper::decode(const DecodedFrame& input) const {
  if (input.isNull() || input.pixels->size() < 16) {
    return unexpected("data too small for compressedDepth");
  }
  const auto* data = input.pixels->data();
  auto size = input.pixels->size();

  for (size_t i = 0; i + 7 < size; ++i) {
    if (data[i] == 0x89 && data[i + 1] == 0x50 && data[i + 2] == 0x4E && data[i + 3] == 0x47 && data[i + 4] == 0x0D &&
        data[i + 5] == 0x0A && data[i + 6] == 0x1A && data[i + 7] == 0x0A) {
      DecodedFrame out;
      out.pixels = std::make_shared<std::vector<uint8_t>>(data + i, data + size);
      return out;
    }
  }
  return unexpected("no PNG signature found in compressedDepth");
}

// ---------------------------------------------------------------------------
// JpegCodec
// ---------------------------------------------------------------------------

JpegCodec::JpegCodec() : tj_(tjInitDecompress()) {}

JpegCodec::~JpegCodec() {
  if (tj_ != nullptr) {
    tjDestroy(tj_);
  }
}

Expected<DecodedFrame> JpegCodec::decode(const DecodedFrame& input) const {
  if (tj_ == nullptr) {
    return unexpected("turbojpeg not initialized");
  }
  if (input.isNull()) {
    return unexpected("empty input");
  }
  const auto* data = input.pixels->data();
  auto size = static_cast<unsigned long>(input.pixels->size());

  int width = 0;
  int height = 0;
  int subsamp = 0;
  if (tjDecompressHeader2(static_cast<tjhandle>(tj_), const_cast<uint8_t*>(data), size, &width, &height, &subsamp) !=
      0) {
    return unexpected(std::string("JPEG header failed: ") + tjGetErrorStr());
  }

  auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  if (tjDecompress2(
          static_cast<tjhandle>(tj_), const_cast<uint8_t*>(data), size, pixels->data(), width, width * 3, height,
          TJPF_RGB, TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
    return unexpected(std::string("JPEG decode failed: ") + tjGetErrorStr());
  }

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = PixelFormat::kRGB888;
  return frame;
}

// ---------------------------------------------------------------------------
// PngCodec
// ---------------------------------------------------------------------------

namespace {

struct PngReadCtx {
  const uint8_t* data;
  size_t size;
  size_t offset;
};

void pngReadCb(png_structp png, png_bytep out, png_size_t count) {
  auto* ctx = static_cast<PngReadCtx*>(png_get_io_ptr(png));
  if (ctx->offset + count > ctx->size) {
    png_error(png, "read past end");
    return;
  }
  std::memcpy(out, ctx->data + ctx->offset, count);
  ctx->offset += count;
}

}  // namespace

Expected<DecodedFrame> PngCodec::decode(const DecodedFrame& input) const {
  if (input.isNull() || input.pixels->size() < 8) {
    return unexpected("empty input");
  }
  const auto* data = input.pixels->data();
  auto size = input.pixels->size();

  if (png_sig_cmp(data, 0, 8) != 0) {
    return unexpected("not a PNG");
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

  PngReadCtx ctx{data, size, 0};
  png_set_read_fn(png, &ctx, pngReadCb);
  png_read_info(png, info);

  auto width = static_cast<int>(png_get_image_width(png, info));
  auto height = static_cast<int>(png_get_image_height(png, info));
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  bool is_mono16 = (color_type == PNG_COLOR_TYPE_GRAY && bit_depth == 16);

  if (!is_mono16) {
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
  }

  PixelFormat fmt;
  int channels;
  if (is_mono16) {
    fmt = PixelFormat::kMono16;
    channels = 2;
  } else {
    bool has_alpha = (png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA) != 0;
    channels = has_alpha ? 4 : 3;
    fmt = has_alpha ? PixelFormat::kRGBA8888 : PixelFormat::kRGB888;
  }

  auto row_bytes = static_cast<size_t>(width) * static_cast<size_t>(channels);
  auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(height) * row_bytes);
  std::vector<png_bytep> rows(static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    rows[static_cast<size_t>(y)] = pixels->data() + static_cast<size_t>(y) * row_bytes;
  }

  png_read_image(png, rows.data());
  png_destroy_read_struct(&png, &info, nullptr);

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = width;
  frame.height = height;
  frame.format = fmt;
  return frame;
}

// ---------------------------------------------------------------------------
// DepthToGrayscale
// ---------------------------------------------------------------------------

Expected<DecodedFrame> DepthToGrayscale::decode(const DecodedFrame& input) const {
  if (input.isNull()) {
    return unexpected("empty depth data");
  }
  if (input.format != PixelFormat::kMono16 || input.width == 0 || input.height == 0) {
    return unexpected("expected mono16 input with valid dimensions");
  }

  auto pixel_count = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);
  if (input.pixels->size() < pixel_count * 2) {
    return unexpected("depth buffer too small for dimensions");
  }
  const auto* src = reinterpret_cast<const uint16_t*>(input.pixels->data());

  uint16_t min_val = 65535;
  uint16_t max_val = 0;
  for (size_t i = 0; i < pixel_count; ++i) {
    if (src[i] > 0) {
      min_val = std::min(min_val, src[i]);
      max_val = std::max(max_val, src[i]);
    }
  }
  if (max_val <= min_val) {
    max_val = min_val + 1;
  }

  auto out = std::make_shared<std::vector<uint8_t>>(pixel_count * 3);
  float scale = 255.0f / static_cast<float>(max_val - min_val);
  for (size_t i = 0; i < pixel_count; ++i) {
    uint8_t val = 0;
    if (src[i] > 0) {
      val = static_cast<uint8_t>(std::clamp(static_cast<float>(src[i] - min_val) * scale, 0.0f, 255.0f));
    }
    (*out)[i * 3 + 0] = val;
    (*out)[i * 3 + 1] = val;
    (*out)[i * 3 + 2] = val;
  }

  DecodedFrame frame;
  frame.pixels = std::move(out);
  frame.width = input.width;
  frame.height = input.height;
  frame.format = PixelFormat::kRGB888;
  return frame;
}

// ---------------------------------------------------------------------------
// SegmentationPalette
// ---------------------------------------------------------------------------

Expected<DecodedFrame> SegmentationPalette::decode(const DecodedFrame& input) const {
  if (input.isNull() || input.width == 0 || input.height == 0) {
    return unexpected("empty segmentation data");
  }
  if (input.format != PixelFormat::kMono8) {
    return unexpected("expected mono8 input for segmentation palette");
  }
  auto pixel_count_check = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);
  if (input.pixels->size() < pixel_count_check) {
    return unexpected("segmentation buffer too small for dimensions");
  }

  static const auto palette = []() {
    std::array<std::array<uint8_t, 3>, 256> pal{};
    pal[0] = {0, 0, 0};
    for (int i = 1; i < 256; ++i) {
      int hue = (i * 37) % 256;
      int sector = hue / 43;
      int frac = (hue % 43) * 6;
      int r = 0;
      int g = 0;
      int b = 0;
      switch (sector) {
        case 0:
          r = 255;
          g = frac;
          break;
        case 1:
          r = 255 - frac;
          g = 255;
          break;
        case 2:
          g = 255;
          b = frac;
          break;
        case 3:
          g = 255 - frac;
          b = 255;
          break;
        case 4:
          r = frac;
          b = 255;
          break;
        default:
          r = 255;
          b = 255 - frac;
          break;
      }
      pal[static_cast<size_t>(i)] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    return pal;
  }();

  auto pixel_count = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);
  auto out = std::make_shared<std::vector<uint8_t>>(pixel_count * 3);
  for (size_t i = 0; i < pixel_count; ++i) {
    auto id = (*input.pixels)[i];
    (*out)[i * 3 + 0] = palette[id][0];
    (*out)[i * 3 + 1] = palette[id][1];
    (*out)[i * 3 + 2] = palette[id][2];
  }

  DecodedFrame frame;
  frame.pixels = std::move(out);
  frame.width = input.width;
  frame.height = input.height;
  frame.format = PixelFormat::kRGB888;
  return frame;
}

// ---------------------------------------------------------------------------
// Pipeline builders
// ---------------------------------------------------------------------------

std::unique_ptr<CodecPipeline> makeJpegPipeline() {
  auto p = std::make_unique<CodecPipeline>();
  p->addStage(std::make_unique<JpegCodec>());
  return p;
}

std::unique_ptr<CodecPipeline> makeCdrJpegPipeline() {
  auto p = std::make_unique<CodecPipeline>();
  p->addStage(std::make_unique<CdrImageStripper>());
  p->addStage(std::make_unique<JpegCodec>());
  return p;
}

std::unique_ptr<CodecPipeline> makeDepthPipeline() {
  auto p = std::make_unique<CodecPipeline>();
  p->addStage(std::make_unique<CompressedDepthStripper>());
  p->addStage(std::make_unique<PngCodec>());
  p->addStage(std::make_unique<DepthToGrayscale>());
  return p;
}

}  // namespace PJ
