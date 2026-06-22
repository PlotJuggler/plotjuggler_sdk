// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/voxel_grid_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace PJ {
namespace {

using sdk::PointField;
using sdk::VoxelGrid;

TEST(VoxelGridCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaVoxelGrid, "PJ.VoxelGrid");
}

TEST(VoxelGridCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeVoxelGrid(nullptr, 0).has_value());
}

TEST(VoxelGridCodecTest, RoundTrip2x2x2OccupancyGrid) {
  VoxelGrid in;
  in.timestamp_ns = 42'000'000'000LL;
  in.frame_id = "map";
  in.origin.position = {.x = 1.0, .y = 2.0, .z = 3.0};
  in.origin.orientation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
  in.cell_size = {.x = 0.1, .y = 0.2, .z = 0.3};
  in.column_count = 2;
  in.row_count = 2;
  in.slice_count = 2;
  in.cell_stride = 1;   // one occupancy byte per voxel
  in.row_stride = 2;    // column_count * cell_stride
  in.slice_stride = 4;  // row_count * row_stride
  in.fields.push_back({.name = "occupancy", .offset = 0, .datatype = PointField::Datatype::kUint8, .count = 1});
  // 8 voxels (2*2*2) in Z-Y-X order, one byte each.
  const std::vector<uint8_t> cells = {0, 100, 50, 0xFF, 10, 20, 30, 40};
  in.data = Span<const uint8_t>(cells.data(), cells.size());

  const auto bytes = serializeVoxelGrid(in);
  auto out = deserializeVoxelGrid(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->origin, in.origin);
  EXPECT_DOUBLE_EQ(out->cell_size.x, in.cell_size.x);
  EXPECT_DOUBLE_EQ(out->cell_size.y, in.cell_size.y);
  EXPECT_DOUBLE_EQ(out->cell_size.z, in.cell_size.z);
  EXPECT_EQ(out->column_count, in.column_count);
  EXPECT_EQ(out->row_count, in.row_count);
  EXPECT_EQ(out->slice_count, in.slice_count);
  EXPECT_EQ(out->cell_stride, in.cell_stride);
  EXPECT_EQ(out->row_stride, in.row_stride);
  EXPECT_EQ(out->slice_stride, in.slice_stride);
  ASSERT_EQ(out->fields.size(), 1u);
  EXPECT_EQ(out->fields[0].name, "occupancy");
  EXPECT_EQ(out->fields[0].offset, 0u);
  EXPECT_EQ(out->fields[0].datatype, PointField::Datatype::kUint8);
  EXPECT_EQ(out->fields[0].count, 1u);
  ASSERT_EQ(out->data.size(), cells.size());
  EXPECT_EQ(std::memcmp(out->data.data(), cells.data(), cells.size()), 0);
}

TEST(VoxelGridCodecTest, RoundTripEmptyGridNoFields) {
  VoxelGrid in;
  in.frame_id = "odom";
  const auto bytes = serializeVoxelGrid(in);
  auto out = deserializeVoxelGrid(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->frame_id, "odom");
  EXPECT_TRUE(out->fields.empty());
  EXPECT_EQ(out->data.size(), 0u);
}

}  // namespace
}  // namespace PJ
