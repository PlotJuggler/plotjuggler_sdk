/**
 * @file point_cloud.hpp
 * @brief Packed point cloud with per-channel field layout.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Description of one channel inside a packed point cloud (x, y, z, intensity,
/// rgb, ring, time, …). Mirrors the shape of sensor_msgs/PointField but the
/// type is canonical PJ vocabulary, not a ROS-specific enum.
struct PointField {
  enum class Datatype : uint8_t {
    kUnknown = 0,
    kInt8 = 1,
    kUint8 = 2,
    kInt16 = 3,
    kUint16 = 4,
    kInt32 = 5,
    kUint32 = 6,
    kFloat32 = 7,
    kFloat64 = 8,
  };

  std::string name;
  uint32_t offset = 0;  ///< Byte offset of this field within a single point.
  Datatype datatype = Datatype::kUnknown;
  uint32_t count = 1;  ///< Number of elements of `datatype` (typically 1).
};

/// Bytes per element for a given PointField datatype. Returns 0 for kUnknown.
[[nodiscard]] constexpr uint32_t bytesPerElement(PointField::Datatype dt) noexcept {
  switch (dt) {
    case PointField::Datatype::kInt8:
    case PointField::Datatype::kUint8:
      return 1;
    case PointField::Datatype::kInt16:
    case PointField::Datatype::kUint16:
      return 2;
    case PointField::Datatype::kInt32:
    case PointField::Datatype::kUint32:
    case PointField::Datatype::kFloat32:
      return 4;
    case PointField::Datatype::kFloat64:
      return 8;
    case PointField::Datatype::kUnknown:
      return 0;
  }
  return 0;
}

/// Packed point cloud. The `data` buffer holds `width * height` points, each
/// occupying `point_step` bytes laid out per `fields`. `is_dense=false` means
/// some points may be invalid (typically NaN-filled).
struct PointCloud {
  uint32_t width = 0;
  uint32_t height = 1;
  uint32_t point_step = 0;  ///< Bytes per point.
  uint32_t row_step = 0;    ///< Bytes per row (= point_step * width when no padding).
  bool is_bigendian = false;
  bool is_dense = true;
  std::string frame_id;  ///< Source coordinate frame; required for 3D TF resolution.
  std::vector<PointField> fields;
  Span<const uint8_t> data;
  BufferAnchor anchor;
  Timestamp timestamp_ns = 0;
};

}  // namespace sdk
}  // namespace PJ
