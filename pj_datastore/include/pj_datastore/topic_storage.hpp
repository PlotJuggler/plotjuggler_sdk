#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

#include "pj_base/expected.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/chunk.hpp"

namespace PJ {

// Import base types into engine namespace
using PJ::DatasetId;
using PJ::SchemaId;
using PJ::Timestamp;
using PJ::TopicId;

struct TopicDescriptor {
  /// Topic display/name key.
  std::string name;
  /// Active schema for newly written chunks.
  SchemaId schema_id = 0;
  /// Owning dataset.
  DatasetId dataset_id = 0;
  /// Target maximum rows per chunk for writers.
  uint32_t max_chunk_rows = 1024;  // Default chunk size
  /// Maximum number of element columns to expand per variable-length array field.
  /// Prevents column explosion. expandArray() clamps to this limit.
  uint32_t array_expansion_limit = 64;
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
  /// Largest array length ever passed to expandArray() for any field in this topic.
  uint32_t max_observed_array_length = 0;
  /// Number of times expandArray() clamped due to array_expansion_limit.
  uint32_t truncated_sample_count = 0;
};

class DataEngine;

/// Per-topic container of committed chunks (commit-ordered deque).
/// appendSealedChunk() enforces chunk t_min >= previous t_max; evictBefore()
/// drops chunks fully older than a threshold. Also holds the column layout for
/// schemaless (schema_id==0) topics and per-field array-expansion counts.
class TopicStorage {
 public:
  /// Create storage for one topic descriptor.
  TopicStorage(TopicId topic_id, TopicDescriptor descriptor);

  /// Append a sealed chunk; rejects out-of-order chunk timestamps.
  [[nodiscard]] PJ::Status appendSealedChunk(TopicChunk chunk);

  /// Remove chunks whose max time is strictly before `t_keep_min`.
  void evictBefore(Timestamp t_keep_min);

  /// Unconditionally remove all retained sealed chunks.
  void clearChunks() noexcept;

  /// Access retained sealed chunks in commit order.
  [[nodiscard]] const std::deque<TopicChunk>& sealedChunks() const noexcept;

  /// Store column layout for schema_id==0 topics (populated at writer registration time).
  /// Allows derived engine and fresh writers to resolve the layout without a committed chunk.
  void setColumnDescriptors(std::vector<ColumnDescriptor> descs) noexcept;

  /// Inline column layout (non-empty for schema_id==0 topics after the first writer is created).
  [[nodiscard]] const std::vector<ColumnDescriptor>& columnDescriptors() const noexcept;

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
  void updateSchema(SchemaId new_schema);

  /// Track the largest observed array length (called by DataWriter::expand_array).
  void updateMaxObservedArrayLength(uint32_t observed_length);

  /// Increment the truncation counter (called when expand_array clamps due to limit).
  void incrementTruncatedSampleCount();

  /// Largest array length ever passed to expandArray() for any field in this topic.
  [[nodiscard]] uint32_t maxObservedArrayLength() const noexcept;

  /// Number of times expandArray() clamped due to array_expansion_limit.
  [[nodiscard]] uint32_t truncatedSampleCount() const noexcept;

  /// Return the current expansion count for a variable-length array field.
  /// Returns 0 if the field has not been expanded yet.
  [[nodiscard]] uint32_t arrayExpansionCount(const std::string& field_path) const noexcept;

  /// Update the expansion count for a variable-length array field.
  void setArrayExpansionCount(const std::string& field_path, uint32_t count);

 private:
  // DataEngine::flushTo needs to move sealed_chunks_ between TopicStorage
  // instances of different engines without copying. Friending it lets the
  // transfer happen entirely inside DataEngine without exposing the move
  // primitive on the public TopicStorage API.
  friend class DataEngine;

  TopicId topic_id_;
  TopicDescriptor descriptor_;
  std::deque<TopicChunk> sealed_chunks_;
  std::vector<ColumnDescriptor> column_descriptors_;  // for schema_id==0 topics
  uint32_t max_observed_array_length_ = 0;
  uint32_t truncated_sample_count_ = 0;
  // Authoritative expansion count per variable-length array field path.
  // Shared across all DataWriter instances writing to this topic.
  std::unordered_map<std::string, uint32_t> array_expansion_counts_;
};

}  // namespace PJ
