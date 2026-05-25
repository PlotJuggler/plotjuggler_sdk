/**
 * @file occupancy_grid.hpp
 * @brief 2D metric occupancy grid placed in world coordinates.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/frame_transforms.hpp"  // for Pose
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// 2D metric occupancy grid placed in world coordinates.
///
/// The grid lies in the local xy-plane of `origin`, with cell (0, 0) at the
/// origin's translation. Each cell holds a signed 8-bit integer:
///   -1     : unknown / no data
///   0..100 : probability of occupied, in percent
///   other  : reserved
///
/// Cell (row r, column c) is at world position
///   origin + (c * resolution, r * resolution, 0) in `frame_id`.
/// Layout is row-major: data[r * width + c]. `data.size()` must equal
/// `width * height`.
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector; consumers don't need to know which.
struct OccupancyGrid {
  Timestamp timestamp_ns = 0;
  std::string frame_id;
  Pose origin;
  double resolution = 0.0;  ///< Cell size in meters (square cells).
  uint32_t width = 0;       ///< Number of columns (cells along x).
  uint32_t height = 0;      ///< Number of rows (cells along y).
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
