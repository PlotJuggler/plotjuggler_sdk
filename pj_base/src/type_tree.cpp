#include "pj_base/type_tree.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PJ {
namespace {

void flatten_impl(const TypeTreeNode& node, std::string_view prefix, std::vector<std::string>& out) {
  std::string current_path = prefix.empty() ? node.name : std::string(prefix) + "." + node.name;

  switch (node.kind) {
    case TypeKind::kStruct:
      for (const auto& child : node.children) {
        flatten_impl(*child, current_path, out);
      }
      break;
    case TypeKind::kArray:
      if (node.element_type && node.fixed_array_size.has_value()) {
        for (uint32_t idx = 0; idx < *node.fixed_array_size; ++idx) {
          std::string indexed = current_path + "[" + std::to_string(idx) + "]";
          flatten_impl(*node.element_type, indexed, out);
        }
      }
      // Dynamic arrays (no fixed_array_size) produce 0 paths
      break;
    default:
      // Primitive, enum — leaf node
      out.push_back(std::move(current_path));
      break;
  }
}

std::size_t count_leaf_fields_impl(const TypeTreeNode& node) {
  switch (node.kind) {
    case TypeKind::kStruct: {
      std::size_t count = 0;
      for (const auto& child : node.children) {
        count += count_leaf_fields_impl(*child);
      }
      return count;
    }
    case TypeKind::kArray:
      if (node.element_type && node.fixed_array_size.has_value()) {
        return *node.fixed_array_size * count_leaf_fields_impl(*node.element_type);
      }
      return 0;  // dynamic array: 0 columns until expanded
    default:
      return 1;  // primitive, enum
  }
}

}  // namespace

std::shared_ptr<TypeTreeNode> makePrimitive(std::string name, PrimitiveType type) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kPrimitive,
      .semantic_tags = {},
      .primitive_type = type,
      .children = {},
      .element_type = {},
      .fixed_array_size = {},
      .enum_mapping = {},
  });
}

std::shared_ptr<TypeTreeNode> makeStruct(std::string name, std::vector<std::shared_ptr<TypeTreeNode>> children) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kStruct,
      .semantic_tags = {},
      .primitive_type = {},
      .children = std::move(children),
      .element_type = {},
      .fixed_array_size = {},
      .enum_mapping = {},
  });
}

std::shared_ptr<TypeTreeNode> makeArray(
    std::string name, std::shared_ptr<TypeTreeNode> element_type, std::optional<uint32_t> fixed_size) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kArray,
      .semantic_tags = {},
      .primitive_type = {},
      .children = {},
      .element_type = std::move(element_type),
      .fixed_array_size = fixed_size,
      .enum_mapping = {},
  });
}

std::shared_ptr<TypeTreeNode> makeEnum(std::string name, PrimitiveType underlying_type, EnumMapping mapping) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kEnum,
      .semantic_tags = {},
      .primitive_type = underlying_type,
      .children = {},
      .element_type = {},
      .fixed_array_size = {},
      .enum_mapping = std::move(mapping),
  });
}

std::vector<std::string> flattenFieldPaths(const TypeTreeNode& root) {
  std::vector<std::string> result;
  if (root.kind != TypeKind::kStruct) {
    result.emplace_back(root.name);
    return result;
  }
  // Skip root struct name -- children use empty prefix
  for (const auto& child : root.children) {
    flatten_impl(*child, "", result);
  }
  return result;
}

std::size_t countLeafFields(const TypeTreeNode& root) {
  return count_leaf_fields_impl(root);
}

}  // namespace PJ
