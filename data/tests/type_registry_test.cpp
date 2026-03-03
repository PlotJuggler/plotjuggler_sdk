#include "PJ/engine/type_registry.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "PJ/base/expected.hpp"
#include "PJ/base/type_tree.hpp"
#include "PJ/base/types.hpp"

namespace PJ::engine {
namespace {

// Helper: build a simple struct with two float64 fields (x, y)
std::shared_ptr<TypeTreeNode> make_point_schema() {
  return make_struct(
      "Point", {
                   make_primitive("x", PrimitiveType::kFloat64),
                   make_primitive("y", PrimitiveType::kFloat64),
               });
}

// Helper: build a struct with three float64 fields (x, y, z)
std::shared_ptr<TypeTreeNode> make_point3d_schema() {
  return make_struct(
      "Point3D", {
                     make_primitive("x", PrimitiveType::kFloat64),
                     make_primitive("y", PrimitiveType::kFloat64),
                     make_primitive("z", PrimitiveType::kFloat64),
                 });
}

// 1. Register a schema, lookup by ID: returns correct tree
TEST(TypeRegistryTest, RegisterAndLookupById) {
  TypeRegistry registry;
  auto tree = make_point_schema();
  auto* raw_ptr = tree.get();

  auto result = registry.register_schema("Point", tree);
  ASSERT_TRUE(result.has_value()) << result.error();

  const TypeTreeNode* looked_up = registry.lookup(*result);
  ASSERT_NE(looked_up, nullptr);
  EXPECT_EQ(looked_up, raw_ptr);
  EXPECT_EQ(looked_up->name, "Point");
  EXPECT_EQ(looked_up->kind, TypeKind::kStruct);
  EXPECT_EQ(looked_up->children.size(), 2);
}

// 2. Register a schema, find by name: returns correct ID
TEST(TypeRegistryTest, RegisterAndFindByName) {
  TypeRegistry registry;
  auto tree = make_point_schema();

  auto result = registry.register_schema("Point", tree);
  ASSERT_TRUE(result.has_value()) << result.error();

  auto found = registry.find_by_name("Point");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, *result);
}

// 3. Register duplicate name: returns AlreadyExistsError
TEST(TypeRegistryTest, RegisterDuplicateNameFails) {
  TypeRegistry registry;

  auto result1 = registry.register_schema("Point", make_point_schema());
  ASSERT_TRUE(result1.has_value()) << result1.error();

  auto result2 = registry.register_schema("Point", make_point_schema());
  ASSERT_FALSE(result2.has_value());
}

// 4. register_or_get with new name: registers and returns ID
TEST(TypeRegistryTest, RegisterOrGetNewName) {
  TypeRegistry registry;

  auto result = registry.register_or_get("Point", make_point_schema());
  ASSERT_TRUE(result.has_value()) << result.error();

  // Verify it was actually registered
  auto found = registry.find_by_name("Point");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, *result);

  const TypeTreeNode* looked_up = registry.lookup(*result);
  ASSERT_NE(looked_up, nullptr);
  EXPECT_EQ(looked_up->name, "Point");
}

// 5. register_or_get with existing name: returns existing ID
TEST(TypeRegistryTest, RegisterOrGetExistingName) {
  TypeRegistry registry;

  auto result1 = registry.register_schema("Point", make_point_schema());
  ASSERT_TRUE(result1.has_value()) << result1.error();

  // register_or_get should return the same ID, ignoring the new tree
  auto result2 = registry.register_or_get("Point", make_point3d_schema());
  ASSERT_TRUE(result2.has_value()) << result2.error();
  EXPECT_EQ(*result1, *result2);

  // The original tree should still be the one stored (2 fields, not 3)
  const TypeTreeNode* looked_up = registry.lookup(*result2);
  ASSERT_NE(looked_up, nullptr);
  EXPECT_EQ(looked_up->children.size(), 2);
}

// 6. lookup with unknown ID: returns nullptr
TEST(TypeRegistryTest, LookupUnknownIdReturnsNullptr) {
  TypeRegistry registry;
  EXPECT_EQ(registry.lookup(999), nullptr);
}

// 7. find_by_name with unknown name: returns nullopt
TEST(TypeRegistryTest, FindByNameUnknownReturnsNullopt) {
  TypeRegistry registry;
  auto found = registry.find_by_name("NonExistent");
  EXPECT_FALSE(found.has_value());
}

