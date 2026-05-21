// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/builtin_object.hpp"

#include <gtest/gtest.h>

using PJ::sdk::AssetVideo;
using PJ::sdk::BuiltinObject;
using PJ::sdk::BuiltinObjectType;
using PJ::sdk::CompressedPointCloud;
using PJ::sdk::DepthImage;
using PJ::sdk::FrameTransforms;
using PJ::sdk::Image;
using PJ::sdk::ImageAnnotations;
using PJ::sdk::Mesh3D;
using PJ::sdk::name;
using PJ::sdk::OccupancyGrid;
using PJ::sdk::parseBuiltinObjectType;
using PJ::sdk::PointCloud;
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
