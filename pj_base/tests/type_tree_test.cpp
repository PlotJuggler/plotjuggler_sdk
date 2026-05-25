// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/type_tree.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace PJ {
namespace {

// Helper: build the canonical robot_pose type tree used in multiple tests.
//
// Pose (struct)
//   frame_name: string
//   position: struct {x: float32, y: float32, z: float32}
//   rotation: struct {w: float32, x: float32, y: float32, z: float32}
//             semantic_tags = {"quaternion"}
std::shared_ptr<TypeTreeNode> make_robot_pose() {
  auto position = makeStruct(
      "position", {
                      makePrimitive("x", PrimitiveType::kFloat32),
                      makePrimitive("y", PrimitiveType::kFloat32),
                      makePrimitive("z", PrimitiveType::kFloat32),
                  });

  auto rotation = std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = "rotation",
      .kind = TypeKind::kStruct,
      .semantic_tags = {"quaternion"},
      .children =
          {
              makePrimitive("w", PrimitiveType::kFloat32),
              makePrimitive("x", PrimitiveType::kFloat32),
              makePrimitive("y", PrimitiveType::kFloat32),
              makePrimitive("z", PrimitiveType::kFloat32),
          },
  });

  return makeStruct(
      "Pose", {
                  makePrimitive("frame_name", PrimitiveType::kString),
                  position,
                  rotation,
              });
}

// ---------- Test 1: flatten_field_paths on robot_pose ----------

TEST(TypeTreeTest, FlattenFieldPathsRobotPose) {
  auto pose = make_robot_pose();
  auto paths = flattenFieldPaths(*pose);

  const std::vector<std::string> expected = {
      "frame_name", "position.x", "position.y", "position.z", "rotation.w", "rotation.x", "rotation.y", "rotation.z",
  };

  ASSERT_EQ(paths.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(paths[i], expected[i]) << "mismatch at index " << i;
  }
}

// ---------- Test 2: count_leaf_fields on robot_pose ----------

TEST(TypeTreeTest, CountLeafFieldsRobotPose) {
  auto pose = make_robot_pose();
  EXPECT_EQ(countLeafFields(*pose), 8u);
}

// ---------- Test 3: factory functions set correct TypeKind ----------

TEST(TypeTreeTest, MakePrimitiveSetsKind) {
  auto node = makePrimitive("x", PrimitiveType::kFloat32);
  EXPECT_EQ(node->kind, TypeKind::kPrimitive);
  EXPECT_EQ(node->name, "x");
  ASSERT_TRUE(node->primitive_type.has_value());
  EXPECT_EQ(node->primitive_type.value(), PrimitiveType::kFloat32);
}

TEST(TypeTreeTest, MakeStructSetsKind) {
  auto node = makeStruct("s", {});
  EXPECT_EQ(node->kind, TypeKind::kStruct);
  EXPECT_EQ(node->name, "s");
}

TEST(TypeTreeTest, MakeArraySetsKind) {
  auto elem = makePrimitive("elem", PrimitiveType::kFloat64);
  auto node = makeArray("arr", elem);
  EXPECT_EQ(node->kind, TypeKind::kArray);
  EXPECT_EQ(node->name, "arr");
  EXPECT_EQ(node->element_type, elem);
}

TEST(TypeTreeTest, MakeEnumSetsKind) {
  EnumMapping mapping;
  mapping.value_to_name[0] = "OFF";
  mapping.value_to_name[1] = "ON";
  mapping.name_to_value["OFF"] = 0;
  mapping.name_to_value["ON"] = 1;

  auto node = makeEnum("state", PrimitiveType::kInt32, std::move(mapping));
  EXPECT_EQ(node->kind, TypeKind::kEnum);
  EXPECT_EQ(node->name, "state");
}

// ---------- Test 4: semantic tags are preserved ----------

TEST(TypeTreeTest, SemanticTagsPreserved) {
  auto pose = make_robot_pose();
  // rotation is the third child (index 2)
  ASSERT_GE(pose->children.size(), 3u);
  const auto& rotation = pose->children[2];
  EXPECT_TRUE(rotation->semantic_tags.contains("quaternion"));
  EXPECT_FALSE(rotation->semantic_tags.contains("pose"));
}

// ---------- Test 5: make_array with fixed size ----------

TEST(TypeTreeTest, MakeArrayWithFixedSize) {
  auto elem = makePrimitive("elem", PrimitiveType::kFloat32);
  auto node = makeArray("data", elem, 10);
  ASSERT_TRUE(node->fixed_array_size.has_value());
  EXPECT_EQ(node->fixed_array_size.value(), 10u);
}

TEST(TypeTreeTest, MakeArrayWithoutFixedSize) {
  auto elem = makePrimitive("elem", PrimitiveType::kFloat32);
  auto node = makeArray("data", elem);
  EXPECT_FALSE(node->fixed_array_size.has_value());
}

// ---------- Test 6: make_enum mapping roundtrips ----------

