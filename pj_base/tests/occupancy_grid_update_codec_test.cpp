// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/occupancy_grid_update_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace PJ {
namespace {

using sdk::OccupancyGridUpdate;

TEST(OccupancyGridUpdateCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaOccupancyGridUpdate, "PJ.OccupancyGridUpdate");
}

TEST(OccupancyGridUpdateCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeOccupancyGridUpdate(nullptr, 0).has_value());
}

TEST(OccupancyGridUpdateCodecTest, RoundTripPatch) {
  OccupancyGridUpdate in;
  in.timestamp_ns = 7'000'000'000LL;
  in.frame_id = "map";
  in.x = 10;
  in.y = 20;
  in.width = 3;
  in.height = 2;
  const std::vector<uint8_t> cells = {0, 50, 100, 0xFF /* -1 unknown */, 25, 75};
  in.data = Span<const uint8_t>(cells.data(), cells.size());

  const auto bytes = serializeOccupancyGridUpdate(in);
  auto out = deserializeOccupancyGridUpdate(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->x, in.x);
  EXPECT_EQ(out->y, in.y);
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  ASSERT_EQ(out->data.size(), cells.size());
  EXPECT_EQ(std::memcmp(out->data.data(), cells.data(), cells.size()), 0);
}

TEST(OccupancyGridUpdateCodecTest, NegativeOffsetsRoundTrip) {
  OccupancyGridUpdate in;
  in.frame_id = "map";
  in.x = -5;
  in.y = -1;
  in.width = 1;
  in.height = 1;
  const std::vector<uint8_t> cells = {42};
  in.data = Span<const uint8_t>(cells.data(), cells.size());

  const auto bytes = serializeOccupancyGridUpdate(in);
  auto out = deserializeOccupancyGridUpdate(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->x, -5);
  EXPECT_EQ(out->y, -1);
}

}  // namespace
}  // namespace PJ
