// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/occupancy_grid_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::OccupancyGrid;
namespace pb = ::PJ::test_pb;

TEST(OccupancyGridCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaOccupancyGrid, "PJ.OccupancyGrid");
}

TEST(OccupancyGridCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeOccupancyGrid(nullptr, 0).has_value());
}

TEST(OccupancyGridCodecTest, RoundTrip2x3Grid) {
  OccupancyGrid in;
  in.timestamp_ns = 42'000'000'000LL;
  in.frame_id = "map";
  in.origin.position = {.x = 1.0, .y = 2.0, .z = 0.0};
  in.origin.orientation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
  in.resolution = 0.05;
  in.width = 3;
  in.height = 2;
  const std::vector<uint8_t> cells = {0, 50, 100, 0xFF /* -1 unknown */, 25, 75};
  in.data = Span<const uint8_t>(cells.data(), cells.size());

  const auto bytes = serializeOccupancyGrid(in);
  auto out = deserializeOccupancyGrid(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->origin, in.origin);
  EXPECT_DOUBLE_EQ(out->resolution, in.resolution);
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  ASSERT_EQ(out->data.size(), cells.size());
  EXPECT_EQ(std::memcmp(out->data.data(), cells.data(), cells.size()), 0);
}

}  // namespace
}  // namespace PJ
