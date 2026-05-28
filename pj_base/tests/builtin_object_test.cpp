// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/builtin_object.hpp"

#include <gtest/gtest.h>

using PJ::sdk::AssetVideo;
using PJ::sdk::BuiltinObject;
using PJ::sdk::BuiltinObjectType;
using PJ::sdk::CameraInfo;
using PJ::sdk::CompressedPointCloud;
using PJ::sdk::DepthImage;
using PJ::sdk::FrameTransforms;
using PJ::sdk::Image;
using PJ::sdk::ImageAnnotations;
using PJ::sdk::Log;
using PJ::sdk::Mesh3D;
using PJ::sdk::name;
using PJ::sdk::OccupancyGrid;
using PJ::sdk::OccupancyGridUpdate;
using PJ::sdk::parseBuiltinObjectType;
using PJ::sdk::PointCloud;
using PJ::sdk::RobotDescription;
using PJ::sdk::SceneEntities;
using PJ::sdk::typeOf;
using PJ::sdk::VideoFrame;

TEST(BuiltinObjectTest, TypeOfRecognizesKnownBuiltinTypes) {
  EXPECT_EQ(typeOf(BuiltinObject{}), BuiltinObjectType::kNone);
  EXPECT_EQ(typeOf(BuiltinObject{Image{}}), BuiltinObjectType::kImage);
  EXPECT_EQ(typeOf(BuiltinObject{PointCloud{}}), BuiltinObjectType::kPointCloud);
  EXPECT_EQ(typeOf(BuiltinObject{DepthImage{}}), BuiltinObjectType::kDepthImage);
  EXPECT_EQ(typeOf(BuiltinObject{ImageAnnotations{}}), BuiltinObjectType::kImageAnnotations);
  EXPECT_EQ(typeOf(BuiltinObject{FrameTransforms{}}), BuiltinObjectType::kFrameTransforms);
  EXPECT_EQ(typeOf(BuiltinObject{OccupancyGrid{}}), BuiltinObjectType::kOccupancyGrid);
  EXPECT_EQ(typeOf(BuiltinObject{CompressedPointCloud{}}), BuiltinObjectType::kCompressedPointCloud);
  EXPECT_EQ(typeOf(BuiltinObject{Mesh3D{}}), BuiltinObjectType::kMesh3D);
  EXPECT_EQ(typeOf(BuiltinObject{VideoFrame{}}), BuiltinObjectType::kVideoFrame);
  EXPECT_EQ(typeOf(BuiltinObject{SceneEntities{}}), BuiltinObjectType::kSceneEntities);
  EXPECT_EQ(typeOf(BuiltinObject{AssetVideo{}}), BuiltinObjectType::kAssetVideo);
  EXPECT_EQ(typeOf(BuiltinObject{RobotDescription{}}), BuiltinObjectType::kRobotDescription);
  EXPECT_EQ(typeOf(BuiltinObject{CameraInfo{}}), BuiltinObjectType::kCameraInfo);
  EXPECT_EQ(typeOf(BuiltinObject{OccupancyGridUpdate{}}), BuiltinObjectType::kOccupancyGridUpdate);
  EXPECT_EQ(typeOf(BuiltinObject{Log{}}), BuiltinObjectType::kLog);
}

TEST(BuiltinObjectTest, NameAndParseRoundTripForEveryEnumEntry) {
  for (auto t : {
           BuiltinObjectType::kNone,
           BuiltinObjectType::kImage,
           BuiltinObjectType::kPointCloud,
           BuiltinObjectType::kDepthImage,
           BuiltinObjectType::kImageAnnotations,
           BuiltinObjectType::kFrameTransforms,
           BuiltinObjectType::kOccupancyGrid,
           BuiltinObjectType::kCompressedPointCloud,
           BuiltinObjectType::kMesh3D,
           BuiltinObjectType::kVideoFrame,
           BuiltinObjectType::kSceneEntities,
           BuiltinObjectType::kAssetVideo,
           BuiltinObjectType::kRobotDescription,
           BuiltinObjectType::kCameraInfo,
           BuiltinObjectType::kOccupancyGridUpdate,
           BuiltinObjectType::kLog,
       }) {
    const auto parsed = parseBuiltinObjectType(name(t));
    ASSERT_TRUE(parsed.has_value()) << "parseBuiltinObjectType failed for " << name(t);
    EXPECT_EQ(*parsed, t) << "round-trip mismatch for " << name(t);
  }
}

TEST(BuiltinObjectTest, ParseRejectsUnknownNames) {
  EXPECT_FALSE(parseBuiltinObjectType("FrameTransforms").has_value());  // missing leading 'k'
  EXPECT_FALSE(parseBuiltinObjectType("").has_value());
  EXPECT_FALSE(parseBuiltinObjectType("kBogus").has_value());
}

TEST(BuiltinObjectTest, ParsesRobotDescriptionTypeName) {
  EXPECT_EQ(parseBuiltinObjectType("kRobotDescription"), BuiltinObjectType::kRobotDescription);
  EXPECT_FALSE(parseBuiltinObjectType("RobotDescription").has_value());
}

TEST(BuiltinObjectTest, PointCloudCarriesFrameId) {
  PointCloud in;
  in.width = 100;
  in.height = 1;
  in.point_step = 16;
  in.row_step = 1600;
  in.frame_id = "velodyne";
  in.timestamp_ns = 1'500'000'000;
  BuiltinObject obj{in};
  ASSERT_EQ(typeOf(obj), BuiltinObjectType::kPointCloud);

  const auto* out = std::any_cast<PointCloud>(&obj);
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->frame_id, "velodyne");
  EXPECT_EQ(out->width, 100u);
  EXPECT_EQ(out->timestamp_ns, 1'500'000'000);
}

TEST(BuiltinObjectTest, RobotDescriptionRoundtripPreservesFields) {
  RobotDescription in{
      .timestamp_ns = 1'500'000'000,
      .topic = "/robot_description",
      .format = "urdf",
      .text = "<robot name=\"test\"><link name=\"base_link\"/></robot>",
  };
  BuiltinObject obj{in};
  ASSERT_EQ(typeOf(obj), BuiltinObjectType::kRobotDescription);

  const auto* out = std::any_cast<RobotDescription>(&obj);
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->topic, in.topic);
  EXPECT_EQ(out->format, in.format);
  EXPECT_EQ(out->text, in.text);
  EXPECT_FALSE(out->empty());

  RobotDescription empty;
  EXPECT_TRUE(empty.empty());
}
