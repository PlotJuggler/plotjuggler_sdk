#pragma once
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "PJ/base/types.hpp"
#include "PJ/engine/chunk.hpp"

namespace PJ::engine {

struct QueryRange {
  /// Topic to query.
  PJ::TopicId topic_id = 0;
  /// Inclusive range start.
  PJ::Timestamp t_min = 0;
  /// Inclusive range end.
  PJ::Timestamp t_max = 0;
};

/// Point query descriptor for latest-at lookup.
struct QueryPoint {
  /// Topic to query.
  PJ::TopicId topic_id = 0;
  /// Query timestamp.
  PJ::Timestamp t = 0;
};

/// One materialized row reference returned by cursors.
struct SampleRow {
  /// Sample timestamp.
  PJ::Timestamp timestamp = 0;
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
  RangeCursor(const std::deque<TopicChunk>& chunks, PJ::Timestamp t_min, PJ::Timestamp t_max);

  [[nodiscard]] bool valid() const noexcept;

  /// Advance to next matching row.
  void advance();

  /// Return current row descriptor.
  [[nodiscard]] SampleRow current() const;

  // Iterate all results via callback (per-row)
  void for_each(std::function<void(const SampleRow&)> callback);

  // Iterate chunk-at-a-time (bulk path)
  void for_each_chunk(std::function<void(const ChunkRowRange&)> callback);

 private:
  const std::deque<TopicChunk>* chunks_;
  PJ::Timestamp t_min_;
  PJ::Timestamp t_max_;
  std::size_t chunk_index_ = 0;
  std::size_t row_index_ = 0;

  void find_first_valid();

  void skip_to_valid();
};

// Find the most recent sample at or before time t; nullopt if none exists.
[[nodiscard]] std::optional<SampleRow> latest_at(const std::deque<TopicChunk>& chunks, PJ::Timestamp t);

// Create a range cursor
[[nodiscard]] RangeCursor range_query(const std::deque<TopicChunk>& chunks, PJ::Timestamp t_min, PJ::Timestamp t_max);

}  // namespace PJ::engine
