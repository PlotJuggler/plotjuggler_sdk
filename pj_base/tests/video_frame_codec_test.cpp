// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/video_frame_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
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

// Locks the on-wire field layout to match foxglove.CompressedVideo:
// timestamp=1, frame_id=2, data=3 (bytes), format=4 (string). The golden bytes
// are built independently of the codec so a future field-number regression is
// caught here.
TEST(VideoFrameCodecTest, WireLayoutMatchesFoxglove) {
  VideoFrame in;
  in.timestamp_ns = 1'700'000'000'500'000'000LL;
  in.frame_id = "cam0";
  in.format = "h265";
  const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  std::vector<uint8_t> expected;
  pb::appendTag(expected, 1, 2);  // timestamp (message)
  pb::appendLenDelim(expected, pb::encodeTimestamp(in.timestamp_ns));
  pb::appendTag(expected, 2, 2);  // frame_id (string)
  pb::appendString(expected, in.frame_id);
  pb::appendTag(expected, 3, 2);  // data (bytes)
  pb::appendBytes(expected, payload.data(), payload.size());
  pb::appendTag(expected, 4, 2);  // format (string)
  pb::appendString(expected, in.format);

  const auto bytes = serializeVideoFrame(in);
  EXPECT_EQ(bytes, expected);
}

// deserializeVideoFrameView must NOT copy the compressed bitstream: the
// returned data span has to point straight into the wire buffer, and the frame
// must keep the supplied anchor alive.
TEST(VideoFrameCodecTest, ViewAliasesInputBuffer) {
  VideoFrame in;
  in.timestamp_ns = 42;
  in.frame_id = "cam";
  in.format = "av1";
  const std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  // Own the wire bytes through a shared_ptr so it can double as the anchor.
  auto wire = std::make_shared<std::vector<uint8_t>>(serializeVideoFrame(in));
  sdk::BufferAnchor anchor = wire;

  auto out = deserializeVideoFrameView(wire->data(), wire->size(), anchor);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->format, in.format);

  // Round-trips the payload contents...
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);

  // ...and aliases the input buffer: the span points inside `wire`, not at a
  // fresh copy.
  const uint8_t* wire_begin = wire->data();
  const uint8_t* wire_end = wire->data() + wire->size();
  EXPECT_GE(out->data.data(), wire_begin);
  EXPECT_LE(out->data.data() + out->data.size(), wire_end);

  // The frame's anchor must reference the same allocation we handed in, keeping
  // the aliased bytes alive.
  EXPECT_EQ(out->anchor, anchor);
}

TEST(VideoFrameCodecTest, ViewEmptyBufferProducesError) {
  sdk::BufferAnchor anchor;
  EXPECT_FALSE(deserializeVideoFrameView(nullptr, 0, anchor).has_value());
}

}  // namespace
}  // namespace PJ
