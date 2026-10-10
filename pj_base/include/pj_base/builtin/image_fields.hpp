/**
 * @file image_fields.hpp
 * @brief `FieldTable` specialization for `image.hpp`'s `Image`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/image.hpp"
#include "pj_base/builtin/point_cloud.hpp"

namespace PJ::sdk {

namespace detail {

/// Per-pixel layout of a raw (uncompressed) `Image` encoding: the datatype
/// and how many consecutive values of it make up one pixel. `bytes()` is the
/// pixel size, i.e. what `imageDataField()`'s buffer descriptor reports as
/// `record_step`. A compressed or unrecognized encoding has no static
/// per-pixel size: `kUnknown`, count 0.
struct ImagePixelLayout {
  PointField::Datatype datatype = PointField::Datatype::kUnknown;
  uint32_t count = 0;

  [[nodiscard]] constexpr uint32_t bytes() const noexcept {
    return count * bytesPerElement(datatype);
  }
};

/// Resolves `ImagePixelLayout` for the documented raw encodings ("rgb8",
/// "rgba8", "bgr8", "bgra8", "mono8", "mono16"); returns a zeroed
/// `ImagePixelLayout` for a compressed encoding ("jpeg", "png", "qoi",
/// "compressedDepth") or any string outside `CommonImageEncoding`'s
/// vocabulary — `Image::encoding` is an open string, so an unrecognized
/// value is expected, not an error.
[[nodiscard]] constexpr ImagePixelLayout imagePixelLayout(std::string_view encoding) noexcept {
  const auto parsed = parseImageEncoding(encoding);
  if (!parsed.has_value()) {
    return {};
  }
  switch (*parsed) {
    case CommonImageEncoding::rgb8:
    case CommonImageEncoding::bgr8:
      return {.datatype = PointField::Datatype::kUint8, .count = 3};
    case CommonImageEncoding::rgba8:
    case CommonImageEncoding::bgra8:
      return {.datatype = PointField::Datatype::kUint8, .count = 4};
    case CommonImageEncoding::mono8:
      return {.datatype = PointField::Datatype::kUint8, .count = 1};
    case CommonImageEncoding::mono16:
      return {.datatype = PointField::Datatype::kUint16, .count = 1};
    case CommonImageEncoding::jpeg:
    case CommonImageEncoding::png:
    case CommonImageEncoding::qoi:
    case CommonImageEncoding::compressedDepth:
      return {};
  }
  return {};
}

}  // namespace detail

/// `kBuffer` descriptor for `Image::data`. `buffer()` resolves the packed
/// pixel bytes: `record_step`/`record_count` come from `detail::imagePixelLayout(encoding)`
/// (a raw encoding's static per-pixel size times `width * height`); a
/// compressed encoding — or any string outside `CommonImageEncoding`'s
/// documented vocabulary — has no static per-pixel size, so both are
/// reported as 0 and `bytes` alone (its full span) is the payload. `row_step`
/// and `is_bigendian` are read straight from the struct (meaningful for raw
/// encodings only, per `image.hpp`'s doc comment). `channels` always carries
/// exactly one entry naming the encoding string, so a consumer that gets a
/// zeroed `record_step` still learns which codec/layout produced the bytes.
/// `buffer_assign()` is `detail::assignPayloadBytes`.
[[nodiscard]] constexpr FieldDescriptor imageDataField(std::string_view name) {
  FieldDescriptor d{};
  d.name = name;
  d.kind = FieldKind::kBuffer;
  d.buffer = [](const void* p) -> BufferLayout {
    const auto& image = *static_cast<const Image*>(p);
    const detail::ImagePixelLayout pixel = detail::imagePixelLayout(image.encoding);
    BufferLayout layout;
    layout.bytes = image.data;
    layout.record_step = pixel.bytes();
    layout.record_count =
        pixel.bytes() == 0 ? 0 : static_cast<uint64_t>(image.width) * static_cast<uint64_t>(image.height);
    layout.row_step = image.row_step;
    layout.is_bigendian = image.is_bigendian;
    layout.channels.push_back(
        BufferLayout::Channel{
            .name = image.encoding,
            .offset = 0,
            .datatype = static_cast<uint8_t>(pixel.datatype),
            .count = pixel.count});
    return layout;
  };
  d.buffer_assign = &detail::assignPayloadBytes<Image>;
  return d;
}

/// Fields of `Image`: every member is an ordinary field except `data`
/// (`imageDataField`); `anchor` is written through that descriptor's
/// `buffer_assign()` and is not listed. `compressed_depth_min`/
/// `compressed_depth_max` are `kOptionalNumber` (nullable — see
/// `field_table.hpp`).
template <>
struct FieldTable<Image> {
  static constexpr std::array fields{
      field<&Image::width>("width"),
      field<&Image::height>("height"),
      field<&Image::encoding>("encoding"),
      field<&Image::row_step>("row_step"),
      field<&Image::is_bigendian>("is_bigendian"),
      imageDataField("data"),
      field<&Image::compressed_depth_min>("compressed_depth_min"),
      field<&Image::compressed_depth_max>("compressed_depth_max"),
      field<&Image::timestamp_ns>("timestamp_ns"),
      field<&Image::frame_id>("frame_id"),
  };
  static constexpr FieldTableView view{"Image", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
