// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/asset_video_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::AssetVideo;
namespace pb = ::PJ::test_pb;

TEST(AssetVideoCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaAssetVideo, "PJ.AssetVideo");
}

TEST(AssetVideoCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeAssetVideo(nullptr, 0).has_value());
}

TEST(AssetVideoCodecTest, RoundTripFullyPopulated) {
  AssetVideo in;
  in.time_origin_ns = 1'700'000'000'000'000'000LL;
  in.duration_ns = 60'000'000'000LL;  // 60 s
  in.file_path = "/data/2026-05-21/camera0.mp4";
  in.media_type = "video/mp4";
  in.width = 1920;
  in.height = 1080;
  in.frame_rate = 29.97;

  const auto bytes = serializeAssetVideo(in);
  auto out = deserializeAssetVideo(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_TRUE(out->time_origin_ns.has_value());
  EXPECT_EQ(*out->time_origin_ns, *in.time_origin_ns);
  ASSERT_TRUE(out->duration_ns.has_value());
  EXPECT_EQ(*out->duration_ns, *in.duration_ns);
  EXPECT_EQ(out->file_path, in.file_path);
  EXPECT_EQ(out->media_type, in.media_type);
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  EXPECT_DOUBLE_EQ(out->frame_rate, in.frame_rate);
}

TEST(AssetVideoCodecTest, OptionalsAbsentRoundTrip) {
  AssetVideo in;
  in.file_path = "relative/path.mkv";

  const auto bytes = serializeAssetVideo(in);
  auto out = deserializeAssetVideo(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_FALSE(out->time_origin_ns.has_value());
  EXPECT_FALSE(out->duration_ns.has_value());
  EXPECT_EQ(out->file_path, in.file_path);
  EXPECT_TRUE(out->media_type.empty());
  EXPECT_EQ(out->width, 0u);
  EXPECT_EQ(out->height, 0u);
}

}  // namespace
}  // namespace PJ
