#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// Logical scalar types supported by schema leaves.
enum class PrimitiveType : uint8_t {
  kFloat32,
  kFloat64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kBool,
  kString,
  kUnspecified = 0xFF,  ///< Untyped null — no column type specified.
};

/// Node category in a schema tree.
enum class TypeKind : uint8_t {
  kPrimitive,
  kStruct,
  kArray,
  kEnum,
};

/// Bi-directional mapping for enum logical types.
struct EnumMapping {
  /// Wire value -> symbolic name.
  std::unordered_map<int64_t, std::string> value_to_name;
  /// Symbolic name -> wire value.
  std::unordered_map<std::string, int64_t> name_to_value;
};

/// Recursive schema node used for topic type descriptions.
struct TypeTreeNode {
  /// Field/type name at this node.
  std::string name;
  /// Structural kind of this node.
  TypeKind kind;
  /// Optional semantic tags (e.g. "quaternion", "pose").
  std::unordered_set<std::string> semantic_tags;  // e.g., "quaternion", "pose"

  // kPrimitive
  /// Present for primitive leaves.
  std::optional<PrimitiveType> primitive_type;

  // kStruct: ordered child fields
  /// Ordered child fields for struct nodes.
  std::vector<std::shared_ptr<TypeTreeNode>> children;

  // kArray: element type + optional fixed size
  /// Element type for array nodes.
  std::shared_ptr<TypeTreeNode> element_type;
  /// Fixed element count for static arrays; unset for dynamic arrays.
  std::optional<uint32_t> fixed_array_size;

  // kEnum: wire-value <-> name mapping
  /// Enum mapping when kind is kEnum.
  std::optional<EnumMapping> enum_mapping;
};

// Factory functions
/// Create a primitive node.
[[nodiscard]] std::shared_ptr<TypeTreeNode> makePrimitive(std::string name, PrimitiveType type);

/// Create a struct node with ordered children.
[[nodiscard]] std::shared_ptr<TypeTreeNode> makeStruct(
    std::string name, std::vector<std::shared_ptr<TypeTreeNode>> children);

/// Create an array node.
[[nodiscard]] std::shared_ptr<TypeTreeNode> makeArray(
    std::string name, std::shared_ptr<TypeTreeNode> element_type, std::optional<uint32_t> fixed_size = std::nullopt);

/// Create an enum node over a primitive wire type.
[[nodiscard]] std::shared_ptr<TypeTreeNode> makeEnum(
    std::string name, PrimitiveType underlying_type, EnumMapping mapping);

// Flatten a type tree into ordered list of leaf field paths
// e.g., Pose -> ["frame_name", "position.x", "position.y", "position.z",
//                "rotation.w", "rotation.x", "rotation.y", "rotation.z"]
[[nodiscard]] std::vector<std::string> flattenFieldPaths(const TypeTreeNode& root);

// Count leaf (primitive/enum) fields in a type tree
[[nodiscard]] std::size_t countLeafFields(const TypeTreeNode& root);

}  // namespace PJ
