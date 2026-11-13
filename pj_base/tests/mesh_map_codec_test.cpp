// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_codec.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/mesh_map_update_codec.hpp"

namespace PJ {
namespace {

using sdk::ChannelCategory;
using sdk::ChannelInfo;
using sdk::ChannelKind;
using sdk::MeshChannel;
using sdk::MeshChannelPatch;
using sdk::MeshFaceLink;
using sdk::MeshMap;
using sdk::MeshMapUpdate;
using Datatype = sdk::PointField::Datatype;
using Target = sdk::MeshChannel::Target;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

template <typename T>
Span<const uint8_t> bytesOf(const std::shared_ptr<std::vector<T>>& values) {
  return {reinterpret_cast<const uint8_t*>(values->data()), values->size() * sizeof(T)};
}

// A unit square split into two triangles, with a float face channel (one value
// "no data"), a categorical uint8 face channel with labels, a uint8 vertex
// channel with an integer "no data" value, and a vertex normal (count 3).
struct Fixture {
  std::shared_ptr<std::vector<float>> positions =
      std::make_shared<std::vector<float>>(std::vector<float>{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0});
  std::shared_ptr<std::vector<uint32_t>> indices =
      std::make_shared<std::vector<uint32_t>>(std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
  std::shared_ptr<std::vector<float>> cost = std::make_shared<std::vector<float>>(std::vector<float>{3.5F, kNaN});
  std::shared_ptr<std::vector<uint8_t>> surface = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0, 1});
  std::shared_ptr<std::vector<uint8_t>> occupancy =
      std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0, 100, 255, 7});
  std::shared_ptr<std::vector<float>> normals = std::make_shared<std::vector<float>>(std::vector<float>(12, 0.0F));
  MeshMap mesh;

  Fixture() {
    for (size_t v = 0; v < 4; ++v) {
      (*normals)[(3 * v) + 2] = 1.0F;
    }
    mesh.timestamp_ns = 7'000'000'001LL;
    mesh.frame_id = "map";
    mesh.origin.position = {.x = 1.0, .y = 2.0, .z = 0.0};
    mesh.id = "tile/3";
    mesh.geometry_id = "18364758544493064720";
    mesh.vertex_count = 4;
    mesh.position_type = Datatype::kFloat32;
    mesh.positions = bytesOf(positions);
    mesh.face_count = 2;
    mesh.indices = bytesOf(indices);
    mesh.channels.push_back(
        {.name = "cost",
         .target = Target::kFace,
         .datatype = Datatype::kFloat32,
         .count = 1,
         .data = bytesOf(cost),
         .info = {.unit = "s"}});
    mesh.channels.push_back(
        {.name = "surface",
         .target = Target::kFace,
         .datatype = Datatype::kUint8,
         .count = 1,
         .data = bytesOf(surface),
         .info = {
             .kind = ChannelKind::kCategorical,
             .categories = {
                 ChannelCategory{.value = 0, .label = "ground"},
                 ChannelCategory{.value = 1, .label = "first floor", .color = sdk::ColorRGBA{255, 0, 0, 255}}}}});
    mesh.channels.push_back(
        {.name = "occupancy",
         .target = Target::kVertex,
         .datatype = Datatype::kUint8,
         .count = 1,
         .data = bytesOf(occupancy),
         .info = {.no_data = 255.0}});
    mesh.channels.push_back(
        {.name = "normal",
         .target = Target::kVertex,
         .datatype = Datatype::kFloat32,
         .count = 3,
         .data = bytesOf(normals)});
    mesh.links.push_back({.from_face = 0, .to_face = 1, .kind = 2, .bidirectional = true, .cost = 1.5});
    mesh.links.push_back({.from_face = 1, .to_face = 0});  // cost unspecified
  }
};

std::vector<uint8_t> appendUnknownVarint(std::vector<uint8_t> bytes, uint32_t field) {
  bytes.push_back(static_cast<uint8_t>(field << 3U));  // wire type 0 (varint); field < 16
  bytes.push_back(42);
  return bytes;
}

