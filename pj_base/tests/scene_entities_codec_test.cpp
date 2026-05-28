// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/scene_entities_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::ArrowPrimitive;
using sdk::AxesPrimitive;
using sdk::ColorRGBA;
using sdk::CubePrimitive;
using sdk::CylinderPrimitive;
using sdk::LinePrimitive;
using sdk::LineType;
using sdk::ModelPrimitive;
using sdk::Point3;
using sdk::Pose;
using sdk::Quaternion;
using sdk::SceneEntities;
using sdk::SceneEntity;
using sdk::SceneEntityDeletion;
using sdk::SpherePrimitive;
using sdk::TextPrimitive;
using sdk::TrianglePrimitive;
using sdk::Vector3;
namespace pb = ::PJ::test_pb;

// Compare two ColorRGBA values allowing 1-LSB drift on each channel from the
// uint8 -> double in [0,1] -> uint8 round-trip in the codec.
::testing::AssertionResult ColorNear(const ColorRGBA& a, const ColorRGBA& b) {
  auto near = [](uint8_t x, uint8_t y) { return x > y ? (x - y) <= 1 : (y - x) <= 1; };
  if (near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a)) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "Color mismatch: got {" << +b.r << "," << +b.g << "," << +b.b << "," << +b.a
                                       << "}, expected {" << +a.r << "," << +a.g << "," << +a.b << "," << +a.a << "}";
}

Pose makePose(double tx, double ty, double tz) {
  return Pose{.position = {.x = tx, .y = ty, .z = tz}, .orientation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0}};
}

TEST(SceneEntitiesCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaSceneEntities, "PJ.SceneEntities");
}

TEST(SceneEntitiesCodecTest, EmptyBufferDecodesEmpty) {
  // SceneEntities round-trips an empty batch as empty bytes (top-level
  // is a `repeated SceneEntity entities = 1`).
  SceneEntities empty;
  const auto bytes = serializeSceneEntities(empty);
  EXPECT_TRUE(bytes.empty());
}

TEST(SceneEntitiesCodecTest, EmptyBufferDeserializesAsError) {
  EXPECT_FALSE(deserializeSceneEntities(nullptr, 0).has_value());
}

TEST(SceneEntitiesCodecTest, RoundTripOneEntityWithEachPrimitive) {
  SceneEntities in;
  SceneEntity e;
  e.timestamp = 100'000'000LL;
  e.frame_id = "world";
  e.id = "mixed_demo";
  e.lifetime_ns = 5'000'000'000LL;
  e.frame_locked = true;

  e.arrows.push_back(
      ArrowPrimitive{
          .pose = makePose(1.0, 0.0, 0.0),
          .shaft_length = 0.5,
          .shaft_diameter = 0.05,
          .head_length = 0.1,
          .head_diameter = 0.1,
          .color = {255, 0, 0, 255},
      });
  e.cubes.push_back(
      CubePrimitive{
          .pose = makePose(0.0, 1.0, 0.0),
          .size = {.x = 0.2, .y = 0.2, .z = 0.2},
          .color = {0, 255, 0, 255},
      });
  e.spheres.push_back(
      SpherePrimitive{
          .pose = makePose(0.0, 0.0, 1.0),
          .size = {.x = 0.3, .y = 0.3, .z = 0.3},
          .color = {0, 0, 255, 255},
      });
  e.cylinders.push_back(
      CylinderPrimitive{
          .pose = makePose(1.0, 1.0, 0.0),
          .size = {.x = 0.2, .y = 0.2, .z = 0.5},
          .bottom_scale = 1.0,
          .top_scale = 0.5,
          .color = {255, 255, 0, 255},
      });

  LinePrimitive line;
  line.type = LineType::kLineStrip;
  line.pose = makePose(0.0, 0.0, 0.0);
  line.thickness = 0.02;
  line.scale_invariant = false;
  line.points = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};
  line.color = {255, 255, 255, 255};
  e.lines.push_back(std::move(line));

  TrianglePrimitive tri;
  tri.pose = makePose(0.0, 0.0, 2.0);
  tri.points = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
  tri.color = {128, 128, 128, 255};
  e.triangles.push_back(std::move(tri));

  e.texts.push_back(
      TextPrimitive{
          .pose = makePose(0.0, 0.0, 3.0),
          .billboard = true,
          .font_size = 14.0,
          .scale_invariant = true,
          .color = {255, 255, 255, 255},
          .text = "label",
      });

  e.axes.push_back(
      AxesPrimitive{
          .pose = makePose(2.0, 0.0, 0.0),
          .length = 1.0,
          .thickness = 0.02,
          .scale_invariant = false,
      });

  in.entities.push_back(std::move(e));

  const auto bytes = serializeSceneEntities(in);
  auto out = deserializeSceneEntities(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->entities.size(), 1u);
  const auto& src = in.entities.front();
  const auto& dst = out->entities.front();

  EXPECT_EQ(dst.timestamp, src.timestamp);
  EXPECT_EQ(dst.frame_id, src.frame_id);
  EXPECT_EQ(dst.id, src.id);
  EXPECT_EQ(dst.lifetime_ns, src.lifetime_ns);
  EXPECT_EQ(dst.frame_locked, src.frame_locked);

  ASSERT_EQ(dst.arrows.size(), 1u);
  ASSERT_EQ(dst.cubes.size(), 1u);
  ASSERT_EQ(dst.spheres.size(), 1u);
  ASSERT_EQ(dst.cylinders.size(), 1u);
  ASSERT_EQ(dst.lines.size(), 1u);
  ASSERT_EQ(dst.triangles.size(), 1u);
  ASSERT_EQ(dst.texts.size(), 1u);
  ASSERT_EQ(dst.axes.size(), 1u);

  EXPECT_EQ(dst.arrows.front().pose, src.arrows.front().pose);
  EXPECT_TRUE(ColorNear(src.arrows.front().color, dst.arrows.front().color));
  EXPECT_EQ(dst.cubes.front().size, src.cubes.front().size);
  EXPECT_DOUBLE_EQ(dst.cylinders.front().top_scale, src.cylinders.front().top_scale);
  EXPECT_EQ(dst.lines.front().type, src.lines.front().type);
  EXPECT_EQ(dst.lines.front().points, src.lines.front().points);
  EXPECT_EQ(dst.triangles.front().points, src.triangles.front().points);
  EXPECT_EQ(dst.texts.front().text, src.texts.front().text);
  EXPECT_TRUE(dst.texts.front().billboard);
  EXPECT_DOUBLE_EQ(dst.axes.front().length, src.axes.front().length);
}

