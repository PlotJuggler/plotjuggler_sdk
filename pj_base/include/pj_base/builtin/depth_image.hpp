/**
 * @file depth_image.hpp
 * @brief Depth image with camera intrinsics.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Depth image. The `encoding` string carries the depth representation
/// (e.g. "16UC1" = millimeters as uint16, "32FC1" = meters as float).
///
/// Intrinsics: `K` is the 3×3 row-major intrinsic camera matrix.
///
///   K = [ fx   0   cx ]
///       [  0  fy   cy ]
///       [  0   0    1 ]
///
/// Back-projection of pixel (u, v) with depth value z:
///
///   X = (u - cx) * z / fx
///   Y = (v - cy) * z / fy
///   Z = z
///
/// Distortion: when `distortion_model` is non-empty, `D` carries the
/// distortion coefficients for that model ("plumb_bob": 5 coeffs
/// k1, k2, p1, p2, k3; "equidistant": 4 coeffs k1, k2, k3, k4). An empty
/// distortion_model means the image is rectified and `D` is unused.
///
/// Derived matrices NOT stored on the struct (caller computes from K):
///
///   R: rectification rotation. Identity for rectified images.
///   P: projection matrix [K | 0_3] (3×4) for rectified images.
///
/// Helpers in pj_base/builtin/depth_image_utils.hpp produce R
/// and P when a consumer wants them precomputed.
///
/// `anchor` keeps the underlying buffer alive — the producer may have
/// made `data` a view into the source payload (zero-copy) or into a
/// freshly allocated vector (when the wire format required conversion);
/// consumers don't need to know which.
struct DepthImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::string encoding;  ///< typically "16UC1" (mm depth) or "32FC1" (meters).
  Span<const uint8_t> data;
  BufferAnchor anchor;

  /// 3×3 row-major intrinsic camera matrix (K).
  std::array<double, 9> K{};

  /// Distortion model identifier; empty means rectified (D unused).
  std::string distortion_model;

  /// Distortion coefficients per `distortion_model`. Size depends on
  /// the model (5 for plumb_bob, 4 for equidistant, …).
  std::vector<double> D;

  Timestamp timestamp_ns = 0;
};

}  // namespace sdk
}  // namespace PJ
