// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/compressed_point_cloud_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::CompressedPointCloud;
namespace pb = ::PJ::test_pb;

TEST(CompressedPointCloudCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaCompressedPointCloud, "PJ.CompressedPointCloud");
}

TEST(CompressedPointCloudCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeCompressedPointCloud(nullptr, 0).has_value());
}

TEST(CompressedPointCloudCodecTest, RoundTripDracoPayload) {
  CompressedPointCloud in;
  in.timestamp_ns = -123'456'789LL;
  in.frame_id = "lidar_link";
  in.format = "draco";
  const std::vector<uint8_t> payload = {0x44, 0x52, 0x41, 0x43, 0x4F, 0x01, 0x02, 0x03, 0x04};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializeCompressedPointCloud(in);
  auto out = deserializeCompressedPointCloud(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->format, in.format);
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

}  // namespace
}  // namespace PJ