TEST(SceneEntitiesCodecTest, RoundTripLineWithPerVertexColorsAndIndices) {
  SceneEntities in;
  SceneEntity e;
  e.frame_id = "world";
  e.id = "lines";
  LinePrimitive line;
  line.type = LineType::kLineList;
  line.pose = makePose(0.0, 0.0, 0.0);
  line.thickness = 0.01;
  line.points = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
  line.colors = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
  line.indices = {0, 1, 2, 3};
  e.lines.push_back(std::move(line));
  in.entities.push_back(std::move(e));

  const auto bytes = serializeSceneEntities(in);
  auto out = deserializeSceneEntities(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->entities.size(), 1u);
  ASSERT_EQ(out->entities.front().lines.size(), 1u);
  const auto& dst_line = out->entities.front().lines.front();
  EXPECT_EQ(dst_line.type, LineType::kLineList);
  EXPECT_EQ(dst_line.indices, std::vector<uint32_t>({0, 1, 2, 3}));
  ASSERT_EQ(dst_line.colors.size(), 4u);
  EXPECT_TRUE(ColorNear(ColorRGBA{0, 255, 0, 255}, dst_line.colors[1]));
}

TEST(SceneEntitiesCodecTest, RoundTripModelPrimitive) {
  SceneEntities in;
  SceneEntity e;
  e.frame_id = "base_link";
  e.id = "robot_mesh";
  e.metadata.push_back({.key = "source", .value = "urdf"});
  ModelPrimitive model;
  model.pose = makePose(1.0, 2.0, 3.0);
  model.scale = {.x = 2.0, .y = 2.0, .z = 2.0};
  model.color = {10, 20, 30, 200};
  model.override_color = true;
  model.url = "package://robot/meshes/base.dae";
  model.media_type = "model/vnd.collada+xml";
  model.data = {0x01, 0x02, 0x03, 0x04};
  e.models.push_back(std::move(model));
  in.entities.push_back(std::move(e));

  const auto bytes = serializeSceneEntities(in);
  auto out = deserializeSceneEntities(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->entities.size(), 1u);
  ASSERT_EQ(out->entities.front().metadata.size(), 1u);
  EXPECT_EQ(out->entities.front().metadata.front().key, "source");
  EXPECT_EQ(out->entities.front().metadata.front().value, "urdf");
  ASSERT_EQ(out->entities.front().models.size(), 1u);
  const auto& src = in.entities.front().models.front();
  const auto& dst = out->entities.front().models.front();
  EXPECT_EQ(dst.pose, src.pose);
  EXPECT_EQ(dst.scale, src.scale);
  EXPECT_TRUE(ColorNear(src.color, dst.color));
  EXPECT_TRUE(dst.override_color);
  EXPECT_EQ(dst.url, src.url);
  EXPECT_EQ(dst.media_type, src.media_type);
  EXPECT_EQ(dst.data, src.data);
}

TEST(SceneEntitiesCodecTest, RoundTripEntitiesAndDeletions) {
  SceneEntities in;
  SceneEntity e;
  e.frame_id = "world";
  e.id = "keep";
  e.cubes.push_back(
      CubePrimitive{
          .pose = makePose(0.0, 0.0, 0.0), .size = {.x = 1.0, .y = 1.0, .z = 1.0}, .color = {255, 255, 255, 255}});
  in.entities.push_back(std::move(e));
  in.deletions.push_back(
      SceneEntityDeletion{.type = SceneEntityDeletion::Type::kMatchingId, .timestamp = 42, .id = "gone"});
  in.deletions.push_back(SceneEntityDeletion{.type = SceneEntityDeletion::Type::kAll, .timestamp = 99, .id = ""});

  const auto bytes = serializeSceneEntities(in);
  auto out = deserializeSceneEntities(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->entities.size(), 1u);
  ASSERT_EQ(out->deletions.size(), 2u);
  EXPECT_EQ(out->deletions[0].type, SceneEntityDeletion::Type::kMatchingId);
  EXPECT_EQ(out->deletions[0].timestamp, 42);
  EXPECT_EQ(out->deletions[0].id, "gone");
  EXPECT_EQ(out->deletions[1].type, SceneEntityDeletion::Type::kAll);
  EXPECT_EQ(out->deletions[1].timestamp, 99);
}

TEST(SceneEntitiesCodecTest, DeletionsOnlyBatchRoundTrips) {
  SceneEntities in;
  in.deletions.push_back(SceneEntityDeletion{.type = SceneEntityDeletion::Type::kAll, .timestamp = 7, .id = ""});
  EXPECT_FALSE(in.empty());

  const auto bytes = serializeSceneEntities(in);
  auto out = deserializeSceneEntities(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->entities.empty());
  ASSERT_EQ(out->deletions.size(), 1u);
  EXPECT_EQ(out->deletions[0].type, SceneEntityDeletion::Type::kAll);
}

}  // namespace
}  // namespace PJ
