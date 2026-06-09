// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/image_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::Image;
namespace pb = ::PJ::test_pb;

TEST(ImageCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaImage, "PJ.Image");
}

TEST(ImageCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeImage(nullptr, 0).has_value());
}

TEST(ImageCodecTest, RoundTripRawRGB8) {
  Image in;
  in.timestamp_ns = 9'000'000'000LL;
  in.width = 2;
  in.height = 2;
  in.encoding = "rgb8";
  in.row_step = 6;  // 2 px * 3 bytes
  in.is_bigendian = false;
  in.frame_id = "camera_front";
  const std::vector<uint8_t> pixels = {255, 0, 0, 0, 255, 0, 0, 0, 255, 128, 128, 128};
  in.data = Span<const uint8_t>(pixels.data(), pixels.size());

  const auto bytes = serializeImage(in);
  auto out = deserializeImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  EXPECT_EQ(out->encoding, in.encoding);
  EXPECT_EQ(out->row_step, in.row_step);
  EXPECT_FALSE(out->is_bigendian);
  EXPECT_EQ(out->frame_id, "camera_front");
  EXPECT_FALSE(out->compressed_depth_min.has_value());
  EXPECT_FALSE(out->compressed_depth_max.has_value());
  ASSERT_EQ(out->data.size(), pixels.size());
  EXPECT_EQ(std::memcmp(out->data.data(), pixels.data(), pixels.size()), 0);
}

TEST(ImageCodecTest, RoundTripCompressedDepthWithRange) {
  Image in;
  in.timestamp_ns = 0;
  in.width = 320;
  in.height = 240;
  in.encoding = "compressedDepth";
  in.compressed_depth_min = 0.5f;
  in.compressed_depth_max = 10.0f;
  const std::vector<uint8_t> payload = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializeImage(in);
  auto out = deserializeImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->encoding, "compressedDepth");
  ASSERT_TRUE(out->compressed_depth_min.has_value());
  ASSERT_TRUE(out->compressed_depth_max.has_value());
  EXPECT_FLOAT_EQ(*out->compressed_depth_min, 0.5f);
  EXPECT_FLOAT_EQ(*out->compressed_depth_max, 10.0f);
  EXPECT_TRUE(out->frame_id.empty());  // unset frame_id round-trips as empty
}

}  // namespace
}  // namespace PJ
