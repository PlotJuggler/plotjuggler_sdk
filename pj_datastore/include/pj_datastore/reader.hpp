#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <cstddef>
#include <optional>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/topic_storage.hpp"

namespace PJ {

class DataEngine;

/// Read-only facade over committed DataEngine storage.
/// Provides listing, metadata, type-tree lookup, range queries,
/// and latest-at point queries.
class DataReader {
 public:
  /// Create a read-only facade bound to `engine`.
  explicit DataReader(const DataEngine& engine);

  /// List all dataset ids known by the engine.
  [[nodiscard]] std::vector<PJ::DatasetId> listDatasets() const;

  /// List topic ids for one dataset.
  [[nodiscard]] std::vector<PJ::TopicId> listTopics(PJ::DatasetId dataset_id) const;

  /// Lookup schema tree for a topic (nullptr if unknown).
  [[nodiscard]] const PJ::TypeTreeNode* getTypeTree(PJ::TopicId topic_id) const;

  /// Return topic metadata if topic exists.
  [[nodiscard]] std::optional<TopicMetadata> getMetadata(PJ::TopicId topic_id) const;

  /// Create range cursor over [t_min, t_max].
  [[nodiscard]] PJ::Expected<RangeCursor> rangeQuery(const QueryRange& range) const;

  /// Return latest sample at or before query time; nullopt payload if no row exists.
  [[nodiscard]] PJ::Expected<std::optional<SampleRow>> latestAt(const QueryPoint& point) const;

  /// Create a series view over one numeric/bool topic column. The returned
  /// reader exposes only value-bearing samples; null rows are skipped.
  [[nodiscard]] PJ::Expected<SeriesReader> series(PJ::TopicId topic_id, std::size_t column_index) const;

 private:
  const DataEngine& engine_;
};

}  // namespace PJ
