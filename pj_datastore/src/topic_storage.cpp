// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/topic_storage.hpp"

#include <fmt/format.h>

#include <utility>
#include <variant>

#include "pj_base/expected.hpp"

namespace PJ {

TopicStorage::TopicStorage(TopicId topic_id, TopicDescriptor descriptor)
    : topic_id_(topic_id), descriptor_(std::move(descriptor)) {}

PJ::Status TopicStorage::appendSealedChunk(TopicChunk chunk) {
  if (!sealed_chunks_.empty() && chunk.stats.t_min < sealed_chunks_.back().stats.t_max) {
    // Reject any chunk whose t_min overlaps with the previous chunk's time range.
    // Using t_max (not t_min) as the boundary: a new chunk starting exactly at
    // the previous t_max is allowed (equal-boundary chunks from normal chunking).
    return PJ::unexpected(
        fmt::format(
            "Overlapping chunk: new t_min={} < last t_max={}", chunk.stats.t_min, sealed_chunks_.back().stats.t_max));
  }
  sealed_chunks_.push_back(std::move(chunk));
  return PJ::okStatus();
}

void TopicStorage::evictBefore(Timestamp t_keep_min) {
  while (!sealed_chunks_.empty() && sealed_chunks_.front().stats.t_max < t_keep_min) {
    sealed_chunks_.pop_front();
  }
}

void TopicStorage::clearChunks() noexcept {
  sealed_chunks_.clear();
}

void TopicStorage::setColumnDescriptors(std::vector<ColumnDescriptor> descs) noexcept {
  column_descriptors_ = std::move(descs);
}

const std::vector<ColumnDescriptor>& TopicStorage::columnDescriptors() const noexcept {
  return column_descriptors_;
}

const std::deque<TopicChunk>& TopicStorage::sealedChunks() const noexcept {
  return sealed_chunks_;
}

TopicMetadata TopicStorage::metadata() const {
  TopicMetadata meta;
  meta.topic_id = topic_id_;
  meta.name = descriptor_.name;
  meta.current_schema = descriptor_.schema_id;
  meta.dataset_id = descriptor_.dataset_id;

  meta.max_observed_array_length = max_observed_array_length_;
  meta.truncated_sample_count = truncated_sample_count_;

  if (sealed_chunks_.empty()) {
    return meta;
  }

  meta.time_range_min = sealed_chunks_.front().stats.t_min;
  meta.time_range_max = sealed_chunks_.back().stats.t_max;

  for (const auto& chunk : sealed_chunks_) {
    meta.total_row_count += chunk.stats.row_count;

    // Approximate byte size: sum encoded timestamp buffer + all encoded column buffers
    meta.total_byte_size += chunk.timestamps.size() * sizeof(Timestamp);
    for (const auto& col : chunk.columns) {
      std::visit(
          [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, RawBuffer>) {
              meta.total_byte_size += v.size();
            } else if constexpr (std::is_same_v<T, encoding::DictionaryEncoded>) {
              meta.total_byte_size += v.indices.size();
              for (const auto& s : v.dictionary) {
                meta.total_byte_size += s.size();
              }
            } else if constexpr (std::is_same_v<T, encoding::PackedBools>) {
              meta.total_byte_size += v.bits.size();
            } else if constexpr (std::is_same_v<T, encoding::ConstantEncoded>) {
              meta.total_byte_size += v.value_size;
            } else if constexpr (std::is_same_v<T, encoding::FrameOfReferenceEncoded>) {
              meta.total_byte_size += v.offsets.size();
            }
          },
          col.data);
      if (col.validity_bitmap) {
        meta.total_byte_size += col.validity_bitmap->sizeBytes();
      }
    }
  }

  return meta;
}

const TopicDescriptor& TopicStorage::descriptor() const noexcept {
  return descriptor_;
}

TopicId TopicStorage::topic_id() const noexcept {
  return topic_id_;
}

bool TopicStorage::empty() const noexcept {
  return sealed_chunks_.empty();
}

Timestamp TopicStorage::time_min() const noexcept {
  if (sealed_chunks_.empty()) {
    return 0;
  }
  return sealed_chunks_.front().stats.t_min;
}

Timestamp TopicStorage::time_max() const noexcept {
  if (sealed_chunks_.empty()) {
    return 0;
  }
  return sealed_chunks_.back().stats.t_max;
}

void TopicStorage::updateSchema(SchemaId new_schema) {
  descriptor_.schema_id = new_schema;
}

void TopicStorage::updateMaxObservedArrayLength(uint32_t observed_length) {
  if (observed_length > max_observed_array_length_) {
    max_observed_array_length_ = observed_length;
  }
}

void TopicStorage::incrementTruncatedSampleCount() {
  ++truncated_sample_count_;
}

uint32_t TopicStorage::maxObservedArrayLength() const noexcept {
  return max_observed_array_length_;
}

uint32_t TopicStorage::truncatedSampleCount() const noexcept {
  return truncated_sample_count_;
}

uint32_t TopicStorage::arrayExpansionCount(const std::string& field_path) const noexcept {
  auto it = array_expansion_counts_.find(field_path);
  return it != array_expansion_counts_.end() ? it->second : 0;
}

void TopicStorage::setArrayExpansionCount(const std::string& field_path, uint32_t count) {
  array_expansion_counts_[field_path] = count;
}

}  // namespace PJ
