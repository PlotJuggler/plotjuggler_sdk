#pragma once
#include <cstdint>
#include <deque>
#include <string>

#include "absl/status/status.h"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::DatasetId;
using pj::SchemaId;
using pj::Timestamp;
using pj::TopicId;

struct TopicDescriptor {
  /// Topic display/name key.
  std::string name;
  /// Active schema for newly written chunks.
  SchemaId schema_id = 0;
  /// Owning dataset.
  DatasetId dataset_id = 0;
  /// Target maximum rows per chunk for writers.
  uint32_t max_chunk_rows = 1024;  // Default chunk size
};

/// Aggregated metadata snapshot for one topic.
struct TopicMetadata {
  /// Topic identifier.
  TopicId topic_id = 0;
  /// Topic display/name key.
  std::string name;
  /// Current schema id.
  SchemaId current_schema = 0;
  /// Owning dataset id.
  DatasetId dataset_id = 0;
  /// Minimum timestamp across retained chunks.
  Timestamp time_range_min = 0;
  /// Maximum timestamp across retained chunks.
  Timestamp time_range_max = 0;
  /// Total rows across retained chunks.
  uint64_t total_row_count = 0;
  /// Approximate total memory footprint across retained chunks.
  uint64_t total_byte_size = 0;  // approximate
};

/// Storage container for committed chunks of one topic.
class TopicStorage {
 public:
  /// Create storage for one topic descriptor.
  TopicStorage(TopicId topic_id, TopicDescriptor descriptor);

  /// Append a sealed chunk; rejects out-of-order chunk timestamps.
  [[nodiscard]] absl::Status append_sealed_chunk(TopicChunk chunk);

  /// Remove chunks whose max time is strictly before `t_keep_min`.
  void evict_before(Timestamp t_keep_min);

  /// Access retained sealed chunks in commit order.
  [[nodiscard]] const std::deque<TopicChunk>& sealed_chunks() const noexcept;

  /// Compute aggregated metadata for current retained chunks.
  [[nodiscard]] TopicMetadata metadata() const;

  /// Access topic descriptor.
  [[nodiscard]] const TopicDescriptor& descriptor() const noexcept;

  /// Topic identifier.
  [[nodiscard]] TopicId topic_id() const noexcept;

  /// True if no chunks are retained.
  [[nodiscard]] bool empty() const noexcept;

  /// Minimum timestamp of retained chunks (0 if empty).
  [[nodiscard]] Timestamp time_min() const noexcept;

  /// Maximum timestamp of retained chunks (0 if empty).
  [[nodiscard]] Timestamp time_max() const noexcept;

  /// Update descriptor schema id for future writes.
  void update_schema(SchemaId new_schema);

 private:
  TopicId topic_id_;
  TopicDescriptor descriptor_;
  std::deque<TopicChunk> sealed_chunks_;
};

}  // namespace pj::engine
