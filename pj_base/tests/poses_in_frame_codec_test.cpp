// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/poses_in_frame_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

namespace pb = ::PJ::test_pb;

TEST(PosesInFrameCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaPosesInFrame, "PJ.PosesInFrame");
}

TEST(PosesInFrameCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializePosesInFrame(nullptr, 0).has_value());
}

TEST(PosesInFrameCodecTest, RoundTrip) {
  sdk::PosesInFrame in;
  in.timestamp_ns = 1'781'069'110'760'016'524LL;
  in.frame_id = "map";
  in.poses.push_back(sdk::Pose{{1.0, -2.0, 0.5}, {0.0, 0.0, 0.7071067811865476, 0.7071067811865476}});
  in.poses.push_back(sdk::Pose{});  // identity orientation (w=1), zero position

  const auto bytes = serializePosesInFrame(in);
  const auto out = deserializePosesInFrame(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  ASSERT_EQ(out->poses.size(), 2u);
  EXPECT_EQ(out->poses[0], in.poses[0]);
  EXPECT_EQ(out->poses[1], in.poses[1]);
}

TEST(PosesInFrameCodecTest, InvalidNestedMessageProducesError) {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 3, 2);
  pb::appendVarint(bytes, 10);
  bytes.push_back(0x08);

  auto output = deserializePosesInFrame(bytes.data(), bytes.size());
  EXPECT_FALSE(output.has_value());
}

TEST(PosesInFrameCodecTest, EmptyPosesRoundTrip) {
  sdk::PosesInFrame in;
  in.frame_id = "odom";
  const auto bytes = serializePosesInFrame(in);
  const auto out = deserializePosesInFrame(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->poses.empty());
  EXPECT_EQ(out->frame_id, "odom");
}

}  // namespace
}  // namespace PJ
