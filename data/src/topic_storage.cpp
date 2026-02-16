#include "pj/engine/topic_storage.hpp"

#include <utility>

namespace pj::engine {

TopicStorage::TopicStorage(TopicId topic_id, TopicDescriptor descriptor)
    : topic_id_(topic_id), descriptor_(std::move(descriptor)) {}

void TopicStorage::append_sealed_chunk(TopicChunk chunk) {
  sealed_chunks_.push_back(std::move(chunk));
}

void TopicStorage::evict_before(Timestamp t_keep_min) {
  while (!sealed_chunks_.empty() &&
         sealed_chunks_.front().stats.t_max < t_keep_min) {
    sealed_chunks_.pop_front();
  }
}

const std::deque<TopicChunk>& TopicStorage::sealed_chunks() const noexcept {
  return sealed_chunks_;
}

TopicMetadata TopicStorage::metadata() const {
  TopicMetadata meta;
  meta.topic_id = topic_id_;
  meta.name = descriptor_.name;
  meta.current_schema = descriptor_.schema_id;
  meta.dataset_id = descriptor_.dataset_id;

  if (sealed_chunks_.empty()) {
    return meta;
  }

  meta.time_range_min = sealed_chunks_.front().stats.t_min;
  meta.time_range_max = sealed_chunks_.back().stats.t_max;

  for (const auto& chunk : sealed_chunks_) {
    meta.total_row_count += chunk.stats.row_count;

    // Approximate byte size: sum encoded timestamp buffer + all encoded column buffers
    meta.total_byte_size += chunk.encoded_timestamps.deltas.size();
    for (const auto& col_buf : chunk.encoded_columns) {
      meta.total_byte_size += col_buf.size();
    }
    for (const auto& validity_buf : chunk.validity_bitmaps) {
      meta.total_byte_size += validity_buf.size();
    }
    for (const auto& dict : chunk.dictionary_data) {
      if (dict.has_value()) {
        meta.total_byte_size += dict->indices.size();
        for (const auto& s : dict->dictionary) {
          meta.total_byte_size += s.size();
        }
      }
    }
    for (const auto& packed : chunk.packed_bool_data) {
      if (packed.has_value()) {
        meta.total_byte_size += packed->bits.size();
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

void TopicStorage::update_schema(SchemaId new_schema) {
  descriptor_.schema_id = new_schema;
}

}  // namespace pj::engine
