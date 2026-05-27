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
  in.start_ns = 12'000'000'000LL;  // 12 s into the file
  in.end_ns = 17'500'000'000LL;    // 17.5 s into the file
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
  ASSERT_TRUE(out->start_ns.has_value());
  EXPECT_EQ(*out->start_ns, *in.start_ns);
  ASSERT_TRUE(out->end_ns.has_value());
  EXPECT_EQ(*out->end_ns, *in.end_ns);
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
  EXPECT_FALSE(out->start_ns.has_value());
  EXPECT_FALSE(out->end_ns.has_value());
  EXPECT_EQ(out->file_path, in.file_path);
  EXPECT_TRUE(out->media_type.empty());
  EXPECT_EQ(out->width, 0u);
  EXPECT_EQ(out->height, 0u);
}

TEST(AssetVideoCodecTest, OneBoundSetOneAbsent) {
  // start_ns set, end_ns absent — consumers should clamp to start_ns and let
  // the decoder reveal the file's true end.
  AssetVideo in_start_only;
  in_start_only.file_path = "/data/file.mp4";
  in_start_only.start_ns = 5'000'000'000LL;

  const auto b1 = serializeAssetVideo(in_start_only);
  auto out1 = deserializeAssetVideo(b1.data(), b1.size());
  ASSERT_TRUE(out1.has_value());
  ASSERT_TRUE(out1->start_ns.has_value());
  EXPECT_EQ(*out1->start_ns, *in_start_only.start_ns);
  EXPECT_FALSE(out1->end_ns.has_value());

  // end_ns set, start_ns absent — symmetric, lets producers cap playback
  // without anchoring the start.
  AssetVideo in_end_only;
  in_end_only.file_path = "/data/file.mp4";
  in_end_only.end_ns = 9'000'000'000LL;

  const auto b2 = serializeAssetVideo(in_end_only);
  auto out2 = deserializeAssetVideo(b2.data(), b2.size());
  ASSERT_TRUE(out2.has_value());
  EXPECT_FALSE(out2->start_ns.has_value());
  ASSERT_TRUE(out2->end_ns.has_value());
  EXPECT_EQ(*out2->end_ns, *in_end_only.end_ns);
}

}  // namespace
}  // namespace PJ
