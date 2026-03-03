#pragma once
#include <optional>
#include <vector>

#include "PJ/base/expected.hpp"
#include "PJ/base/type_tree.hpp"
#include "PJ/base/types.hpp"
#include "PJ/engine/query.hpp"
#include "PJ/engine/topic_storage.hpp"

namespace PJ::engine {

class DataEngine;

/// Read-only facade over committed DataEngine storage.
/// Provides listing, metadata, type-tree lookup, range queries,
/// and latest-at point queries.
class DataReader {
 public:
  /// Create a read-only facade bound to `engine`.
  explicit DataReader(const DataEngine& engine);

  /// List all dataset ids known by the engine.
  [[nodiscard]] std::vector<PJ::DatasetId> list_datasets() const;

  /// List topic ids for one dataset.
  [[nodiscard]] std::vector<PJ::TopicId> list_topics(PJ::DatasetId dataset_id) const;

  /// Lookup schema tree for a topic (nullptr if unknown).
  [[nodiscard]] const PJ::TypeTreeNode* get_type_tree(PJ::TopicId topic_id) const;

  /// Return topic metadata if topic exists.
  [[nodiscard]] std::optional<TopicMetadata> get_metadata(PJ::TopicId topic_id) const;

  /// Create range cursor over [t_min, t_max].
  [[nodiscard]] PJ::Expected<RangeCursor> range_query(const QueryRange& range) const;

  /// Return latest sample at or before query time; nullopt payload if no row exists.
  [[nodiscard]] PJ::Expected<std::optional<SampleRow>> latest_at(const QueryPoint& point) const;

 private:
  const DataEngine& engine_;
};

}  // namespace PJ::engine
