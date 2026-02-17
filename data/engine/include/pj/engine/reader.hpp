#pragma once
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::DatasetId;
using pj::TopicId;
using pj::TypeTreeNode;

class DataEngine;

/// Read-only facade over committed DataEngine storage.
/// Provides listing, metadata, type-tree lookup, range queries,
/// and latest-at point queries.
class DataReader {
 public:
  /// Create a read-only facade bound to `engine`.
  explicit DataReader(const DataEngine& engine);

  /// List all dataset ids known by the engine.
  [[nodiscard]] std::vector<DatasetId> list_datasets() const;

  /// List topic ids for one dataset.
  [[nodiscard]] std::vector<TopicId> list_topics(DatasetId dataset_id) const;

  /// Lookup schema tree for a topic (nullptr if unknown).
  [[nodiscard]] const TypeTreeNode* get_type_tree(TopicId topic_id) const;

  /// Return topic metadata if topic exists.
  [[nodiscard]] std::optional<TopicMetadata> get_metadata(TopicId topic_id) const;

  /// Create range cursor over [t_min, t_max].
  [[nodiscard]] absl::StatusOr<RangeCursor> range_query(const QueryRange& range) const;

  /// Return latest sample at or before query time.
  [[nodiscard]] absl::StatusOr<LatestAtResult> latest_at(const QueryPoint& point) const;

 private:
  const DataEngine& engine_;
};

}  // namespace pj::engine
