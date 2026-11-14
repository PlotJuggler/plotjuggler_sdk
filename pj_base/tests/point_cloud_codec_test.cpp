// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/point_cloud_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
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

// A PointField body: name, then `extra` (bytes spliced in verbatim), then offset / datatype / count.
std::vector<uint8_t> pointFieldBody(const std::vector<uint8_t>& extra) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 2);
  pb::appendString(body, "x");
  body.insert(body.end(), extra.begin(), extra.end());
  pb::appendTag(body, 2, 0);
  pb::appendVarint(body, 4);
  pb::appendTag(body, 3, 0);
  pb::appendVarint(body, 7);  // FLOAT32
  pb::appendTag(body, 4, 0);
  pb::appendVarint(body, 3);
  return body;
}

// A PointCloud holding one PointField (field 8) followed by frame_id (field 10).
std::vector<uint8_t> cloudWithField(const std::vector<uint8_t>& field_body) {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 8, 2);
  pb::appendLenDelim(bytes, field_body);
  pb::appendTag(bytes, 10, 2);
  pb::appendString(bytes, "lidar");
  return bytes;
}

// A later SDK may add a field of any wire type to PointField. This decoder has
// to skip it and stay in sync: the fields after it, and the cloud's own fields
// after the PointField, must still decode.
TEST(PointCloudCodecTest, UnknownFieldsInsideAPointFieldAreSkipped) {
  std::vector<uint8_t> varint_field;
  pb::appendTag(varint_field, 9, 0);
  pb::appendVarint(varint_field, 300);

  std::vector<uint8_t> fixed64_field;
  pb::appendTag(fixed64_field, 10, 1);
  pb::appendFixed64(fixed64_field, 0x0102030405060708ULL);

  std::vector<uint8_t> bytes_field;
  pb::appendTag(bytes_field, 11, 2);
  pb::appendString(bytes_field, "later");

  std::vector<uint8_t> fixed32_field;
  pb::appendTag(fixed32_field, 12, 5);
  pb::appendFixed32(fixed32_field, 0x01020304U);

  for (const auto& extra : {varint_field, fixed64_field, bytes_field, fixed32_field}) {
    const auto bytes = cloudWithField(pointFieldBody(extra));
    auto out = deserializePointCloud(bytes.data(), bytes.size());
    ASSERT_TRUE(out.has_value()) << "unknown field with first byte " << int{extra.front()} << ": " << out.error();
    ASSERT_EQ(out->fields.size(), 1u);
    EXPECT_EQ(out->fields[0].name, "x");
    EXPECT_EQ(out->fields[0].offset, 4u);
    EXPECT_EQ(out->fields[0].datatype, PointField::Datatype::kFloat32);
    EXPECT_EQ(out->fields[0].count, 3u);
    EXPECT_EQ(out->frame_id, "lidar");
  }
}

// A known field that is malformed fails the decode. The bytes after the bad
// PointField are chosen so that skipping a second field over them would land
// exactly on the end of the buffer and report success.
TEST(PointCloudCodecTest, MalformedPointFieldFailsTheDecode) {
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 8, 2);
  pb::appendLenDelim(bytes, {0x10, 0x80});  // offset: a varint cut short
  bytes.insert(bytes.end(), {0x02, 0x10, 0x07});
  EXPECT_FALSE(deserializePointCloud(bytes.data(), bytes.size()).has_value());

  // The same bad PointField followed by a well-formed field is rejected too.
  const auto valid_suffix = cloudWithField({0x10, 0x80});
  EXPECT_FALSE(deserializePointCloud(valid_suffix.data(), valid_suffix.size()).has_value());
}

// A standard proto3 encoder omits every field equal to its zero value, so a
// missing field means zero, whatever the C++ struct's own default is.
TEST(PointCloudCodecTest, AbsentFieldsDecodeAsTheProto3ZeroValues) {
  std::vector<uint8_t> name_only;
  pb::appendTag(name_only, 1, 2);
  pb::appendString(name_only, "x");

  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 2, 0);
  pb::appendVarint(bytes, 3);  // width
  pb::appendTag(bytes, 8, 2);
  pb::appendLenDelim(bytes, name_only);

  auto out = deserializePointCloud(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value()) << out.error();
  EXPECT_EQ(out->width, 3u);
  EXPECT_EQ(out->height, 0u);
  EXPECT_FALSE(out->is_dense);
  ASSERT_EQ(out->fields.size(), 1u);
  EXPECT_EQ(out->fields[0].count, 0u);
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

PointCloud smallCloud(std::vector<uint8_t>& payload) {
  PointCloud in;
  in.width = 2;
  in.point_step = 12;
  in.row_step = 24;
  in.frame_id = "lidar";
  in.fields = {
      {.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  payload.assign(24, 0);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i * 7);
  }
  in.data = Span<const uint8_t>(payload.data(), payload.size());
  return in;
}

TEST(PointCloudCodecTest, ViewAliasesAnchoredBytes) {
  std::vector<uint8_t> points;
  const PointCloud in = smallCloud(points);
  const auto wire = std::make_shared<const std::vector<uint8_t>>(serializePointCloud(in));
  auto out = deserializePointCloudView(wire->data(), wire->size(), wire);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->frame_id, in.frame_id);
  ASSERT_EQ(out->fields.size(), 3u);
  ASSERT_EQ(out->data.size(), points.size());
  EXPECT_EQ(std::memcmp(out->data.data(), points.data(), points.size()), 0);
  // No copy: the bytes live inside the wire buffer, which the result keeps alive.
  EXPECT_GE(out->data.data(), wire->data());
  EXPECT_LE(out->data.data() + out->data.size(), wire->data() + wire->size());
  EXPECT_EQ(out->anchor, wire);
}

TEST(PointCloudCodecTest, ViewWithoutAnchorCopies) {
  std::vector<uint8_t> points;
  const PointCloud in = smallCloud(points);
  const std::vector<uint8_t> wire = serializePointCloud(in);
  auto out = deserializePointCloudView(wire.data(), wire.size(), nullptr);
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->data.size(), points.size());
  EXPECT_EQ(std::memcmp(out->data.data(), points.data(), points.size()), 0);
  EXPECT_TRUE(out->data.data() < wire.data() || out->data.data() >= wire.data() + wire.size());
  EXPECT_NE(out->anchor, nullptr);
}

TEST(PointCloudCodecTest, ViewRejectsWhatTheCopyingDecoderRejects) {
  const auto garbage = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{0xFF, 0xFF, 0xFF});
  EXPECT_FALSE(deserializePointCloudView(garbage->data(), garbage->size(), garbage).has_value());
  EXPECT_FALSE(deserializePointCloudView(nullptr, 0, nullptr).has_value());
}

}  // namespace
}  // namespace PJ
