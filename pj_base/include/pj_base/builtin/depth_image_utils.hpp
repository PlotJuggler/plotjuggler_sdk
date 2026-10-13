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

/// Camera matrix K reduced to its inverse, built once per image so a per-pixel loop
/// checks only its own inputs.
class PinholeIntrinsics {
 public:
  /// Rejects a non-finite or singular K and nothing else: skew and projective terms
  /// are honoured, so a K that went through float32 is accepted. Distortion
  /// correction belongs to the caller.
  [[nodiscard]] static std::optional<PinholeIntrinsics> fromK(const std::array<double, 9>& k) noexcept {
    for (double value : k) {
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    const double cofactor0 = k[4] * k[8] - k[5] * k[7];
    const double cofactor1 = k[5] * k[6] - k[3] * k[8];
    const double cofactor2 = k[3] * k[7] - k[4] * k[6];
    const double det = k[0] * cofactor0 + k[1] * cofactor1 + k[2] * cofactor2;
    if (!std::isfinite(det) || det == 0) {
      return std::nullopt;
    }
    const std::array<double, 9> inverse{
        cofactor0 / det, (k[2] * k[7] - k[1] * k[8]) / det, (k[1] * k[5] - k[2] * k[4]) / det,
        cofactor1 / det, (k[0] * k[8] - k[2] * k[6]) / det, (k[2] * k[3] - k[0] * k[5]) / det,
        cofactor2 / det, (k[1] * k[6] - k[0] * k[7]) / det, (k[0] * k[4] - k[1] * k[3]) / det};
    for (double value : inverse) {
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    return PinholeIntrinsics(inverse);
  }

  /// Unproject a rectified pixel with positive metric depth: the ray K^-1 [u, v, 1]
  /// scaled so the returned point's z equals `depth_m`. Rejects a non-finite result
  /// or a ray with z <= 0.
  [[nodiscard]] std::optional<std::array<double, 3>> unproject(double u, double v, double depth_m) const noexcept {
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(depth_m) || depth_m <= 0) {
      return std::nullopt;
    }
    const double ray_z = k_inverse_[6] * u + k_inverse_[7] * v + k_inverse_[8];
    if (!std::isfinite(ray_z) || ray_z <= 0) {
      return std::nullopt;
    }
    const double scale = depth_m / ray_z;
    const double point_x = (k_inverse_[0] * u + k_inverse_[1] * v + k_inverse_[2]) * scale;
    const double point_y = (k_inverse_[3] * u + k_inverse_[4] * v + k_inverse_[5]) * scale;
    if (!std::isfinite(point_x) || !std::isfinite(point_y)) {
      return std::nullopt;
    }
    return std::array<double, 3>{point_x, point_y, depth_m};
  }

 private:
  explicit PinholeIntrinsics(const std::array<double, 9>& k_inverse) noexcept : k_inverse_(k_inverse) {}

  /// Row-major K^-1; only `fromK` builds it, so every entry is finite.
  std::array<double, 9> k_inverse_;
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
