#pragma once
#include <cstddef>
#include <deque>
#include <vector>

#include "absl/functional/function_ref.h"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::Timestamp;
using pj::TopicId;

struct QueryRange {
  /// Topic to query.
  TopicId topic_id = 0;
  /// Inclusive range start.
  Timestamp t_min = 0;
  /// Inclusive range end.
  Timestamp t_max = 0;
};

/// Point query descriptor for latest-at lookup.
struct QueryPoint {
  /// Topic to query.
  TopicId topic_id = 0;
  /// Query timestamp.
  Timestamp t = 0;
};

/// One materialized row reference returned by cursors.
struct SampleRow {
  /// Sample timestamp.
  Timestamp timestamp = 0;
  /// Pointer to source chunk containing this row.
  const TopicChunk* chunk = nullptr;
  /// Row index inside `chunk`.
  std::size_t row_index = 0;
};

/// Contiguous row interval inside one chunk.
struct ChunkRowRange {
  /// Source chunk.
  const TopicChunk* chunk = nullptr;
  /// Inclusive start row.
  std::size_t row_start = 0;
  /// Exclusive end row.
  std::size_t row_end = 0;  // exclusive
};

// Cursor for iterating range query results across chunks
class RangeCursor {
 public:
  /// Construct cursor over [t_min, t_max] from committed chunks.
  RangeCursor(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max);

  [[nodiscard]] bool valid() const noexcept;

  /// Advance to next matching row.
  void advance();

  /// Return current row descriptor.
  [[nodiscard]] SampleRow current() const;

  // Iterate all results via callback (per-row)
  void for_each(absl::FunctionRef<void(const SampleRow&)> callback);

  // Iterate chunk-at-a-time (bulk path)
  void for_each_chunk(absl::FunctionRef<void(const ChunkRowRange&)> callback);

 private:
  const std::deque<TopicChunk>* chunks_;
  Timestamp t_min_;
  Timestamp t_max_;
  std::size_t chunk_index_ = 0;
  std::size_t row_index_ = 0;

  void find_first_valid();

  void skip_to_valid();
};

struct LatestAtResult {
  /// True when at least one sample <= query time was found.
  bool found = false;
  /// Timestamp of returned sample.
  Timestamp timestamp = 0;
  /// Source chunk pointer.
  const TopicChunk* chunk = nullptr;
  /// Row index within `chunk`.
  std::size_t row_index = 0;
};

// Find the most recent sample at or before time t
[[nodiscard]] LatestAtResult latest_at(const std::deque<TopicChunk>& chunks, Timestamp t);

// Create a range cursor
[[nodiscard]] RangeCursor range_query(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max);

}  // namespace pj::engine
