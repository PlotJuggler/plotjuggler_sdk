/**
 * @file voxel_grid.hpp
 * @brief Dense 3D voxel grid placed in world coordinates.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/frame_transforms.hpp"  // for Pose, Vector3
#include "pj_base/builtin/point_cloud.hpp"       // for PointField (shared channel descriptor)
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Dense 3D voxel grid placed in world coordinates — the volumetric sibling of
/// OccupancyGrid, reusing PointCloud's per-channel field model (`PointField`).
///
/// The grid is a regular lattice of `column_count * row_count * slice_count`
/// voxels. Each voxel occupies `cell_stride` bytes laid out per `fields`; cells
/// are stored densely in depth-major, row-major **Z-Y-X** order (x varies
/// fastest), matching the foxglove.VoxelGrid wire layout so a parser can expose
/// `data` as a zero-copy view rather than transcoding millions of cells.
///
/// Strides (in bytes) describe the packing and allow trailing padding:
///   cell_stride  : one voxel        (>= sum of field element sizes)
///   row_stride   : one row of x     (>= column_count * cell_stride)
///   slice_stride : one z-slice      (>= row_count    * row_stride)
/// so the byte offset of voxel (cx, ry, sz) is
///   sz*slice_stride + ry*row_stride + cx*cell_stride
/// and `data.size()` must be at least `slice_count * slice_stride`.
///
/// Voxel (column cx, row ry, slice sz) has its **center** at, in `frame_id`:
///   origin ∘ ((cx + .5)*cell_size.x, (ry + .5)*cell_size.y, (sz + .5)*cell_size.z)
/// where `origin` is the grid's lower-front-left corner (the (0,0,0) corner).
///
/// Unlike the 2D OccupancyGrid (which fixes -1/0..100 occupancy semantics), the
/// per-voxel value is **generic**: `fields` declares the layout (occupancy byte,
/// RGBA, a float cost/intensity, class id, …) and the renderer decides which
/// voxels are drawn (the draw predicate / colormap is viewer-side), so one type
/// serves occupancy maps, costmaps, ESDFs, and semantic grids.
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector; consumers don't need to know which.
struct VoxelGrid {
  Timestamp timestamp_ns = 0;
  std::string frame_id;       ///< Source coordinate frame; required for 3D TF resolution.
  Pose origin;                ///< Lower-front-left (0,0,0) corner of the grid in `frame_id`.
  Vector3 cell_size;          ///< Metric voxel size along x, y, z (meters); need not be cubic.
  uint32_t column_count = 0;  ///< Voxels along x (fastest-varying).
  uint32_t row_count = 0;     ///< Voxels along y.
  uint32_t slice_count = 0;   ///< Voxels along z (depth).
  uint32_t cell_stride = 0;   ///< Bytes per voxel.
  uint32_t row_stride = 0;    ///< Bytes per row of x.
  uint32_t slice_stride = 0;  ///< Bytes per z-slice.
  std::vector<PointField> fields;
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
