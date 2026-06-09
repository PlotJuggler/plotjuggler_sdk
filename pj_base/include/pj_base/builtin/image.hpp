/**
 * @file image.hpp
 * @brief Image built-in object: raw or compressed, identified by an
 *        encoding string.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Optional vocabulary of canonical image encoding strings.
///
/// `Image::encoding` is an open std::string; producers and consumers are not
/// required to use the values listed here. This enum documents the encodings
/// the SDK recognizes and provides round-trip string conversion helpers.
// NOLINTBEGIN(readability-identifier-naming)
// Enumerator names are deliberately the canonical encoding strings, not the
// project's kCamelCase constants.
enum class CommonImageEncoding : uint8_t {
  // Raw pixel layouts
  rgb8,    ///< 3 bytes/pixel, R-G-B order.
  rgba8,   ///< 4 bytes/pixel, R-G-B-A order.
  bgr8,    ///< 3 bytes/pixel, B-G-R order.
  bgra8,   ///< 4 bytes/pixel, B-G-R-A order.
  mono8,   ///< 1 byte/pixel, grayscale.
  mono16,  ///< 2 bytes/pixel, grayscale.
  // Compressed wire formats
  jpeg,
  png,
  qoi,
  // ROS-specific
  compressedDepth,  ///< PNG payload + depth quantization range in extras.
};
// NOLINTEND(readability-identifier-naming)

/// Canonical string for an encoding value.
[[nodiscard]] inline constexpr std::string_view name(CommonImageEncoding e) noexcept {
  switch (e) {
    case CommonImageEncoding::rgb8:
      return "rgb8";
    case CommonImageEncoding::rgba8:
      return "rgba8";
    case CommonImageEncoding::bgr8:
      return "bgr8";
    case CommonImageEncoding::bgra8:
      return "bgra8";
    case CommonImageEncoding::mono8:
      return "mono8";
    case CommonImageEncoding::mono16:
      return "mono16";
    case CommonImageEncoding::jpeg:
      return "jpeg";
    case CommonImageEncoding::png:
      return "png";
    case CommonImageEncoding::qoi:
      return "qoi";
    case CommonImageEncoding::compressedDepth:
      return "compressedDepth";
  }
  return "";
}

/// Parse an encoding string into the enum. Returns nullopt if the string is
/// not one of the documented vocabulary entries.
[[nodiscard]] inline constexpr std::optional<CommonImageEncoding> parseImageEncoding(std::string_view s) noexcept {
  if (s == "rgb8") {
    return CommonImageEncoding::rgb8;
  }
  if (s == "rgba8") {
    return CommonImageEncoding::rgba8;
  }
  if (s == "bgr8") {
    return CommonImageEncoding::bgr8;
  }
  if (s == "bgra8") {
    return CommonImageEncoding::bgra8;
  }
  if (s == "mono8") {
    return CommonImageEncoding::mono8;
  }
  if (s == "mono16") {
    return CommonImageEncoding::mono16;
  }
  if (s == "jpeg") {
    return CommonImageEncoding::jpeg;
  }
  if (s == "png") {
    return CommonImageEncoding::png;
  }
  if (s == "qoi") {
    return CommonImageEncoding::qoi;
  }
  if (s == "compressedDepth") {
    return CommonImageEncoding::compressedDepth;
  }
  return std::nullopt;
}

/// Image. The `encoding` string distinguishes raw pixel layouts from
/// compressed wire formats; the producer decides which.
///
///   - Raw encodings: "rgb8", "rgba8", "bgr8", "bgra8", "mono8", "mono16".
///     `data` is `row_step * height` bytes laid out per the encoding.
///     `row_step` may exceed `width * bytes_per_pixel(encoding)` when the
///     wire format includes per-row padding; consumers must honor it.
///     `is_bigendian` is meaningful only for mono16 (and any future
///     multi-byte raw encoding).
///
///   - Compressed encodings: "jpeg", "png", "qoi". `data` carries the
///     compressed payload; consumers run the appropriate codec to obtain
///     decoded pixels. `row_step` and `is_bigendian` are unused.
///
///   - Compressed depth: "compressedDepth" (ROS-style). `data` carries a
///     PNG payload that decodes to grayscale; `compressed_depth_min` and
///     `compressed_depth_max` carry the quantization range needed to map
///     the grayscale back to depth values.
///
/// `CommonImageEncoding` above defines the documented vocabulary of canonical
/// encoding strings, with helpers to parse and emit them.
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector (when the wire format required conversion); consumers
/// don't need to know which.
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::string encoding;       ///< raw or compressed; see vocabulary above.
  uint32_t row_step = 0;      ///< raw encodings only; 0 for compressed.
  bool is_bigendian = false;  ///< mono16 only.
  Span<const uint8_t> data;
  BufferAnchor anchor;

  /// ROS compressedDepth metadata: depth-quantization range used after
  /// PNG decoding to map grayscale back to depth values. Both nullopt for
  /// regular images.
  std::optional<float> compressed_depth_min;
  std::optional<float> compressed_depth_max;

  Timestamp timestamp_ns = 0;

  /// Source coordinate frame (ROS sensor_msgs/Image and foxglove.CompressedImage
  /// both carry it). Lets a consumer match the image to the CameraInfo of the same
  /// frame_id (calibration / native resolution), e.g. to rectify lens distortion so
  /// 2D annotations align with the image. Empty when the producer has no frame
  /// information.
  std::string frame_id;
};

}  // namespace sdk
}  // namespace PJ