// 8. evolve_schema with additive change: succeeds, lookup returns new tree
TEST(TypeRegistryTest, EvolveSchemaAdditiveChange) {
  TypeRegistry registry;

  auto original = make_point_schema();
  auto result = registry.register_schema("Point", original);
  ASSERT_TRUE(result.has_value()) << result.error();
  SchemaId id = *result;

  // Evolve: add a z field
  auto evolved = make_point3d_schema();
  auto* evolved_ptr = evolved.get();
  PJ::Status status = registry.evolve_schema(id, evolved);
  ASSERT_TRUE(status.has_value()) << status.error();

  // lookup should now return the evolved tree
  const TypeTreeNode* looked_up = registry.lookup(id);
  ASSERT_NE(looked_up, nullptr);
  EXPECT_EQ(looked_up, evolved_ptr);
  EXPECT_EQ(looked_up->children.size(), 3);
}

// 9. evolve_schema with removed field: returns InvalidArgumentError
TEST(TypeRegistryTest, EvolveSchemaRemovedFieldFails) {
  TypeRegistry registry;

  // Start with 3 fields
  auto original = make_point3d_schema();
  auto result = registry.register_schema("Point3D", original);
  ASSERT_TRUE(result.has_value()) << result.error();

  // Try to evolve to 2 fields (removing z)
  auto reduced = make_point_schema();
  PJ::Status status = registry.evolve_schema(*result, reduced);
  ASSERT_FALSE(status.has_value());
}

// 10. evolve_schema with type change on existing field: returns InvalidArgumentError
TEST(TypeRegistryTest, EvolveSchemaTypeChangeFails) {
  TypeRegistry registry;

  auto original = make_point_schema();  // x: float64, y: float64
  auto result = registry.register_schema("Point", original);
  ASSERT_TRUE(result.has_value()) << result.error();

  // Try to evolve: change x from float64 to int32
  auto changed = make_struct(
      "Point", {
                   make_primitive("x", PrimitiveType::kInt32),  // type changed!
                   make_primitive("y", PrimitiveType::kFloat64),
                   make_primitive("z", PrimitiveType::kFloat64),
               });
  PJ::Status status = registry.evolve_schema(*result, changed);
  ASSERT_FALSE(status.has_value());
}

// 11. evolve_schema with unknown ID: returns NotFoundError
TEST(TypeRegistryTest, EvolveSchemaUnknownIdFails) {
  TypeRegistry registry;

  PJ::Status status = registry.evolve_schema(999, make_point_schema());
  ASSERT_FALSE(status.has_value());
}

// 12. Multiple schemas: register 3, verify each has unique ID and correct tree
TEST(TypeRegistryTest, MultipleSchemas) {
  TypeRegistry registry;

  auto tree_a = make_primitive("temp", PrimitiveType::kFloat32);
  auto tree_b = make_point_schema();
  auto tree_c = make_struct(
      "Pose", {
                  make_primitive("frame", PrimitiveType::kString),
                  make_struct(
                      "position",
                      {
                          make_primitive("x", PrimitiveType::kFloat64),
                          make_primitive("y", PrimitiveType::kFloat64),
                          make_primitive("z", PrimitiveType::kFloat64),
                      }),
              });

  auto* raw_a = tree_a.get();
  auto* raw_b = tree_b.get();
  auto* raw_c = tree_c.get();

  auto id_a = registry.register_schema("Temperature", tree_a);
  auto id_b = registry.register_schema("Point", tree_b);
  auto id_c = registry.register_schema("Pose", tree_c);

  ASSERT_TRUE(id_a.has_value()) << id_a.error();
  ASSERT_TRUE(id_b.has_value()) << id_b.error();
  ASSERT_TRUE(id_c.has_value()) << id_c.error();

  // All IDs are unique
  EXPECT_NE(*id_a, *id_b);
  EXPECT_NE(*id_a, *id_c);
  EXPECT_NE(*id_b, *id_c);

  // Each lookup returns the correct tree
  EXPECT_EQ(registry.lookup(*id_a), raw_a);
  EXPECT_EQ(registry.lookup(*id_b), raw_b);
  EXPECT_EQ(registry.lookup(*id_c), raw_c);

  // find_by_name works for all
  auto found_a = registry.find_by_name("Temperature");
  auto found_b = registry.find_by_name("Point");
  auto found_c = registry.find_by_name("Pose");
  ASSERT_TRUE(found_a.has_value());
  ASSERT_TRUE(found_b.has_value());
  ASSERT_TRUE(found_c.has_value());
  EXPECT_EQ(*found_a, *id_a);
  EXPECT_EQ(*found_b, *id_b);
  EXPECT_EQ(*found_c, *id_c);
}

}  // namespace
}  // namespace PJ::engine