TEST(MeshMapCodecTest, SchemaNames) {
  EXPECT_EQ(kSchemaMeshMap, "PJ.MeshMap");
  EXPECT_EQ(kSchemaMeshMapUpdate, "PJ.MeshMapUpdate");
}

TEST(MeshMapCodecTest, RoundTripsEveryField) {
  Fixture fixture;
  ASSERT_TRUE(validateMeshMap(fixture.mesh));
  const auto bytes = serializeMeshMap(fixture.mesh);
  auto decoded = deserializeMeshMap(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();

  EXPECT_EQ(decoded->timestamp_ns, fixture.mesh.timestamp_ns);
  EXPECT_EQ(decoded->frame_id, "map");
  EXPECT_DOUBLE_EQ(decoded->origin.position.y, 2.0);
  EXPECT_EQ(decoded->id, "tile/3");
  EXPECT_EQ(decoded->geometry_id, fixture.mesh.geometry_id);
  EXPECT_EQ(decoded->vertex_count, 4u);
  EXPECT_EQ(decoded->position_type, Datatype::kFloat32);
  ASSERT_EQ(decoded->positions.size(), fixture.mesh.positions.size());
  EXPECT_EQ(std::memcmp(decoded->positions.data(), fixture.positions->data(), decoded->positions.size()), 0);
  EXPECT_EQ(std::memcmp(decoded->indices.data(), fixture.indices->data(), decoded->indices.size()), 0);
  EXPECT_TRUE(decoded->anchor);
  EXPECT_NE(decoded->positions.data(), fixture.mesh.positions.data());  // owns its bytes

  ASSERT_EQ(decoded->channels.size(), 4u);
  const MeshChannel* cost = sdk::findChannel(decoded->channels, Target::kFace, "cost");
  ASSERT_NE(cost, nullptr);
  EXPECT_EQ(cost->info.unit, "s");
  EXPECT_FALSE(cost->info.no_data.has_value());
  float value = 0.0F;
  std::memcpy(&value, cost->data.data() + 4, sizeof(float));
  EXPECT_TRUE(std::isnan(value));  // "no data" survives the wire

  const MeshChannel* surface = sdk::findChannel(decoded->channels, Target::kFace, "surface");
  ASSERT_NE(surface, nullptr);
  EXPECT_EQ(surface->info.kind, ChannelKind::kCategorical);
  ASSERT_EQ(surface->info.categories.size(), 2u);
  EXPECT_EQ(surface->info.categories[1].label, "first floor");
  ASSERT_TRUE(surface->info.categories[1].color.has_value());
  EXPECT_EQ(surface->info.categories[1].color->r, 255);
  EXPECT_FALSE(surface->info.categories[0].color.has_value());

  const MeshChannel* occupancy = sdk::findChannel(decoded->channels, Target::kVertex, "occupancy");
  ASSERT_NE(occupancy, nullptr);
  ASSERT_TRUE(occupancy->info.no_data.has_value());
  EXPECT_DOUBLE_EQ(*occupancy->info.no_data, 255.0);
  EXPECT_EQ(occupancy->data[2], 255);

  const MeshChannel* normal = sdk::findChannel(decoded->channels, Target::kVertex, "normal");
  ASSERT_NE(normal, nullptr);
  EXPECT_EQ(normal->count, 3u);
  EXPECT_EQ(sdk::findChannel(decoded->channels, Target::kFace, "normal"), nullptr);

  ASSERT_EQ(decoded->links.size(), 2u);
  EXPECT_EQ(decoded->links[0].kind, 2u);
  EXPECT_TRUE(decoded->links[0].bidirectional);
  EXPECT_DOUBLE_EQ(decoded->links[0].cost, 1.5);
  EXPECT_TRUE(std::isnan(decoded->links[1].cost));
}

TEST(MeshMapCodecTest, DispatcherRoundTripAndEmptyMesh) {
  Fixture fixture;
  auto bytes = serializeBuiltinObject(sdk::BuiltinObject{fixture.mesh});
  ASSERT_TRUE(bytes) << bytes.error();
  auto decoded = deserializeBuiltinObject(sdk::BuiltinObjectType::kMeshMap, bytes->data(), bytes->size());
  ASSERT_TRUE(decoded) << decoded.error();
  ASSERT_NE(decoded->get<MeshMap>(), nullptr);
  EXPECT_EQ(decoded->get<MeshMap>()->face_count, 2u);

  EXPECT_TRUE(validateMeshMap(MeshMap{}));
  const auto empty = serializeMeshMap(MeshMap{});
  EXPECT_TRUE(deserializeMeshMap(empty.data(), empty.size()));
}

TEST(MeshMapCodecTest, AcceptsFloat64Positions) {
  auto positions = std::make_shared<std::vector<double>>(std::vector<double>{0, 0, 0});
  MeshMap mesh;
  mesh.vertex_count = 1;
  mesh.position_type = Datatype::kFloat64;
  mesh.positions = bytesOf(positions);
  EXPECT_TRUE(validateMeshMap(mesh));
}

TEST(MeshMapCodecTest, SkipsUnknownFieldsForForwardCompatibility) {
  Fixture fixture;
  const auto bytes = appendUnknownVarint(serializeMeshMap(fixture.mesh), 15);
  auto decoded = deserializeMeshMap(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->channels.size(), 4u);
}

TEST(MeshMapCodecTest, RejectsMalformedKnownFieldsAndTruncation) {
  // A known field with an invalid value fails the decode rather than being
  // skipped: channel (field 11) whose target (field 2) is 7.
  auto bytes = serializeMeshMap(MeshMap{});
  for (const uint8_t byte : {uint8_t{(11U << 3U) | 2U}, uint8_t{2}, uint8_t{(2U << 3U) | 0U}, uint8_t{7}}) {
    bytes.push_back(byte);
  }
  EXPECT_FALSE(deserializeMeshMap(bytes.data(), bytes.size()));

  auto truncated = serializeMeshMap(Fixture{}.mesh);
  truncated.resize(truncated.size() - 3);
  EXPECT_FALSE(deserializeMeshMap(truncated.data(), truncated.size()));
  EXPECT_FALSE(deserializeMeshMap(nullptr, 0));
}

TEST(MeshMapCodecTest, RejectsUnsafeLayouts) {
  auto expect_rejected = [](auto mutate, const char* what) {
    Fixture fixture;
    mutate(fixture);
    EXPECT_FALSE(validateMeshMap(fixture.mesh)) << what;
    const auto bytes = serializeMeshMap(fixture.mesh);
    EXPECT_FALSE(deserializeMeshMap(bytes.data(), bytes.size())) << what;
  };
  expect_rejected([](Fixture& f) { f.mesh.position_type = Datatype::kInt32; }, "integer positions");
  expect_rejected([](Fixture& f) { f.mesh.vertex_count = 5; }, "positions short of vertex_count");
  expect_rejected([](Fixture& f) { f.mesh.face_count = 3; }, "indices short of face_count");
  expect_rejected([](Fixture& f) { (*f.indices)[4] = 9; }, "index out of range");
  expect_rejected([](Fixture& f) { f.mesh.channels[0].data = f.mesh.channels[0].data.subspan(0, 4); }, "short channel");
  expect_rejected([](Fixture& f) { f.mesh.channels[0].datatype = Datatype::kUnknown; }, "unknown datatype");
  expect_rejected([](Fixture& f) { f.mesh.channels[3].count = 2; }, "count disagrees with data");
  expect_rejected([](Fixture& f) { f.mesh.channels[0].name.clear(); }, "unnamed channel");
  expect_rejected([](Fixture& f) { f.mesh.channels[1].name = "cost"; }, "duplicate face channel");
  expect_rejected([](Fixture& f) { f.mesh.links[0].to_face = 2; }, "link out of range");

  Fixture same_name_other_target;
  same_name_other_target.mesh.channels[2].name = "cost";  // a vertex "cost" beside the face one is fine
  EXPECT_TRUE(validateMeshMap(same_name_other_target.mesh));
}

struct UpdateFixture {
  std::shared_ptr<std::vector<uint32_t>> indices = std::make_shared<std::vector<uint32_t>>(std::vector<uint32_t>{3, 0});
  std::shared_ptr<std::vector<float>> sparse = std::make_shared<std::vector<float>>(std::vector<float>{1.25F, kNaN});
  std::shared_ptr<std::vector<uint8_t>> dense = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{1, 2});
  MeshMapUpdate update;

  UpdateFixture() {
    update.timestamp_ns = 9;
    update.frame_id = "map";
    update.id = "tile/3";
    update.geometry_id = "42";
    update.patches.push_back(
        {.channel =
             {.name = "height",
              .target = Target::kVertex,
              .datatype = Datatype::kFloat32,
              .data = bytesOf(sparse),
              .info = {.unit = "m"}},
         .indices = bytesOf(indices)});
    update.patches.push_back(
        {.channel = {
             .name = "occupancy",
             .target = Target::kFace,
             .datatype = Datatype::kUint8,
             .data = bytesOf(dense),
             .info = {.no_data = 255.0}}});
  }
};

