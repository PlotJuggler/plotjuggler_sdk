// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/point_cloud_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::PointCloud;
using sdk::PointField;
namespace pb = ::PJ::test_pb;

TEST(PointCloudCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaPointCloud, "PJ.PointCloud");
}

TEST(PointCloudCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializePointCloud(nullptr, 0).has_value());
}

TEST(PointCloudCodecTest, RoundTripXYZIntensity) {
  PointCloud in;
  in.timestamp_ns = 5'000'000'000LL;
  in.width = 3;
  in.height = 1;
  in.point_step = 16;  // 4*float32
  in.row_step = 48;    // 3 * point_step
  in.is_bigendian = false;
  in.is_dense = true;
  in.frame_id = "velodyne";
  in.fields = {
      {.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "intensity", .offset = 12, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  std::vector<uint8_t> payload(48, 0xAB);
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializePointCloud(in);
  auto out = deserializePointCloud(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  EXPECT_EQ(out->point_step, in.point_step);
  EXPECT_EQ(out->row_step, in.row_step);
  EXPECT_FALSE(out->is_bigendian);
  EXPECT_TRUE(out->is_dense);
  EXPECT_EQ(out->frame_id, in.frame_id);
  ASSERT_EQ(out->fields.size(), 4u);
  for (size_t i = 0; i < in.fields.size(); ++i) {
    EXPECT_EQ(out->fields[i].name, in.fields[i].name);
    EXPECT_EQ(out->fields[i].offset, in.fields[i].offset);
    EXPECT_EQ(out->fields[i].datatype, in.fields[i].datatype);
    EXPECT_EQ(out->fields[i].count, in.fields[i].count);
  }
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

TEST(PointCloudCodecTest, FrameIdAbsentRoundTrips) {
  PointCloud in;
  in.width = 1;
  in.height = 1;
  in.point_step = 12;
  in.row_step = 12;

  const auto bytes = serializePointCloud(in);
  auto out = deserializePointCloud(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->frame_id.empty());
}

PointCloud smallCloud(std::vector<uint8_t>& payload) {
  PointCloud in;
  in.width = 2;
  in.point_step = 12;
  in.row_step = 24;
  in.frame_id = "lidar";
  in.fields = {
      {.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  payload.assign(24, 0);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i * 7);
  }
  in.data = Span<const uint8_t>(payload.data(), payload.size());
  return in;
}

TEST(PointCloudCodecTest, ViewAliasesAnchoredBytes) {
  std::vector<uint8_t> points;
  const PointCloud in = smallCloud(points);
  const auto wire = std::make_shared<const std::vector<uint8_t>>(serializePointCloud(in));
  auto out = deserializePointCloudView(wire->data(), wire->size(), wire);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->frame_id, in.frame_id);
  ASSERT_EQ(out->fields.size(), 3u);
  ASSERT_EQ(out->data.size(), points.size());
  EXPECT_EQ(std::memcmp(out->data.data(), points.data(), points.size()), 0);
  // No copy: the bytes live inside the wire buffer, which the result keeps alive.
  EXPECT_GE(out->data.data(), wire->data());
  EXPECT_LE(out->data.data() + out->data.size(), wire->data() + wire->size());
  EXPECT_EQ(out->anchor, wire);
}

TEST(PointCloudCodecTest, ViewWithoutAnchorCopies) {
  std::vector<uint8_t> points;
  const PointCloud in = smallCloud(points);
  const std::vector<uint8_t> wire = serializePointCloud(in);
  auto out = deserializePointCloudView(wire.data(), wire.size(), nullptr);
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->data.size(), points.size());
  EXPECT_EQ(std::memcmp(out->data.data(), points.data(), points.size()), 0);
  EXPECT_TRUE(out->data.data() < wire.data() || out->data.data() >= wire.data() + wire.size());
  EXPECT_NE(out->anchor, nullptr);
}

TEST(PointCloudCodecTest, ViewRejectsWhatTheCopyingDecoderRejects) {
  const auto garbage = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{0xFF, 0xFF, 0xFF});
  EXPECT_FALSE(deserializePointCloudView(garbage->data(), garbage->size(), garbage).has_value());
  EXPECT_FALSE(deserializePointCloudView(nullptr, 0, nullptr).has_value());
}

}  // namespace
}  // namespace PJ
