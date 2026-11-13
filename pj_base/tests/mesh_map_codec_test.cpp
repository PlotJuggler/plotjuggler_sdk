// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_codec.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/mesh_map_update_codec.hpp"

namespace PJ {
namespace {

using sdk::MeshFaceLink;
using sdk::MeshMap;
using sdk::MeshMapUpdate;
using sdk::PointField;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

template <typename T>
void append(std::vector<uint8_t>& out, T value) {
  const auto offset = out.size();
  out.resize(offset + sizeof(T));
  std::memcpy(out.data() + offset, &value, sizeof(T));
}

// A unit square split into two triangles. Vertex record: x, y, z float32 and a
// "height" float32 channel (16 bytes). Face record: vertex_indices uint32[3]
// and a "cost" float32 channel (16 bytes); face 1's cost is "no data".
MeshMap makeMesh(std::vector<uint8_t>& storage) {
  MeshMap mesh;
  mesh.timestamp_ns = 7'000'000'001LL;
  mesh.frame_id = "map";
  mesh.origin.position = {.x = 1.0, .y = 2.0, .z = 0.0};
  mesh.geometry_id = "18364758544493064720";
  mesh.vertex_count = 4;
  mesh.vertex_stride = 16;
  mesh.vertex_fields = {
      {.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
      {.name = "height", .offset = 12, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  mesh.face_count = 2;
  mesh.face_stride = 16;
  mesh.face_data_offset = 64;
  mesh.face_fields = {
      {.name = "vertex_indices", .offset = 0, .datatype = PointField::Datatype::kUint32, .count = 3},
      {.name = "cost", .offset = 12, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  mesh.links = {{.from_face = 0, .to_face = 1, .kind = 2, .bidirectional = true}};

  storage.clear();
  const float xy[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (int v = 0; v < 4; ++v) {
    append(storage, xy[v][0]);
    append(storage, xy[v][1]);
    append(storage, 0.0F);
    append(storage, static_cast<float>(v) * 0.5F);
  }
  const uint32_t faces[2][3] = {{0, 1, 2}, {0, 2, 3}};
  const float costs[2] = {3.5F, kNaN};
  for (int f = 0; f < 2; ++f) {
    for (const uint32_t index : faces[f]) {
      append(storage, index);
    }
    append(storage, costs[f]);
  }
  mesh.data = Span<const uint8_t>(storage.data(), storage.size());
  return mesh;
}

float faceCost(const MeshMap& mesh, uint32_t face) {
  float value = 0.0F;
  std::memcpy(&value, mesh.data.data() + mesh.face_data_offset + (face * mesh.face_stride) + 12, sizeof(float));
  return value;
}

TEST(MeshMapCodecTest, SchemaNames) {
  EXPECT_EQ(kSchemaMeshMap, "PJ.MeshMap");
  EXPECT_EQ(kSchemaMeshMapUpdate, "PJ.MeshMapUpdate");
}

TEST(MeshMapCodecTest, RoundTripsEveryField) {
  std::vector<uint8_t> storage;
  const MeshMap mesh = makeMesh(storage);
  const auto bytes = serializeMeshMap(mesh);
  auto decoded = deserializeMeshMap(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();

  EXPECT_EQ(decoded->timestamp_ns, mesh.timestamp_ns);
  EXPECT_EQ(decoded->frame_id, "map");
  EXPECT_DOUBLE_EQ(decoded->origin.position.y, 2.0);
  EXPECT_EQ(decoded->geometry_id, mesh.geometry_id);
  EXPECT_EQ(decoded->vertex_count, 4u);
  EXPECT_EQ(decoded->vertex_stride, 16u);
  ASSERT_EQ(decoded->vertex_fields.size(), 4u);
  EXPECT_EQ(decoded->vertex_fields[3].name, "height");
  EXPECT_EQ(decoded->face_count, 2u);
  EXPECT_EQ(decoded->face_stride, 16u);
  EXPECT_EQ(decoded->face_data_offset, 64u);
  ASSERT_EQ(decoded->face_fields.size(), 2u);
  EXPECT_EQ(decoded->face_fields[0].count, 3u);
  EXPECT_EQ(decoded->links, mesh.links);
  ASSERT_EQ(decoded->data.size(), storage.size());
  EXPECT_EQ(std::memcmp(decoded->data.data(), storage.data(), storage.size()), 0);
  EXPECT_FLOAT_EQ(faceCost(*decoded, 0), 3.5F);
  EXPECT_TRUE(std::isnan(faceCost(*decoded, 1)));  // "no data" survives the wire
  // The decoded object owns its bytes.
  EXPECT_NE(decoded->data.data(), storage.data());
  EXPECT_TRUE(decoded->anchor);
}

TEST(MeshMapCodecTest, DispatcherRoundTrip) {
  std::vector<uint8_t> storage;
  const sdk::BuiltinObject object{makeMesh(storage)};
  auto bytes = serializeBuiltinObject(object);
  ASSERT_TRUE(bytes) << bytes.error();
  auto decoded = deserializeBuiltinObject(sdk::BuiltinObjectType::kMeshMap, bytes->data(), bytes->size());
  ASSERT_TRUE(decoded) << decoded.error();
  ASSERT_NE(decoded->get<MeshMap>(), nullptr);
  EXPECT_EQ(decoded->get<MeshMap>()->face_count, 2u);
}

TEST(MeshMapCodecTest, EmptyMeshIsValid) {
  EXPECT_TRUE(validateMeshMap(MeshMap{}));
  const auto bytes = serializeMeshMap(MeshMap{});
  EXPECT_TRUE(deserializeMeshMap(bytes.data(), bytes.size()));
}

TEST(MeshMapCodecTest, AcceptsFloat64Positions) {
  MeshMap mesh;
  mesh.vertex_count = 1;
  mesh.vertex_stride = 24;
  mesh.vertex_fields = {
      {.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat64, .count = 1},
      {.name = "y", .offset = 8, .datatype = PointField::Datatype::kFloat64, .count = 1},
      {.name = "z", .offset = 16, .datatype = PointField::Datatype::kFloat64, .count = 1},
  };
  const std::vector<uint8_t> storage(24, 0);
  mesh.data = Span<const uint8_t>(storage.data(), storage.size());
  EXPECT_TRUE(validateMeshMap(mesh));
}

TEST(MeshMapCodecTest, RejectsUnsafeLayouts) {
  std::vector<uint8_t> storage;
  auto expectRejected = [&](auto mutate, const char* what) {
    MeshMap mesh = makeMesh(storage);
    mutate(mesh);
    EXPECT_FALSE(validateMeshMap(mesh)) << what;
    const auto bytes = serializeMeshMap(mesh);
    EXPECT_FALSE(deserializeMeshMap(bytes.data(), bytes.size())) << what;
  };
  expectRejected([](MeshMap& m) { m.vertex_fields.erase(m.vertex_fields.begin() + 2); }, "missing z");
  expectRejected([](MeshMap& m) { m.vertex_fields[0].datatype = PointField::Datatype::kInt32; }, "integer x");
  expectRejected([](MeshMap& m) { m.face_fields[0].count = 4; }, "four indices");
  expectRejected([](MeshMap& m) { m.face_fields.erase(m.face_fields.begin()); }, "no vertex_indices");
  expectRejected([](MeshMap& m) { m.vertex_fields[3].offset = 14; }, "field past stride");
  expectRejected([](MeshMap& m) { m.vertex_stride = 0; }, "zero vertex stride");
  expectRejected([](MeshMap& m) { m.face_data_offset = 48; }, "faces overlap vertices");
  expectRejected([](MeshMap& m) { m.face_count = 3; }, "faces past data");
  expectRejected([](MeshMap& m) { m.vertex_count = 2; }, "index out of range");
  expectRejected([](MeshMap& m) { m.links[0].to_face = 2; }, "link out of range");
  expectRejected([](MeshMap& m) { m.data = m.data.subspan(0, 60); }, "vertices past data");
  EXPECT_FALSE(deserializeMeshMap(nullptr, 0));
}

MeshMapUpdate makeUpdate(std::vector<uint8_t>& storage) {
  MeshMapUpdate update;
  update.timestamp_ns = 9;
  update.frame_id = "map";
  update.geometry_id = "42";
  update.target = MeshMapUpdate::Target::kVertices;
  update.count = 2;
  update.stride = 8;
  update.fields = {
      {.name = "index", .offset = 0, .datatype = PointField::Datatype::kUint32, .count = 1},
      {.name = "cost", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  storage.clear();
  append(storage, uint32_t{3});
  append(storage, 1.25F);
  append(storage, uint32_t{0});
  append(storage, kNaN);
  update.data = Span<const uint8_t>(storage.data(), storage.size());
  return update;
}

TEST(MeshMapUpdateCodecTest, RoundTripsEveryField) {
  std::vector<uint8_t> storage;
  const MeshMapUpdate update = makeUpdate(storage);
  const auto bytes = serializeMeshMapUpdate(update);
  auto decoded = deserializeMeshMapUpdate(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->timestamp_ns, 9);
  EXPECT_EQ(decoded->frame_id, "map");
  EXPECT_EQ(decoded->geometry_id, "42");
  EXPECT_EQ(decoded->target, MeshMapUpdate::Target::kVertices);
  EXPECT_EQ(decoded->count, 2u);
  EXPECT_EQ(decoded->stride, 8u);
  ASSERT_EQ(decoded->fields.size(), 2u);
  ASSERT_EQ(decoded->data.size(), storage.size());
  EXPECT_EQ(std::memcmp(decoded->data.data(), storage.data(), storage.size()), 0);

  auto via_dispatcher = serializeBuiltinObject(sdk::BuiltinObject{update});
  ASSERT_TRUE(via_dispatcher);
  auto object =
      deserializeBuiltinObject(sdk::BuiltinObjectType::kMeshMapUpdate, via_dispatcher->data(), via_dispatcher->size());
  ASSERT_TRUE(object) << object.error();
  ASSERT_NE(object->get<MeshMapUpdate>(), nullptr);
}

TEST(MeshMapUpdateCodecTest, RejectsUnsafeLayouts) {
  std::vector<uint8_t> storage;
  MeshMapUpdate update = makeUpdate(storage);
  update.fields.erase(update.fields.begin());
  EXPECT_FALSE(validateMeshMapUpdate(update));  // no index

  update = makeUpdate(storage);
  update.fields[0].datatype = PointField::Datatype::kUint16;
  EXPECT_FALSE(validateMeshMapUpdate(update));  // index of the wrong type

  update = makeUpdate(storage);
  update.count = 3;
  EXPECT_FALSE(validateMeshMapUpdate(update));  // records past data

  update = makeUpdate(storage);
  update.fields[1].offset = 6;
  EXPECT_FALSE(validateMeshMapUpdate(update));  // channel past stride

  EXPECT_TRUE(validateMeshMapUpdate(MeshMapUpdate{}));  // empty update needs no index field
}

}  // namespace
}  // namespace PJ
