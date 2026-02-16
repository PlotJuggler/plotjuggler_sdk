#pragma once
#include <optional>
#include <vector>

#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_tree.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

class DataEngine;

/// Read-only facade over committed DataEngine storage.
/// Provides listing, metadata, type-tree lookup, range queries,
/// and latest-at point queries.
class DataReader {
public:
  explicit DataReader(const DataEngine& engine);

  [[nodiscard]] std::vector<DatasetId> list_datasets() const;
  [[nodiscard]] std::vector<TopicId> list_topics(DatasetId dataset_id) const;
  [[nodiscard]] const TypeTreeNode* get_type_tree(TopicId topic_id) const;
  [[nodiscard]] std::optional<TopicMetadata> get_metadata(
      TopicId topic_id) const;

  [[nodiscard]] RangeCursor range_query(const QueryRange& range) const;
  [[nodiscard]] LatestAtResult latest_at(const QueryPoint& point) const;

private:
  const DataEngine& engine_;
};

}  // namespace pj::engine