TEST(MeshMapUpdateCodecTest, RoundTripsSparseAndDensePatches) {
  UpdateFixture fixture;
  ASSERT_TRUE(validateMeshMapUpdate(fixture.update));
  const auto bytes = appendUnknownVarint(serializeMeshMapUpdate(fixture.update), 14);
  auto decoded = deserializeMeshMapUpdate(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->timestamp_ns, 9);
  EXPECT_EQ(decoded->id, "tile/3");
  EXPECT_EQ(decoded->geometry_id, "42");
  ASSERT_EQ(decoded->patches.size(), 2u);
  const MeshChannelPatch& sparse = decoded->patches[0];
  EXPECT_EQ(sparse.channel.name, "height");
  EXPECT_EQ(sparse.channel.target, Target::kVertex);
  EXPECT_EQ(sparse.channel.info.unit, "m");
  ASSERT_EQ(sparse.indices.size(), 8u);
  uint32_t first = 0;
  std::memcpy(&first, sparse.indices.data(), sizeof(first));
  EXPECT_EQ(first, 3u);
  EXPECT_TRUE(decoded->patches[1].indices.empty());  // dense
  EXPECT_EQ(decoded->patches[1].channel.data.size(), 2u);

  auto object = serializeBuiltinObject(sdk::BuiltinObject{fixture.update});
  ASSERT_TRUE(object);
  EXPECT_TRUE(deserializeBuiltinObject(sdk::BuiltinObjectType::kMeshMapUpdate, object->data(), object->size()));
}

TEST(MeshMapUpdateCodecTest, RejectsUnsafeLayouts) {
  UpdateFixture fixture;
  fixture.update.patches[0].indices = fixture.update.patches[0].indices.subspan(0, 6);
  EXPECT_FALSE(validateMeshMapUpdate(fixture.update));  // indices not uint32

  UpdateFixture short_data;
  short_data.update.patches[0].indices = short_data.update.patches[0].indices.subspan(0, 4);
  EXPECT_FALSE(validateMeshMapUpdate(short_data.update));  // 1 index, 2 values

  UpdateFixture partial;
  partial.update.patches[1].channel.datatype = Datatype::kUint16;
  partial.update.patches[1].channel.count = 3;
  EXPECT_FALSE(validateMeshMapUpdate(partial.update));  // 2 bytes are not a whole element

  UpdateFixture duplicate;
  duplicate.update.patches[1].channel.name = "height";
  duplicate.update.patches[1].channel.target = Target::kVertex;
  EXPECT_FALSE(validateMeshMapUpdate(duplicate.update));

  EXPECT_TRUE(validateMeshMapUpdate(MeshMapUpdate{}));
}

}  // namespace
}  // namespace PJ