TEST(TypeTreeTest, MakeEnumMappingRoundtrips) {
  EnumMapping mapping;
  mapping.value_to_name[0] = "OFF";
  mapping.value_to_name[1] = "ON";
  mapping.name_to_value["OFF"] = 0;
  mapping.name_to_value["ON"] = 1;

  auto node = makeEnum("switch", PrimitiveType::kInt32, std::move(mapping));

  ASSERT_TRUE(node->enum_mapping.has_value());
  const auto& em = node->enum_mapping.value();

  // value -> name
  ASSERT_TRUE(em.value_to_name.contains(0));
  EXPECT_EQ(em.value_to_name.at(0), "OFF");
  ASSERT_TRUE(em.value_to_name.contains(1));
  EXPECT_EQ(em.value_to_name.at(1), "ON");

  // name -> value
  ASSERT_TRUE(em.name_to_value.contains("OFF"));
  EXPECT_EQ(em.name_to_value.at("OFF"), 0);
  ASSERT_TRUE(em.name_to_value.contains("ON"));
  EXPECT_EQ(em.name_to_value.at("ON"), 1);
}

TEST(TypeTreeTest, MakeEnumStoresUnderlyingType) {
  EnumMapping mapping;
  auto node = makeEnum("e", PrimitiveType::kUint8, std::move(mapping));
  ASSERT_TRUE(node->primitive_type.has_value());
  EXPECT_EQ(node->primitive_type.value(), PrimitiveType::kUint8);
}

// ---------- Test 7: flatten on a primitive-only root ----------

TEST(TypeTreeTest, FlattenPrimitiveRoot) {
  auto node = makePrimitive("temperature", PrimitiveType::kFloat64);
  auto paths = flattenFieldPaths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "temperature");
}

// ---------- Test 8: flatten on an enum root ----------

TEST(TypeTreeTest, FlattenEnumRoot) {
  EnumMapping mapping;
  auto node = makeEnum("state", PrimitiveType::kInt32, std::move(mapping));
  auto paths = flattenFieldPaths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "state");
}

// ---------- Test 9: flatten on an array root ----------

TEST(TypeTreeTest, FlattenArrayRoot) {
  auto elem = makePrimitive("elem", PrimitiveType::kFloat32);
  auto node = makeArray("sensor_data", elem, 100);
  auto paths = flattenFieldPaths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "sensor_data");
}

// ---------- Test 10: count_leaf_fields on non-struct roots ----------

TEST(TypeTreeTest, CountLeafFieldsPrimitive) {
  auto node = makePrimitive("x", PrimitiveType::kFloat32);
  EXPECT_EQ(countLeafFields(*node), 1u);
}

TEST(TypeTreeTest, CountLeafFieldsEmptyStruct) {
  auto node = makeStruct("empty", {});
  EXPECT_EQ(countLeafFields(*node), 0u);
}

// ---------- Test 11: deeply nested struct ----------

TEST(TypeTreeTest, DeeplyNestedStruct) {
  auto inner = makeStruct(
      "inner", {
                   makePrimitive("val", PrimitiveType::kInt32),
               });
  auto middle = makeStruct("middle", {inner});
  auto outer = makeStruct("outer", {middle});

  auto paths = flattenFieldPaths(*outer);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "middle.inner.val");

  EXPECT_EQ(countLeafFields(*outer), 1u);
}

// ---------- BUG-24: countLeafFields counts kArray as 1 instead of recursing ----------

TEST(TypeTreeTest, CountLeafFieldsFixedArray) {
  // A fixed array of 4 float32 elements should count as 4 leaf fields
  auto elem = makePrimitive("elem", PrimitiveType::kFloat32);
  auto arr = makeArray("data", elem, 4);
  auto root = makeStruct("Root", {arr});
  EXPECT_EQ(countLeafFields(*root), 4u);
}

TEST(TypeTreeTest, CountLeafFieldsFixedArrayOfStruct) {
  // A fixed array of 3 structs with 2 fields each = 6 leaf fields
  auto inner = makeStruct(
      "point", {
                   makePrimitive("x", PrimitiveType::kFloat32),
                   makePrimitive("y", PrimitiveType::kFloat32),
               });
  auto arr = makeArray("points", inner, 3);
  auto root = makeStruct("Root", {arr});
  EXPECT_EQ(countLeafFields(*root), 6u);
}

TEST(TypeTreeTest, CountLeafFieldsDynamicArray) {
  // A dynamic (no fixed size) array should count as 0 leaf fields
  auto elem = makePrimitive("elem", PrimitiveType::kFloat32);
  auto arr = makeArray("data", elem);
  auto root = makeStruct("Root", {arr});
  EXPECT_EQ(countLeafFields(*root), 0u);
}

// ---------- BUG-25: flattenFieldPaths should expand fixed arrays ----------

TEST(TypeTreeTest, FlattenFieldPathsFixedArray) {
  auto elem = makePrimitive("v", PrimitiveType::kFloat32);
  auto arr = makeArray("data", elem, 3);
  auto root = makeStruct(
      "Root", {
                  makePrimitive("id", PrimitiveType::kInt32),
                  arr,
              });
  auto paths = flattenFieldPaths(*root);
  // Expect: id, data[0].v, data[1].v, data[2].v
  ASSERT_EQ(paths.size(), 4u);
  EXPECT_EQ(paths[0], "id");
  EXPECT_EQ(paths[1], "data[0].v");
  EXPECT_EQ(paths[2], "data[1].v");
  EXPECT_EQ(paths[3], "data[2].v");
}

}  // namespace
}  // namespace PJ
