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
#include <cmath>
#include <optional>

#include "pj_base/builtin/depth_image.hpp"

namespace PJ {
namespace sdk {

/// Conventional pinhole intrinsics, validated once so a per-pixel loop checks only
/// its own inputs.
struct PinholeIntrinsics {
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;

  /// Rejects singular/nonfinite intrinsics and unsupported skew/projective terms;
  /// distortion correction belongs to the caller.
  [[nodiscard]] static std::optional<PinholeIntrinsics> fromK(const std::array<double, 9>& k) noexcept {
    for (double value : k) {
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    if (k[0] <= 0 || k[4] <= 0 || k[1] != 0 || k[3] != 0 || k[6] != 0 || k[7] != 0 || k[8] != 1) {
      return std::nullopt;
    }
    return PinholeIntrinsics{.fx = k[0], .fy = k[4], .cx = k[2], .cy = k[5]};
  }

  /// Unproject a rectified pixel with positive metric depth.
  [[nodiscard]] std::optional<std::array<double, 3>> unproject(double u, double v, double depth_m) const noexcept {
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(depth_m) || depth_m <= 0) {
      return std::nullopt;
    }
    std::array<double, 3> result{(u - cx) * depth_m / fx, (v - cy) * depth_m / fy, depth_m};
    for (double value : result) {
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    return result;
  }
};

/// One-pixel form of `PinholeIntrinsics::fromK(k)->unproject(u, v, depth_m)`; a loop
/// over an image should call `fromK` once instead.
[[nodiscard]] inline std::optional<std::array<double, 3>> unprojectPixel(
    const std::array<double, 9>& k, double u, double v, double depth_m) noexcept {
  const auto intrinsics = PinholeIntrinsics::fromK(k);
  return intrinsics ? intrinsics->unproject(u, v, depth_m) : std::nullopt;
}

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
