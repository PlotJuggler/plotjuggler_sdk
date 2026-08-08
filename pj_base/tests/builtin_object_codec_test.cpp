// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/builtin_object_codec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

TEST(BuiltinObjectCodec, DispatchesEveryStableBuiltinType) {
  const std::array<PJ::sdk::BuiltinObject, 17> objects{
      PJ::sdk::Image{},
      PJ::sdk::PointCloud{},
      PJ::sdk::DepthImage{},
      [] {
        PJ::sdk::ImageAnnotations annotations;
        annotations.circles.emplace_back();
        return annotations;
      }(),
      [] {
        PJ::sdk::FrameTransforms transforms;
        transforms.transforms.emplace_back();
        return transforms;
      }(),
      PJ::sdk::OccupancyGrid{},
      PJ::sdk::CompressedPointCloud{},
      PJ::sdk::Mesh3D{},
      PJ::sdk::VideoFrame{},
      [] {
        PJ::sdk::SceneEntities scene;
        scene.entities.emplace_back();
        return scene;
      }(),
      PJ::sdk::RobotDescription{},
      PJ::sdk::CameraInfo{},
      PJ::sdk::OccupancyGridUpdate{},
      PJ::sdk::Log{},
      PJ::sdk::PosesInFrame{},
      PJ::sdk::VoxelGrid{},
      [] {
        PJ::sdk::PlotMarkers markers;
        markers.markers.emplace_back();
        return markers;
      }(),
  };

  for (const auto& object : objects) {
    const auto expected_type = PJ::sdk::typeOf(object);
    ASSERT_NE(expected_type, PJ::sdk::BuiltinObjectType::kNone);

    auto encoded = PJ::serializeBuiltinObject(object);
    ASSERT_TRUE(encoded) << PJ::sdk::name(expected_type) << ": " << encoded.error();
    ASSERT_FALSE(encoded->empty()) << PJ::sdk::name(expected_type);

    auto decoded = PJ::deserializeBuiltinObject(expected_type, encoded->data(), encoded->size());
    ASSERT_TRUE(decoded) << PJ::sdk::name(expected_type) << ": " << decoded.error();
    EXPECT_EQ(PJ::sdk::typeOf(*decoded), expected_type);
  }
}

TEST(BuiltinObjectCodec, RejectsEmptyAndUnknownTypeTags) {
  const auto encoded = PJ::serializeBuiltinObject(PJ::sdk::BuiltinObject{});
  EXPECT_FALSE(encoded);
  EXPECT_NE(encoded.error().find("kNone"), std::string::npos);

  const uint8_t byte = 0;
  const auto decoded = PJ::deserializeBuiltinObject(static_cast<PJ::sdk::BuiltinObjectType>(999), &byte, sizeof(byte));
  EXPECT_FALSE(decoded);
  EXPECT_NE(decoded.error().find("unknown"), std::string::npos);
}

TEST(BuiltinObjectCodec, RoundTripsDefaultObjectsIncludingZeroByteProtoMessages) {
  const std::array<PJ::sdk::BuiltinObject, 17> defaults{
      PJ::sdk::Image{},
      PJ::sdk::PointCloud{},
      PJ::sdk::DepthImage{},
      PJ::sdk::ImageAnnotations{},
      PJ::sdk::FrameTransforms{},
      PJ::sdk::OccupancyGrid{},
      PJ::sdk::CompressedPointCloud{},
      PJ::sdk::Mesh3D{},
      PJ::sdk::VideoFrame{},
      PJ::sdk::SceneEntities{},
      PJ::sdk::RobotDescription{},
      PJ::sdk::CameraInfo{},
      PJ::sdk::OccupancyGridUpdate{},
      PJ::sdk::Log{},
      PJ::sdk::PosesInFrame{},
      PJ::sdk::VoxelGrid{},
      PJ::sdk::PlotMarkers{},
  };

  for (const auto& input : defaults) {
    const auto type = PJ::sdk::typeOf(input);
    auto encoded = PJ::serializeBuiltinObject(input);
    ASSERT_TRUE(encoded) << PJ::sdk::name(type) << ": " << encoded.error();

    auto decoded = PJ::deserializeBuiltinObject(type, encoded->data(), encoded->size());
    ASSERT_TRUE(decoded) << PJ::sdk::name(type) << ": " << decoded.error();
    EXPECT_EQ(PJ::sdk::typeOf(*decoded), type);
  }
}

TEST(BuiltinObjectCodec, RobotDescriptionHasCanonicalWireRoundTrip) {
  PJ::sdk::RobotDescription input;
  input.timestamp_ns = 123456789;
  input.topic = "/robot_description";
  input.format = "urdf";
  input.text = "<robot name=\"test\"/>";

  const auto encoded = PJ::serializeRobotDescription(input);
  ASSERT_FALSE(encoded.empty());

  auto output = PJ::deserializeRobotDescription(encoded.data(), encoded.size());
  ASSERT_TRUE(output) << output.error();
  EXPECT_EQ(*output, input);
}

}  // namespace
