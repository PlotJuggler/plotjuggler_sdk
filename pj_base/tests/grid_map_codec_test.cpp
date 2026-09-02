// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/grid_map_codec.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace PJ {
namespace {

using sdk::GridMap;
using sdk::PointField;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// 3 columns x 2 rows, two float32 channels per cell (elevation, cost), row-major.
// Cell (2,1) has a NaN elevation: the "no data" sentinel must survive the wire.
GridMap makeGrid(std::vector<uint8_t>& storage) {
  GridMap grid;
  grid.timestamp_ns = 42'000'000'000LL;
  grid.frame_id = "odom";
  grid.origin.position = {.x = 1.0, .y = -2.0, .z = 0.5};
  grid.origin.orientation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
  grid.cell_size = {.x = 0.02, .y = 0.05};
  grid.column_count = 3;
  grid.row_count = 2;
  grid.cell_stride = 8;
  grid.row_stride = 24;
  grid.fields.push_back({.name = "elevation", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1});
  grid.fields.push_back({.name = "cost", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1});
  const std::vector<float> values = {
      0.10f, 1.0f, 0.20f, 2.0f, 0.30f, 3.0f,  // row 0: (elevation, cost) x 3 columns
      0.40f, 4.0f, 0.50f, 5.0f, kNaN,  6.0f,  // row 1
  };
  storage.resize(values.size() * sizeof(float));
  std::memcpy(storage.data(), values.data(), storage.size());
  grid.data = Span<const uint8_t>(storage.data(), storage.size());
  return grid;
}

float cellValue(const GridMap& grid, uint32_t column, uint32_t row, uint32_t field_offset) {
  float value = 0.0f;
  std::memcpy(
      &value, grid.data.data() + row * grid.row_stride + column * grid.cell_stride + field_offset, sizeof(float));
  return value;
}

TEST(GridMapCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaGridMap, "PJ.GridMap");
}

TEST(GridMapCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeGridMap(nullptr, 0).has_value());
}

TEST(GridMapCodecTest, RoundTrip3x2TwoChannels) {
  std::vector<uint8_t> storage;
  const GridMap in = makeGrid(storage);

  const auto bytes = serializeGridMap(in);
  auto out = deserializeGridMap(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value()) << out.error();
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->origin, in.origin);
  EXPECT_DOUBLE_EQ(out->cell_size.x, in.cell_size.x);
  EXPECT_DOUBLE_EQ(out->cell_size.y, in.cell_size.y);
  EXPECT_EQ(out->column_count, in.column_count);
  EXPECT_EQ(out->row_count, in.row_count);
  EXPECT_EQ(out->cell_stride, in.cell_stride);
  EXPECT_EQ(out->row_stride, in.row_stride);
  ASSERT_EQ(out->fields.size(), 2u);
  EXPECT_EQ(out->fields[0].name, "elevation");
  EXPECT_EQ(out->fields[0].offset, 0u);
  EXPECT_EQ(out->fields[0].datatype, PointField::Datatype::kFloat32);
  EXPECT_EQ(out->fields[0].count, 1u);
  EXPECT_EQ(out->fields[1].name, "cost");
  EXPECT_EQ(out->fields[1].offset, 4u);
  ASSERT_EQ(out->data.size(), storage.size());
  EXPECT_EQ(std::memcmp(out->data.data(), storage.data(), storage.size()), 0);

  // Decoded bytes are owned by the result, not by the wire buffer or `storage`.
  EXPECT_NE(out->data.data(), storage.data());
  EXPECT_TRUE(static_cast<bool>(out->anchor));

  EXPECT_FLOAT_EQ(cellValue(*out, 1, 0, 0), 0.20f);
  EXPECT_FLOAT_EQ(cellValue(*out, 1, 1, 4), 5.0f);
  EXPECT_TRUE(std::isnan(cellValue(*out, 2, 1, 0)));
}

TEST(GridMapCodecTest, RoundTripEmptyGridNoFields) {
  GridMap in;
  in.frame_id = "map";
  const auto bytes = serializeGridMap(in);
  auto out = deserializeGridMap(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value()) << out.error();
  EXPECT_EQ(out->frame_id, "map");
  EXPECT_TRUE(out->fields.empty());
  EXPECT_EQ(out->data.size(), 0u);
  EXPECT_EQ(out->column_count, 0u);
  EXPECT_EQ(out->row_count, 0u);
}

TEST(GridMapCodecTest, RejectsDataShorterThanDeclaredRows) {
  std::vector<uint8_t> storage;
  GridMap in = makeGrid(storage);
  in.row_count = 3;  // declares 72 bytes, only 48 present
  const auto bytes = serializeGridMap(in);
  EXPECT_FALSE(deserializeGridMap(bytes.data(), bytes.size()).has_value());
}

TEST(GridMapCodecTest, RejectsFieldPastCellStride) {
  std::vector<uint8_t> storage;
  GridMap in = makeGrid(storage);
  in.fields[1].offset = 8;  // float32 at byte 8 of an 8-byte cell
  const auto bytes = serializeGridMap(in);
  EXPECT_FALSE(deserializeGridMap(bytes.data(), bytes.size()).has_value());
}

TEST(GridMapCodecTest, RejectsZeroStrideWithCells) {
  std::vector<uint8_t> storage;
  GridMap in = makeGrid(storage);
  in.cell_stride = 0;
  const auto bytes = serializeGridMap(in);
  EXPECT_FALSE(deserializeGridMap(bytes.data(), bytes.size()).has_value());
}

TEST(GridMapCodecTest, RejectsRowStrideShorterThanColumns) {
  std::vector<uint8_t> storage;
  GridMap in = makeGrid(storage);
  in.row_stride = 16;  // 3 columns x 8 bytes need 24
  const auto bytes = serializeGridMap(in);
  EXPECT_FALSE(deserializeGridMap(bytes.data(), bytes.size()).has_value());
}

}  // namespace
}  // namespace PJ
