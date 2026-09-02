/**
 * @file grid_map.hpp
 * @brief 2D grid of per-cell channels (elevation maps, layered costmaps) placed in world coordinates.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/frame_transforms.hpp"  // for Pose, Vector2
#include "pj_base/builtin/point_cloud.hpp"       // for PointField (shared channel descriptor)
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// 2D grid whose cells carry named channels — the layered, generic-valued
/// sibling of OccupancyGrid: elevation maps, multi-layer costmaps, terrain
/// classification, anything a `grid_map_msgs/GridMap` or `foxglove.Grid` holds.
///
/// Cells are fixed-size records of `cell_stride` bytes stored densely in
/// row-major order (x / column varies fastest), `row_stride` bytes per row, so
/// the byte offset of cell (column c, row r) is `r*row_stride + c*cell_stride`
/// and `data.size()` must be at least `row_count * row_stride`. `fields`
/// describes the channels inside one record with the same `PointField` model
/// PointCloud and VoxelGrid use; a NaN in a float channel means "no data" for
/// that cell, integer channels have no empty sentinel. The packed cell layout
/// is the one `foxglove.Grid` uses (row-major records, x fastest), so a parser
/// can hand that message's `data` over as a zero-copy view; its header and
/// field descriptors still need conversion (different wire field numbers, a
/// `PackedElementField` datatype numbering that differs from `PointField`'s,
/// and `count` always 1). Producers with another layout (grid_map's
/// column-major ring buffer) transcode once at the boundary.
///
/// Cell (c, r) has its **center** at, in `frame_id`:
///   origin ∘ ((c + .5)*cell_size.x, (r + .5)*cell_size.y, 0)
/// where `origin` is the corner of cell (0,0) and the grid lies in the
/// origin's local xy-plane. Which channel is height, which is color, and how
/// values map to a colormap are viewer-side choices; the type carries no
/// styling.
///
/// Channel-name conventions consumers may rely on: `elevation` is the
/// conventional height channel; `red`, `green`, `blue`, `alpha` are the RGBA
/// color channels, as in `foxglove.Grid`.
///
/// `anchor` keeps the underlying buffer alive — `data` may be a view into the
/// source payload or into a freshly allocated buffer; consumers don't need to
/// know which.
struct GridMap {
  Timestamp timestamp_ns = 0;
  std::string frame_id;       ///< Source coordinate frame; required for 3D TF resolution.
  Pose origin;                ///< Corner of cell (0,0) in `frame_id`; the grid lies in its local xy-plane.
  Vector2 cell_size;          ///< Metric cell size along local x (columns) and y (rows), meters.
  uint32_t column_count = 0;  ///< Cells along x (fastest-varying).
  uint32_t row_count = 0;     ///< Cells along y.
  uint32_t cell_stride = 0;   ///< Bytes per cell record (>= the largest field end: offset + size * count).
  uint32_t row_stride = 0;    ///< Bytes per row (>= column_count * cell_stride).
  std::vector<PointField> fields;
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
