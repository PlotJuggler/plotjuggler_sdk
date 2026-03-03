#include "PJ/base/type_tree.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PJ {
namespace {

void flatten_impl(const TypeTreeNode& node, std::string_view prefix, std::vector<std::string>& out) {
  std::string current_path = prefix.empty() ? node.name : std::string(prefix) + "." + node.name;

  if (node.kind != TypeKind::kStruct) {
    out.push_back(std::move(current_path));
    return;
  }
  for (const auto& child : node.children) {
    flatten_impl(*child, current_path, out);
  }
}

std::size_t count_leaf_fields_impl(const TypeTreeNode& node) {
  if (node.kind != TypeKind::kStruct) {
    return 1;
  }
  std::size_t count = 0;
  for (const auto& child : node.children) {
    count += count_leaf_fields_impl(*child);
  }
  return count;
}

}  // namespace

std::shared_ptr<TypeTreeNode> make_primitive(std::string name, PrimitiveType type) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kPrimitive,
      .primitive_type = type,
  });
}

std::shared_ptr<TypeTreeNode> make_struct(std::string name, std::vector<std::shared_ptr<TypeTreeNode>> children) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kStruct,
      .children = std::move(children),
  });
}

std::shared_ptr<TypeTreeNode> make_array(
    std::string name, std::shared_ptr<TypeTreeNode> element_type, std::optional<uint32_t> fixed_size) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kArray,
      .element_type = std::move(element_type),
      .fixed_array_size = fixed_size,
  });
}

std::shared_ptr<TypeTreeNode> make_enum(std::string name, PrimitiveType underlying_type, EnumMapping mapping) {
  return std::make_shared<TypeTreeNode>(TypeTreeNode{
      .name = std::move(name),
      .kind = TypeKind::kEnum,
      .primitive_type = underlying_type,
      .enum_mapping = std::move(mapping),
  });
}

std::vector<std::string> flatten_field_paths(const TypeTreeNode& root) {
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

std::size_t count_leaf_fields(const TypeTreeNode& root) {
  return count_leaf_fields_impl(root);
}

}  // namespace PJ
