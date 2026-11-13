// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_codec.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/mesh_map_update_codec.hpp"
#include "protobuf_wire_test_helpers.hpp"

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

TEST(MeshMapCodecTest, RoundTripsFloat64Positions) {
  auto positions = std::make_shared<std::vector<double>>(std::vector<double>{1.5, -2.0, 3.25});
  MeshMap mesh;
  mesh.vertex_count = 1;
  mesh.position_type = Datatype::kFloat64;
  mesh.positions = bytesOf(positions);
  EXPECT_TRUE(validateMeshMap(mesh));

  const auto bytes = serializeMeshMap(mesh);
  auto decoded = deserializeMeshMap(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->position_type, Datatype::kFloat64);
  ASSERT_EQ(decoded->positions.size(), 3 * sizeof(double));
  double z = 0.0;
  std::memcpy(&z, decoded->positions.data() + (2 * sizeof(double)), sizeof(z));  // spans are not aligned
  EXPECT_DOUBLE_EQ(z, 3.25);
}

TEST(MeshMapCodecTest, SkipsUnknownFieldsForForwardCompatibility) {
  Fixture fixture;
  const auto bytes = appendUnknownVarint(serializeMeshMap(fixture.mesh), 15);
  auto decoded = deserializeMeshMap(bytes.data(), bytes.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->channels.size(), 4u);
}

TEST(MeshMapCodecTest, RejectsTruncation) {
  // Malformed known fields: RejectsEachMalformedKnownField.
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
  auto expect_rejected = [](auto mutate, const char* what) {
    UpdateFixture fixture;
    mutate(fixture.update);
    EXPECT_FALSE(validateMeshMapUpdate(fixture.update)) << what;
    const auto bytes = serializeMeshMapUpdate(fixture.update);
    EXPECT_FALSE(deserializeMeshMapUpdate(bytes.data(), bytes.size())) << what;
  };
  expect_rejected(
      [](MeshMapUpdate& u) { u.patches[0].indices = u.patches[0].indices.subspan(0, 6); }, "indices not uint32");
  expect_rejected(
      [](MeshMapUpdate& u) { u.patches[0].indices = u.patches[0].indices.subspan(0, 4); }, "1 index, 2 values");
  expect_rejected(
      [](MeshMapUpdate& u) {
        u.patches[1].channel.datatype = Datatype::kUint16;
        u.patches[1].channel.count = 3;
      },
      "2 bytes are not a whole element");
  expect_rejected(
      [](MeshMapUpdate& u) {
        u.patches[1].channel.name = "height";
        u.patches[1].channel.target = Target::kVertex;
      },
      "duplicate channel");
  expect_rejected([](MeshMapUpdate& u) { u.patches[1].channel.datatype = Datatype::kUnknown; }, "unknown datatype");

  EXPECT_TRUE(validateMeshMapUpdate(MeshMapUpdate{}));
}

// ---------- hand-written proto3 wire ----------
//
// These bytes come from outside the SDK's Writer, which writes every field: a
// standard proto3 encoder omits each field equal to its zero value.

/// Chaining wrapper over the test_pb builders, to write a message field by field.
struct Wire {
  std::vector<uint8_t> bytes;

  Wire& varint(uint32_t field, uint64_t value) {
    test_pb::appendTag(bytes, field, 0);
    test_pb::appendVarint(bytes, value);
    return *this;
  }
  Wire& len(uint32_t field, const std::vector<uint8_t>& payload) {
    test_pb::appendTag(bytes, field, 2);
    test_pb::appendLenDelim(bytes, payload);
    return *this;
  }
  Wire& str(uint32_t field, std::string_view text) {
    test_pb::appendTag(bytes, field, 2);
    test_pb::appendString(bytes, text);
    return *this;
  }
  Wire& msg(uint32_t field, const Wire& nested) {
    return len(field, nested.bytes);
  }
};

constexpr uint64_t kWireUint8 = 2;
constexpr uint64_t kWireFloat32 = 7;
constexpr uint64_t kBeyondUint32 = uint64_t{1} << 32U;  // truncates to 0 if not rejected
const std::vector<uint8_t> kBadTag = {0x00};            // a submessage whose first tag is field 0

template <typename T>
std::vector<uint8_t> leBytes(std::initializer_list<T> values) {
  std::vector<uint8_t> out(values.size() * sizeof(T));
  std::memcpy(out.data(), values.begin(), out.size());  // the test hosts are little-endian
  return out;
}

/// A valid one-triangle mesh.
Wire triangleMesh(bool with_position_type = true, uint64_t vertex_count = 3, uint64_t face_count = 1) {
  Wire mesh;
  mesh.varint(6, vertex_count);
  if (with_position_type) {
    mesh.varint(7, kWireFloat32);
  }
  mesh.len(8, leBytes<float>({0, 0, 0, 1, 0, 0, 0, 1, 0}));
  mesh.varint(9, face_count);
  mesh.len(10, leBytes<uint32_t>({0, 1, 2}));
  return mesh;
}

