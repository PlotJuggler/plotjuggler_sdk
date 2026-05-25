// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/video_frame_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::VideoFrame;
namespace pb = ::PJ::test_pb;

TEST(VideoFrameCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaVideoFrame, "PJ.VideoFrame");
}

TEST(VideoFrameCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeVideoFrame(nullptr, 0).has_value());
}

TEST(VideoFrameCodecTest, RoundTripRealisticPayload) {
  VideoFrame in;
  in.timestamp_ns = 1'700'000'000'500'000'000LL;
  in.frame_id = "camera_color_optical_frame";
  in.format = "h264";
  const std::vector<uint8_t> payload = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xC0, 0x1F};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializeVideoFrame(in);
  auto out = deserializeVideoFrame(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->format, in.format);
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

}  // namespace
}  // namespace PJ
