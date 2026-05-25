// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/point_cloud_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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

}  // namespace
}  // namespace PJ
