/**
 * @file depth_image_utils.hpp
 * @brief Free-function helpers that derive conventional matrices (R, P)
 *        from sdk::DepthImage's intrinsics.
 *
 * The DepthImage struct stores K and the distortion description only;
 * R (rectification rotation) and P (3×4 projection matrix) are derivable
 * from those when the image is rectified. Consumers that prefer to read
 * R/P pre-built call these helpers; consumers that go directly to K
 * ignore this header.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/depth_image.hpp"

namespace PJ {
namespace sdk {

/// Rectification rotation. For a DepthImage with empty distortion_model
/// (i.e. rectified) this returns the identity rotation. Unrectified
/// depth has no canonical rectification rotation — the caller has the
/// external knowledge — so the same identity is returned as a sensible
/// default; treat the result as meaningful only when distortion_model
/// is empty.
[[nodiscard]] inline std::array<double, 9> rectificationRotation(const DepthImage& /*img*/) noexcept {
  return {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
}

/// 3×4 row-major projection matrix derived from K. Equals [K | 0_3]:
///
///   P = [ fx   0   cx   0 ]
///       [  0  fy   cy   0 ]
///       [  0   0    1   0 ]
///
/// Meaningful when the image is rectified (distortion_model empty);
/// otherwise it represents the projection without the rectification
/// step the caller would need to apply separately.
[[nodiscard]] inline std::array<double, 12> projectionMatrix(const DepthImage& img) noexcept {
  const auto& k = img.K;
  return {
      k[0], k[1], k[2], 0.0,  //
      k[3], k[4], k[5], 0.0,  //
      k[6], k[7], k[8], 0.0,
  };
}

}  // namespace sdk
}  // namespace PJ