/// A float32 face channel valid for triangleMesh().
Wire faceChannel(std::string_view name = "c", uint64_t target = 1, uint64_t count = 1) {
  return Wire{}.str(1, name).varint(2, target).varint(3, kWireFloat32).varint(4, count).len(5, leBytes<float>({2.5F}));
}

Expected<MeshMap> decode(const Wire& wire) {
  return deserializeMeshMap(wire.bytes.data(), wire.bytes.size());
}

Expected<MeshMapUpdate> decodeUpdate(const Wire& wire) {
  return deserializeMeshMapUpdate(wire.bytes.data(), wire.bytes.size());
}

template <typename T>
std::string errorOf(const Expected<T>& result) {
  return result ? std::string("decoded") : result.error();
}

TEST(MeshMapCodecTest, DecodesCanonicalProto3WithDefaultsElided) {
  // protoc --encode=PJ.MeshMap pj/MeshMap.proto of:
  //   vertex_count: 3  position_type: FLOAT32  face_count: 1
  //   positions: <float32 0,0,0 1,0,0 0,1,0>  indices: <uint32 0,1,2>
  //   channels { name: "h" target: VERTEX datatype: UINT8 count: 1 data: "\001\002\003" }
  // `target: VERTEX` is the zero value, so the channel carries no field 2.
  const std::vector<uint8_t> golden = {
      0x30, 0x03, 0x38, 0x07, 0x42, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x48, 0x01, 0x52, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x5a, 0x0c, 0x0a, 0x01, 0x68, 0x18, 0x02, 0x20, 0x01, 0x2a, 0x03, 0x01, 0x02, 0x03};
  auto decoded = deserializeMeshMap(golden.data(), golden.size());
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->position_type, Datatype::kFloat32);
  ASSERT_EQ(decoded->channels.size(), 1u);
  EXPECT_EQ(decoded->channels[0].target, Target::kVertex);
  EXPECT_EQ(decoded->channels[0].datatype, Datatype::kUint8);
  EXPECT_EQ(decoded->channels[0].count, 1u);

  // Nested messages with every field elided read as their documented defaults:
  // an empty ChannelCategory and an empty MeshFaceLink.
  Wire mesh = triangleMesh();
  mesh.msg(
      11, Wire{}
              .str(1, "v")
              .varint(3, kWireFloat32)
              .varint(4, 1)
              .len(5, leBytes<float>({1, 2, 3}))
              .msg(6, Wire{}.msg(4, Wire{})));
  mesh.msg(12, Wire{});
  auto defaults = decode(mesh);
  ASSERT_TRUE(defaults) << defaults.error();
  const MeshChannel& channel = defaults->channels.at(0);
  EXPECT_EQ(channel.target, Target::kVertex);
  EXPECT_EQ(channel.info.kind, ChannelKind::kContinuous);
  EXPECT_FALSE(channel.info.no_data.has_value());
  ASSERT_EQ(channel.info.categories.size(), 1u);
  EXPECT_EQ(channel.info.categories[0].value, 0);
  EXPECT_FALSE(channel.info.categories[0].color.has_value());
  ASSERT_EQ(defaults->links.size(), 1u);
  EXPECT_EQ(defaults->links[0].kind, 0u);
  EXPECT_FALSE(defaults->links[0].bidirectional);
  EXPECT_TRUE(std::isnan(defaults->links[0].cost));
}

TEST(MeshMapCodecTest, RejectsMissingDatatypeCountOrPositionType) {
  const auto no_datatype =
      decode(triangleMesh().msg(11, Wire{}.str(1, "c").varint(2, 1).varint(4, 1).len(5, leBytes<float>({2.5F}))));
  EXPECT_EQ(errorOf(no_datatype), "MeshMap: channel 'c' has an unknown datatype");

  const auto no_count = decode(
      triangleMesh().msg(11, Wire{}.str(1, "c").varint(2, 1).varint(3, kWireFloat32).len(5, leBytes<float>({2.5F}))));
  EXPECT_EQ(errorOf(no_count), "MeshMap: channel 'c' has a zero count");

  EXPECT_EQ(errorOf(decode(triangleMesh(false))), "MeshMap: position_type must be float32 or float64");
}

