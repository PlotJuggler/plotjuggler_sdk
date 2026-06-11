/**
 * @file poses_in_frame.hpp
 * @brief Array of poses in a single reference frame at one instant.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "pj_base/builtin/frame_transforms.hpp"  // Pose / Vector3 / Quaternion
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// An array of poses in a single reference frame at one instant
/// (geometry_msgs/PoseArray, foxglove.PosesInFrame). Pure data: rendering
/// style (arrow vs triad, size, color) is chosen by the viewer at draw time.
struct PosesInFrame {
  /// Acquisition time in nanoseconds (0 when the source had no timestamp).
  Timestamp timestamp_ns = 0;
  /// Frame all poses are expressed in.
  std::string frame_id;
  /// The poses. Order is meaningful only to the producer.
  std::vector<Pose> poses;
};

}  // namespace sdk
}  // namespace PJ
