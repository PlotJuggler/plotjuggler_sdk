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

/// A valid 1x1 grid; tests append the field under test last, so a nested
/// decode failure cannot be masked by the top-level parser skipping onward.
std::vector<uint8_t> oneCellGrid() {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 5, 0);
  pb::appendVarint(bytes, 1);
  pb::appendTag(bytes, 6, 0);
  pb::appendVarint(bytes, 1);
  pb::appendTag(bytes, 7, 2);
  pb::appendLenDelim(bytes, {0});
  return bytes;
}

TEST(OccupancyGridCodecTest, DecodesOriginOrientationWithElidedZeroW) {
  std::vector<uint8_t> orientation;  // {z: 1}: w = 0 elided by a proto3 encoder
  pb::appendTag(orientation, 3, 1);
  pb::appendDouble(orientation, 1.0);
  std::vector<uint8_t> pose;
  pb::appendTag(pose, 2, 2);
  pb::appendLenDelim(pose, orientation);
  auto bytes = oneCellGrid();
  pb::appendTag(bytes, 3, 2);
  pb::appendLenDelim(bytes, pose);

  auto out = deserializeOccupancyGrid(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->origin.orientation, (sdk::Quaternion{.x = 0.0, .y = 0.0, .z = 1.0, .w = 0.0}));

  const auto no_origin = oneCellGrid();
  out = deserializeOccupancyGrid(no_origin.data(), no_origin.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->origin, sdk::Pose{});  // absent origin: identity
}

TEST(OccupancyGridCodecTest, RejectsMalformedOriginPosition) {
  // position {x: <2 of 8 bytes>}, then 0x00, which a second skip reads as an
  // empty length-delimited field.
  const std::vector<uint8_t> pose = {0x0a, 0x03, 0x09, 0x00, 0x00, 0x00};
  auto bytes = oneCellGrid();
  pb::appendTag(bytes, 3, 2);
  pb::appendLenDelim(bytes, pose);
  EXPECT_FALSE(deserializeOccupancyGrid(bytes.data(), bytes.size()).has_value());
}

TEST(OccupancyGridCodecTest, RejectsMalformedTimestamp) {
  // seconds holding an over-long varint (10 continuation bytes), then 0x00,
  // which a second skip reads as a varint.
  std::vector<uint8_t> timestamp = {0x08};
  timestamp.insert(timestamp.end(), 10, 0xFF);
  timestamp.push_back(0x00);
  auto bytes = oneCellGrid();
  pb::appendTag(bytes, 1, 2);
  pb::appendLenDelim(bytes, timestamp);
  EXPECT_FALSE(deserializeOccupancyGrid(bytes.data(), bytes.size()).has_value());
}

TEST(OccupancyGridCodecTest, RejectsFieldNumbersOutsideProtobufRange) {
  auto decode_with_field = [](uint64_t field) {
    auto bytes = oneCellGrid();
    pb::appendVarint(bytes, (field << 3U) | 2U);  // length-delimited
    pb::appendString(bytes, "evil");
    return deserializeOccupancyGrid(bytes.data(), bytes.size());
  };
  constexpr uint64_t kMaxField = (uint64_t{1} << 29U) - 1;
  ASSERT_TRUE(decode_with_field(kMaxField).has_value());  // unknown, skipped
  EXPECT_FALSE(decode_with_field(kMaxField + 1).has_value());
  EXPECT_FALSE(decode_with_field(0).has_value());
  const auto aliased = decode_with_field((uint64_t{1} << 32U) + 2);  // truncated, it would be frame_id
  EXPECT_FALSE(aliased.has_value()) << "decoded frame_id '" << (aliased ? aliased->frame_id : "") << "'";
}

}  // namespace
}  // namespace PJ