TEST(MeshMapCodecTest, RejectsEachMalformedKnownField) {
  ASSERT_TRUE(decode(triangleMesh().msg(11, faceChannel()).msg(12, Wire{}.varint(3, 7))));  // the valid base

  const std::pair<const char*, Wire> cases[] = {
      {"channel target 7", triangleMesh().msg(11, faceChannel("c", 7))},
      {"channel count beyond uint32", triangleMesh().msg(11, faceChannel("c", 1, kBeyondUint32 + 1))},
      {"vertex_count beyond uint32", triangleMesh(true, kBeyondUint32 + 3)},
      {"face_count beyond uint32", triangleMesh(true, 3, kBeyondUint32 + 1)},
      {"link from_face beyond uint32", triangleMesh().msg(12, Wire{}.varint(1, kBeyondUint32))},
      {"link to_face beyond uint32", triangleMesh().msg(12, Wire{}.varint(2, kBeyondUint32))},
      {"link kind beyond uint32", triangleMesh().msg(12, Wire{}.varint(3, kBeyondUint32 + 7))},
      {"malformed channel info", triangleMesh().msg(11, faceChannel().len(6, kBadTag))},
      {"malformed category", triangleMesh().msg(11, faceChannel().msg(6, Wire{}.len(4, kBadTag)))},
      {"malformed link", triangleMesh().len(12, kBadTag)},
      {"malformed origin", triangleMesh().len(3, kBadTag)},
      {"malformed timestamp", triangleMesh().len(1, kBadTag)},
  };
  for (const auto& [what, wire] : cases) {
    EXPECT_EQ(errorOf(decode(wire)), "MeshMap wire: decode failed") << what;
  }
}

TEST(MeshMapCodecTest, ReadsAChannelKindItDoesNotKnowAsContinuous) {
  const auto decoded = decode(triangleMesh().msg(11, faceChannel().msg(6, Wire{}.varint(3, 2))));
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(decoded->channels.at(0).info.kind, ChannelKind::kContinuous);
}

TEST(MeshMapCodecTest, RejectsAnUnnamedChannelWhileParsing) {
  // The parser rejects it (decode failed), before validation would.
  EXPECT_EQ(errorOf(decode(triangleMesh().msg(11, Wire{}).msg(11, faceChannel()))), "MeshMap wire: decode failed");

  Wire many = triangleMesh();
  for (int i = 0; i < 100'000; ++i) {
    many.msg(11, Wire{});
  }
  EXPECT_EQ(errorOf(decode(many)), "MeshMap wire: decode failed");
}

TEST(MeshMapUpdateCodecTest, DecodesCanonicalProto3WithDefaultsElided) {
  // protoc --encode=PJ.MeshMapUpdate pj/MeshMapUpdate.proto of:
  //   patches { channel { name: "h" target: VERTEX datatype: UINT8 count: 1 data: "\t" }
  //             indices: "\002\000\000\000" }
  const std::vector<uint8_t> golden = {0x2a, 0x12, 0x0a, 0x0a, 0x0a, 0x01, 0x68, 0x18, 0x02, 0x20,
                                       0x01, 0x2a, 0x01, 0x09, 0x12, 0x04, 0x02, 0x00, 0x00, 0x00};
  auto decoded = deserializeMeshMapUpdate(golden.data(), golden.size());
  ASSERT_TRUE(decoded) << decoded.error();
  ASSERT_EQ(decoded->patches.size(), 1u);
  EXPECT_EQ(decoded->patches[0].channel.target, Target::kVertex);
  EXPECT_EQ(decoded->patches[0].channel.datatype, Datatype::kUint8);

  const auto no_count = decodeUpdate(
      Wire{}.msg(
          5, Wire{}.msg(1, Wire{}.str(1, "h").varint(3, kWireUint8).len(5, {9})).len(2, leBytes<uint32_t>({2}))));
  EXPECT_EQ(errorOf(no_count), "MeshMapUpdate: channel 'h' has a zero count");
}

TEST(MeshMapUpdateCodecTest, RejectsEachMalformedKnownField) {
  const Wire valid_patch = Wire{}.msg(1, faceChannel());
  ASSERT_TRUE(decodeUpdate(Wire{}.msg(5, valid_patch)));

  const std::pair<const char*, Wire> cases[] = {
      {"patch channel target 7", Wire{}.msg(5, Wire{}.msg(1, faceChannel("c", 7)))},
      {"malformed patch", Wire{}.msg(5, valid_patch).len(5, kBadTag)},
      {"malformed patch channel", Wire{}.msg(5, Wire{}.msg(1, faceChannel().len(6, kBadTag)))},
      {"malformed timestamp", Wire{}.msg(5, valid_patch).len(1, kBadTag)},
      {"unnamed patch channel first", Wire{}.msg(5, Wire{}).msg(5, valid_patch)},
  };
  for (const auto& [what, wire] : cases) {
    EXPECT_EQ(errorOf(decodeUpdate(wire)), "MeshMapUpdate wire: decode failed") << what;
  }
}

}  // namespace
}  // namespace PJ
