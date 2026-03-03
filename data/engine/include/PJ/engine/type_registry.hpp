#pragma once
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "PJ/base/expected.hpp"
#include "PJ/base/type_tree.hpp"
#include "PJ/base/types.hpp"

namespace PJ::engine {

class TypeRegistry {
 public:
  // Register a known schema (from Protobuf, ROS, etc.)
  // Fails if schema_name already exists.
  [[nodiscard]] PJ::Expected<PJ::SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<PJ::TypeTreeNode> type_tree);

  // Late discovery: register from first message (JSON, etc.)
  // Returns existing schema ID if name already registered.
  [[nodiscard]] PJ::Expected<PJ::SchemaId> register_or_get(
      std::string schema_name, std::shared_ptr<PJ::TypeTreeNode> type_tree);

  // Lookup by ID — returns nullptr if not found
  [[nodiscard]] const PJ::TypeTreeNode* lookup(PJ::SchemaId id) const;

  // Lookup by name — returns nullopt if not found
  [[nodiscard]] std::optional<PJ::SchemaId> find_by_name(std::string_view name) const;

  // Schema evolution: add fields to existing schema (additive only).
  // Fails if: ID not found, existing fields changed type, fields removed.
  [[nodiscard]] PJ::Status evolve_schema(PJ::SchemaId id, std::shared_ptr<PJ::TypeTreeNode> updated_tree);

 private:
  /// Next schema id to assign.
  PJ::SchemaId next_id_ = 1;
  /// Schema storage by id.
  absl::flat_hash_map<PJ::SchemaId, std::shared_ptr<PJ::TypeTreeNode>> schemas_;
  /// Name -> schema id index.
  absl::flat_hash_map<std::string, PJ::SchemaId> name_to_id_;
};

}  // namespace PJ::engine
