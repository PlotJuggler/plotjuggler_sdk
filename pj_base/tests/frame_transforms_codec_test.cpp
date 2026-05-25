// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/frame_transforms_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::FrameTransform;
using sdk::FrameTransforms;
namespace pb = ::PJ::test_pb;

std::vector<uint8_t> encodeFrameTransform(const FrameTransform& transform) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 2);
  pb::appendLenDelim(body, pb::encodeTimestamp(transform.timestamp));
  pb::appendTag(body, 2, 2);
  pb::appendString(body, transform.parent_frame_id);
  pb::appendTag(body, 3, 2);
  pb::appendString(body, transform.child_frame_id);
  pb::appendTag(body, 4, 2);
  pb::appendLenDelim(body, pb::encodeVector3(transform.translation));
  pb::appendTag(body, 5, 2);
  pb::appendLenDelim(body, pb::encodeQuaternion(transform.rotation));
  return body;
}

TEST(FrameTransformsCodecTest, SchemaNameMatchesFrameTransforms) {
  EXPECT_EQ(kSchemaFrameTransforms, "PJ.FrameTransforms");
}

TEST(FrameTransformsCodecTest, EmptyMessageProducesEmptyBytes) {
  FrameTransforms transforms;
  EXPECT_TRUE(serializeFrameTransforms(transforms).empty());
}

TEST(FrameTransformsCodecTest, GoldenBytesSingleTransform) {
  FrameTransforms transforms;
  transforms.transforms.push_back(
      FrameTransform{
          .timestamp = 1'234'567'890'123,
          .parent_frame_id = "map",
          .child_frame_id = "base_link",
          .translation = {.x = 1.0, .y = 2.0, .z = 3.0},
          .rotation = {.x = 0.0, .y = 0.0, .z = 0.707, .w = 0.707},
      });

  std::vector<uint8_t> expected;
  pb::appendTag(expected, 1, 2);
  pb::appendLenDelim(expected, encodeFrameTransform(transforms.transforms.front()));

  EXPECT_EQ(serializeFrameTransforms(transforms), expected);
}

TEST(FrameTransformsCodecTest, RoundTripMultipleTransforms) {
  FrameTransforms input;
  input.transforms.push_back(
      FrameTransform{
          .timestamp = 42,
          .parent_frame_id = "map",
          .child_frame_id = "odom",
          .translation = {.x = 1.0, .y = 0.0, .z = 0.0},
          .rotation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0},
      });
  input.transforms.push_back(
      FrameTransform{
          .timestamp = -1,
          .parent_frame_id = "odom",
          .child_frame_id = "base_link",
          .translation = {.x = -1.5, .y = 2.5, .z = 3.5},
          .rotation = {.x = 0.1, .y = 0.2, .z = 0.3, .w = 0.9},
      });

  const auto bytes = serializeFrameTransforms(input);
  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());

  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(*output, input);
}

TEST(FrameTransformsCodecTest, EmptyBufferProducesError) {
  const std::vector<uint8_t> bytes;
  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());
  EXPECT_FALSE(output.has_value());
}

TEST(FrameTransformsCodecTest, InvalidNestedMessageProducesError) {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 1, 2);
  pb::appendVarint(bytes, 10);
  bytes.push_back(0x08);

  auto output = deserializeFrameTransforms(bytes.data(), bytes.size());
  EXPECT_FALSE(output.has_value());
}

}  // namespace
}  // namespace PJ
