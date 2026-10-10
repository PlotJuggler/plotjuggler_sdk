/**
 * @file depth_image_fields.hpp
 * @brief `FieldTable` specialization for `depth_image.hpp`'s `DepthImage`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

#include "pj_base/builtin/depth_image.hpp"
#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/point_cloud.hpp"

namespace PJ::sdk {

namespace detail {

/// Pixel datatype of a `DepthImage::encoding` value, or `kUnknown` when the
/// string is not one of the two encodings `depth_image.hpp`'s doc comment
/// documents ("16UC1" — millimeters as uint16 — or "32FC1" — meters as
/// float32). `DepthImage::encoding` is an open string (like `Image::encoding`),
/// so an unrecognized value is expected, not an error.
[[nodiscard]] constexpr PointField::Datatype depthImageDatatype(std::string_view encoding) noexcept {
  if (encoding == "16UC1") {
    return PointField::Datatype::kUint16;
  }
  if (encoding == "32FC1") {
    return PointField::Datatype::kFloat32;
  }
  return PointField::Datatype::kUnknown;
}

}  // namespace detail

/// `kBuffer` descriptor for `DepthImage::data`. Unlike `Image`, `DepthImage`
/// has no `row_step` member, so `buffer()` derives it as `width * bpp` (no
/// row padding representable) whenever `bpp` (from
/// `bytesPerElement(detail::depthImageDatatype(encoding))`) is known; an unrecognized
/// encoding reports `record_step = row_step = 0` and `bytes` alone (its full
/// span) is the payload — same convention as `imageDataField()`
/// (`image_fields.hpp`). `channels` always carries exactly one entry naming
/// the encoding string. `buffer_assign()` is `detail::assignPayloadBytes`.
[[nodiscard]] constexpr FieldDescriptor depthImageDataField(std::string_view name) {
  FieldDescriptor d{};
  d.name = name;
  d.kind = FieldKind::kBuffer;
  d.buffer = [](const void* p) -> BufferLayout {
    const auto& image = *static_cast<const DepthImage*>(p);
    const PointField::Datatype datatype = detail::depthImageDatatype(image.encoding);
    const uint32_t bpp = bytesPerElement(datatype);
    BufferLayout layout;
    layout.bytes = image.data;
    layout.record_step = bpp;
    layout.record_count = bpp == 0 ? 0 : static_cast<uint64_t>(image.width) * static_cast<uint64_t>(image.height);
    layout.row_step = bpp == 0 ? 0 : image.width * bpp;
    layout.is_bigendian = false;  // DepthImage carries no endianness member; native in-memory doubles/ints.
    layout.channels.push_back(
        BufferLayout::Channel{
            .name = image.encoding,
            .offset = 0,
            .datatype = static_cast<uint8_t>(datatype),
            .count = bpp == 0 ? 0 : uint32_t{1}});
    return layout;
  };
  d.buffer_assign = &detail::assignPayloadBytes<DepthImage>;
  return d;
}

/// Fields of `DepthImage`: every member is an ordinary field except `data`
/// (`depthImageDataField`); `anchor` is written through that descriptor's
/// `buffer_assign()` and is not listed. `K` is a fixed-size list of 9
/// numbers (`std::array<double, 9>` — see `field_table.hpp`'s `kList`
/// fixed-size case); `D` is a growable list of numbers
/// (`std::vector<double>`, size depends on `distortion_model`).
template <>
struct FieldTable<DepthImage> {
  static constexpr std::array fields{
      field<&DepthImage::width>("width"),
      field<&DepthImage::height>("height"),
      field<&DepthImage::encoding>("encoding"),
      depthImageDataField("data"),
      field<&DepthImage::K>("K"),
      field<&DepthImage::distortion_model>("distortion_model"),
      field<&DepthImage::D>("D"),
      field<&DepthImage::timestamp_ns>("timestamp_ns"),
  };
  static constexpr FieldTableView view{"DepthImage", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
