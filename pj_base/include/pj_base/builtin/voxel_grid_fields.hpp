/**
 * @file voxel_grid_fields.hpp
 * @brief `FieldTable` specialization for `voxel_grid.hpp`'s `VoxelGrid`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/frame_transforms_fields.hpp"
#include "pj_base/builtin/point_cloud_fields.hpp"
#include "pj_base/builtin/voxel_grid.hpp"

namespace PJ::sdk {

/// `kBuffer` descriptor for `VoxelGrid::data`. `buffer()` resolves the packed
/// voxel bytes: `record_step` is `cell_stride`, `row_step` is `row_stride`, the
/// channels are read from `fields`, and `record_count` is
/// `column_count * row_count * slice_count`. A voxel is addressed as
/// `sz*slice_stride + ry*row_stride + cx*cell_stride`: `slice_stride` has no
/// `BufferLayout` slot, so read it from the sibling field. `record_count` is 0
/// when any count or `cell_stride` is 0, when `row_stride` or `slice_stride` is
/// shorter than the row or slice it steps over, or when the declared extent (the
/// last row and slice need no trailing padding) exceeds `data`. `VoxelGrid` carries no
/// endianness member, so `is_bigendian` is false. `buffer_assign()` is
/// `detail::assignPayloadBytes`.
[[nodiscard]] constexpr FieldDescriptor voxelGridDataField(std::string_view name) {
  FieldDescriptor d{};
  d.name = name;
  d.kind = FieldKind::kBuffer;
  d.buffer = [](const void* p) -> BufferLayout {
    const auto& grid = *static_cast<const VoxelGrid*>(p);
    BufferLayout layout;
    layout.bytes = grid.data;
    layout.record_step = grid.cell_stride;
    layout.row_step = grid.row_stride;
    layout.is_bigendian = false;

    const uint64_t size = grid.data.size();
    const uint64_t row_bytes = uint64_t{grid.column_count} * grid.cell_stride;
    const bool has_cells =
        grid.column_count != 0 && grid.row_count != 0 && grid.slice_count != 0 && grid.cell_stride != 0;
    // Strides at least as long as what they step over (the documented layout) keep cells
    // disjoint, so a fitting extent bounds the count product by `size` and nothing wraps.
    const uint64_t rows_bytes = uint64_t{grid.row_count} * grid.row_stride;
    const bool disjoint = grid.row_stride >= row_bytes && grid.slice_stride >= rows_bytes;
    const uint64_t slice_bytes = rows_bytes - grid.row_stride + row_bytes;  // the last row needs no padding
    const bool fits =
        has_cells && disjoint && detail::rowsFitIn(grid.slice_count, slice_bytes, grid.slice_stride, size);
    layout.record_count = fits ? uint64_t{grid.column_count} * grid.row_count * grid.slice_count : 0;

    layout.channels.reserve(grid.fields.size());
    for (const PointField& f : grid.fields) {
      layout.channels.push_back(
          BufferLayout::Channel{
              .name = f.name, .offset = f.offset, .datatype = static_cast<uint8_t>(f.datatype), .count = f.count});
    }
    return layout;
  };
  d.buffer_assign = &detail::assignPayloadBytes<VoxelGrid>;
  return d;
}

/// Fields of `VoxelGrid`: every member is an ordinary field except `data`
/// (`voxelGridDataField`); `anchor` is written through that descriptor's
/// `buffer_assign()` and is not listed. `origin` is a `Pose`, `cell_size` a
/// `Vector3` and `fields` a list of `PointField` (all linked tables).
template <>
struct FieldTable<VoxelGrid> {
  static constexpr std::array fields{
      field<&VoxelGrid::timestamp_ns>("timestamp_ns"),
      field<&VoxelGrid::frame_id>("frame_id"),
      field<&VoxelGrid::origin>("origin"),
      field<&VoxelGrid::cell_size>("cell_size"),
      field<&VoxelGrid::column_count>("column_count"),
      field<&VoxelGrid::row_count>("row_count"),
      field<&VoxelGrid::slice_count>("slice_count"),
      field<&VoxelGrid::cell_stride>("cell_stride"),
      field<&VoxelGrid::row_stride>("row_stride"),
      field<&VoxelGrid::slice_stride>("slice_stride"),
      field<&VoxelGrid::fields>("fields"),
      voxelGridDataField("data"),
  };
  static constexpr FieldTableView view{"VoxelGrid", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
