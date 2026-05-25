// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh3d_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::Mesh3D;
namespace pb = ::PJ::test_pb;

TEST(Mesh3DCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaMesh3D, "PJ.Mesh3D");
}

TEST(Mesh3DCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeMesh3D(nullptr, 0).has_value());
}

TEST(Mesh3DCodecTest, RoundTripEmbeddedAsset) {
  Mesh3D in;
  in.timestamp_ns = 1'000'000'000LL;
  in.frame_id = "world";
  in.id = "robot_link0";
  in.pose.position = {.x = 0.0, .y = 0.0, .z = 0.5};
  in.pose.orientation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
  in.scale = {.x = 1.0, .y = 1.0, .z = 1.0};
  in.format = "stl";
  const std::vector<uint8_t> payload(80, 0xAA);  // minimal STL header bytes
  in.data = Span<const uint8_t>(payload.data(), payload.size());
  in.color = {200, 100, 50, 255};
  in.override_color = true;

  const auto bytes = serializeMesh3D(in);
  auto out = deserializeMesh3D(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->frame_id, in.frame_id);
  EXPECT_EQ(out->id, in.id);
  EXPECT_EQ(out->pose, in.pose);
  EXPECT_EQ(out->scale, in.scale);
  EXPECT_EQ(out->format, in.format);
  EXPECT_TRUE(out->url.empty());
  EXPECT_TRUE(out->override_color);
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

TEST(Mesh3DCodecTest, RoundTripUrlReference) {
  Mesh3D in;
  in.timestamp_ns = 0;
  in.frame_id = "world";
  in.id = "wheel";
  in.pose.orientation.w = 1.0;
  in.url = "file:///robots/wheel.glb";

  const auto bytes = serializeMesh3D(in);
  auto out = deserializeMesh3D(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->url, in.url);
  EXPECT_TRUE(out->data.empty());
  EXPECT_FALSE(out->override_color);
}

}  // namespace
}  // namespace PJ
