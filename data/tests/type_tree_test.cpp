#include "PJ/base/type_tree.hpp"

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
  auto position = make_struct(
      "position", {
                      make_primitive("x", PrimitiveType::kFloat32),
                      make_primitive("y", PrimitiveType::kFloat32),
                      make_primitive("z", PrimitiveType::kFloat32),
                  });

  auto rotation = std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = "rotation",
      .kind = TypeKind::kStruct,
      .semantic_tags = {"quaternion"},
      .children =
          {
              make_primitive("w", PrimitiveType::kFloat32),
              make_primitive("x", PrimitiveType::kFloat32),
              make_primitive("y", PrimitiveType::kFloat32),
              make_primitive("z", PrimitiveType::kFloat32),
          },
  });

  return make_struct(
      "Pose", {
                  make_primitive("frame_name", PrimitiveType::kString),
                  position,
                  rotation,
              });
}

// ---------- Test 1: flatten_field_paths on robot_pose ----------

TEST(TypeTreeTest, FlattenFieldPathsRobotPose) {
  auto pose = make_robot_pose();
  auto paths = flatten_field_paths(*pose);

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
  EXPECT_EQ(count_leaf_fields(*pose), 8u);
}

// ---------- Test 3: factory functions set correct TypeKind ----------

TEST(TypeTreeTest, MakePrimitiveSetsKind) {
  auto node = make_primitive("x", PrimitiveType::kFloat32);
  EXPECT_EQ(node->kind, TypeKind::kPrimitive);
  EXPECT_EQ(node->name, "x");
  ASSERT_TRUE(node->primitive_type.has_value());
  EXPECT_EQ(node->primitive_type.value(), PrimitiveType::kFloat32);
}

TEST(TypeTreeTest, MakeStructSetsKind) {
  auto node = make_struct("s", {});
  EXPECT_EQ(node->kind, TypeKind::kStruct);
  EXPECT_EQ(node->name, "s");
}

TEST(TypeTreeTest, MakeArraySetsKind) {
  auto elem = make_primitive("elem", PrimitiveType::kFloat64);
  auto node = make_array("arr", elem);
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

  auto node = make_enum("state", PrimitiveType::kInt32, std::move(mapping));
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
  auto elem = make_primitive("elem", PrimitiveType::kFloat32);
  auto node = make_array("data", elem, 10);
  ASSERT_TRUE(node->fixed_array_size.has_value());
  EXPECT_EQ(node->fixed_array_size.value(), 10u);
}

TEST(TypeTreeTest, MakeArrayWithoutFixedSize) {
  auto elem = make_primitive("elem", PrimitiveType::kFloat32);
  auto node = make_array("data", elem);
  EXPECT_FALSE(node->fixed_array_size.has_value());
}

// ---------- Test 6: make_enum mapping roundtrips ----------

TEST(TypeTreeTest, MakeEnumMappingRoundtrips) {
  EnumMapping mapping;
  mapping.value_to_name[0] = "OFF";
  mapping.value_to_name[1] = "ON";
  mapping.name_to_value["OFF"] = 0;
  mapping.name_to_value["ON"] = 1;

  auto node = make_enum("switch", PrimitiveType::kInt32, std::move(mapping));

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
  auto node = make_enum("e", PrimitiveType::kUint8, std::move(mapping));
  ASSERT_TRUE(node->primitive_type.has_value());
  EXPECT_EQ(node->primitive_type.value(), PrimitiveType::kUint8);
}

// ---------- Test 7: flatten on a primitive-only root ----------

TEST(TypeTreeTest, FlattenPrimitiveRoot) {
  auto node = make_primitive("temperature", PrimitiveType::kFloat64);
  auto paths = flatten_field_paths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "temperature");
}

// ---------- Test 8: flatten on an enum root ----------

TEST(TypeTreeTest, FlattenEnumRoot) {
  EnumMapping mapping;
  auto node = make_enum("state", PrimitiveType::kInt32, std::move(mapping));
  auto paths = flatten_field_paths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "state");
}

// ---------- Test 9: flatten on an array root ----------

TEST(TypeTreeTest, FlattenArrayRoot) {
  auto elem = make_primitive("elem", PrimitiveType::kFloat32);
  auto node = make_array("sensor_data", elem, 100);
  auto paths = flatten_field_paths(*node);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "sensor_data");
}

// ---------- Test 10: count_leaf_fields on non-struct roots ----------

TEST(TypeTreeTest, CountLeafFieldsPrimitive) {
  auto node = make_primitive("x", PrimitiveType::kFloat32);
  EXPECT_EQ(count_leaf_fields(*node), 1u);
}

TEST(TypeTreeTest, CountLeafFieldsEmptyStruct) {
  auto node = make_struct("empty", {});
  EXPECT_EQ(count_leaf_fields(*node), 0u);
}

// ---------- Test 11: deeply nested struct ----------

TEST(TypeTreeTest, DeeplyNestedStruct) {
  auto inner = make_struct(
      "inner", {
                   make_primitive("val", PrimitiveType::kInt32),
               });
  auto middle = make_struct("middle", {inner});
  auto outer = make_struct("outer", {middle});

  auto paths = flatten_field_paths(*outer);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "middle.inner.val");

  EXPECT_EQ(count_leaf_fields(*outer), 1u);
}

}  // namespace
}  // namespace PJ
