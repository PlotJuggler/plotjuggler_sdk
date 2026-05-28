/**
 * @file camera_info.hpp
 * @brief Pinhole camera calibration (intrinsics + distortion) for a camera frame.
 *
 * CameraInfo is a small owned builtin: it carries calibration matrices and
 * distortion coefficients directly, with no byte blob, so no BufferAnchor is
 * needed. It is correlated to an image / depth topic by convention — a consumer
 * pairs `<ns>/camera_info` with `<ns>/image_raw` — so the canonical object
 * itself carries no topic linkage. Sub-window fields (binning, ROI) are
 * intentionally omitted; they are additive later if a consumer needs them.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Pinhole camera calibration. Mirrors the calibration payload of
/// sensor_msgs/CameraInfo (K/D/R/P + distortion model), expressed as canonical
/// PJ vocabulary rather than a ROS-specific type. Matrices are row-major.
struct CameraInfo {
  Timestamp timestamp_ns = 0;
  std::string frame_id;          ///< Camera optical frame.
  uint32_t width = 0;            ///< Image width in pixels.
  uint32_t height = 0;           ///< Image height in pixels.
  std::string distortion_model;  ///< e.g. "plumb_bob", "rational_polynomial", "equidistant".
  std::vector<double> D;         ///< Distortion coefficients; size depends on the model.
  std::array<double, 9> K{};     ///< 3x3 intrinsics [fx 0 cx; 0 fy cy; 0 0 1].
  std::array<double, 9> R{};     ///< 3x3 rectification (identity for monocular).
  std::array<double, 12> P{};    ///< 3x4 projection / camera matrix.

  bool operator==(const CameraInfo&) const = default;
};

}  // namespace sdk
}  // namespace PJ
