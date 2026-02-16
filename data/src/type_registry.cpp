#include "pj/engine/type_registry.hpp"

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace pj::engine {
namespace {

// Flatten a type tree into leaf paths paired with their PrimitiveType.
// For primitives: uses primitive_type directly.
// For enums: uses the underlying primitive_type.
// For arrays: uses the element_type's primitive_type (if primitive/enum).
// For structs: recurses into children.
void flatten_leaf_types_impl(
    const TypeTreeNode& node, std::string_view prefix,
    std::vector<std::pair<std::string, PrimitiveType>>& out) {
  std::string current_path =
      prefix.empty() ? node.name : absl::StrCat(prefix, ".", node.name);

  switch (node.kind) {
    case TypeKind::kPrimitive:
      if (node.primitive_type.has_value()) {
        out.emplace_back(std::move(current_path), *node.primitive_type);
      }
      return;
    case TypeKind::kEnum:
      if (node.primitive_type.has_value()) {
        out.emplace_back(std::move(current_path), *node.primitive_type);
      }
      return;
    case TypeKind::kArray:
      // Treat the array itself as a leaf node with its element's type
      if (node.element_type && node.element_type->primitive_type.has_value()) {
        out.emplace_back(std::move(current_path),
                         *node.element_type->primitive_type);
      }
      return;
    case TypeKind::kStruct:
      for (const auto& child : node.children) {
        flatten_leaf_types_impl(*child, current_path, out);
      }
      return;
  }
}

// Flatten starting from root, skipping the root struct name (same convention
// as flatten_field_paths).
std::vector<std::pair<std::string, PrimitiveType>> flatten_leaf_types(
    const TypeTreeNode& root) {
  std::vector<std::pair<std::string, PrimitiveType>> result;
  if (root.kind != TypeKind::kStruct) {
    if (root.primitive_type.has_value()) {
      result.emplace_back(root.name, *root.primitive_type);
    }
    return result;
  }
  for (const auto& child : root.children) {
    flatten_leaf_types_impl(*child, "", result);
  }
  return result;
}

}  // namespace

absl::StatusOr<SchemaId> TypeRegistry::register_schema(
    std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree) {
  if (name_to_id_.contains(schema_name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Schema '", schema_name, "' already registered"));
  }
  SchemaId id = next_id_++;
  name_to_id_.emplace(schema_name, id);
  schemas_.emplace(id, std::move(type_tree));
  return id;
}

absl::StatusOr<SchemaId> TypeRegistry::register_or_get(
    std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree) {
  auto it = name_to_id_.find(schema_name);
  if (it != name_to_id_.end()) {
    return it->second;
  }
  return register_schema(std::move(schema_name), std::move(type_tree));
}

const TypeTreeNode* TypeRegistry::lookup(SchemaId id) const {
  auto it = schemas_.find(id);
  if (it == schemas_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::optional<SchemaId> TypeRegistry::find_by_name(
    std::string_view name) const {
  auto it = name_to_id_.find(name);
  if (it == name_to_id_.end()) {
    return std::nullopt;
  }
  return it->second;
}

absl::Status TypeRegistry::evolve_schema(
    SchemaId id, std::shared_ptr<TypeTreeNode> updated_tree) {
  auto it = schemas_.find(id);
  if (it == schemas_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Schema ID ", id, " not found"));
  }

  const auto& old_tree = it->second;
  auto old_leaves = flatten_leaf_types(*old_tree);
  auto new_leaves = flatten_leaf_types(*updated_tree);

  // Build a map from path -> PrimitiveType for the new tree
  absl::flat_hash_map<std::string, PrimitiveType> new_leaf_map;
  new_leaf_map.reserve(new_leaves.size());
  for (auto& [path, ptype] : new_leaves) {
    new_leaf_map.emplace(std::move(path), ptype);
  }

  // Every old leaf must exist in the new tree with the same type
  for (const auto& [old_path, old_type] : old_leaves) {
    auto new_it = new_leaf_map.find(old_path);
    if (new_it == new_leaf_map.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Field '", old_path,
                       "' was removed in the updated schema"));
    }
    if (new_it->second != old_type) {
      return absl::InvalidArgumentError(
          absl::StrCat("Field '", old_path,
                       "' changed type in the updated schema"));
    }
  }

  // Validation passed — replace with updated tree
  it->second = std::move(updated_tree);
  return absl::OkStatus();
}

}  // namespace pj::engine
