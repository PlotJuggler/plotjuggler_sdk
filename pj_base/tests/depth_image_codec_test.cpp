// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/depth_image_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::DepthImage;
namespace pb = ::PJ::test_pb;

TEST(DepthImageCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaDepthImage, "PJ.DepthImage");
}

TEST(DepthImageCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeDepthImage(nullptr, 0).has_value());
}

TEST(DepthImageCodecTest, RoundTripRectified16UC1) {
  DepthImage in;
  in.timestamp_ns = 1'234'000'000LL;
  in.width = 2;
  in.height = 2;
  in.encoding = "16UC1";
  in.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  // empty distortion_model => rectified
  const std::vector<uint8_t> payload = {0x10, 0x00, 0x20, 0x00, 0x30, 0x00, 0x40, 0x00};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializeDepthImage(in);
  auto out = deserializeDepthImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  EXPECT_EQ(out->encoding, in.encoding);
  EXPECT_EQ(out->K, in.K);
  EXPECT_TRUE(out->distortion_model.empty());
  EXPECT_TRUE(out->D.empty());
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

TEST(DepthImageCodecTest, RoundTripPlumbBobDistortion) {
  DepthImage in;
  in.width = 640;
  in.height = 480;
  in.encoding = "32FC1";
  in.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  in.distortion_model = "plumb_bob";
  in.D = {-0.1, 0.05, 0.001, -0.002, 0.0};

  const auto bytes = serializeDepthImage(in);
  auto out = deserializeDepthImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->distortion_model, "plumb_bob");
  ASSERT_EQ(out->D.size(), 5u);
  for (size_t i = 0; i < in.D.size(); ++i) {
    EXPECT_DOUBLE_EQ(out->D[i], in.D[i]) << "D[" << i << "]";
  }
}

}  // namespace
}  // namespace PJ
