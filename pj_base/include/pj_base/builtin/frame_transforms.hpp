/**
 * @file frame_transforms.hpp
 * @brief Time-stamped 3D transforms between named reference frames.
 *
 * FrameTransforms is a small owned builtin for TF-style frame relationships.
 * It stores strings and scalar geometry directly; no BufferAnchor is needed.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Vector in 2D space (a direction or an extent, e.g. a grid's cell size).
struct Vector2 {
  double x = 0.0;
  double y = 0.0;
  bool operator==(const Vector2&) const = default;
};

/// Translation vector in 3D space.
struct Vector3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  bool operator==(const Vector3&) const = default;
};

/// Unit quaternion representing a 3D rotation, written (x, y, z, w) with w the
/// scalar part (Hamilton convention): v' = q * v * conjugate(q).
struct Quaternion {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;
  bool operator==(const Quaternion&) const = default;
};

/// Rigid transform in 3D space: position + orientation, in metres, in a
/// right-handed frame. p_frame = R(orientation) * p_local + position.
/// Used by Mesh3D, OccupancyGrid, SceneEntities, and other types that
/// place data in a frame of reference.
struct Pose {
  Vector3 position;
  Quaternion orientation;
  bool operator==(const Pose&) const = default;
};

/// Places the child frame in its parent: a point known in the child frame is
/// p_parent = R(rotation) * p_child + translation.
struct FrameTransform {
  Timestamp timestamp_ns = 0;
  std::string parent_frame_id;
  std::string child_frame_id;
  Vector3 translation;
  Quaternion rotation;
  bool operator==(const FrameTransform&) const = default;
};

/// Batch of frame transforms carried by one source payload.
struct FrameTransforms {
  std::vector<FrameTransform> transforms;
  bool operator==(const FrameTransforms&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return transforms.empty();
  }
};

}  // namespace sdk
}  // namespace PJ
