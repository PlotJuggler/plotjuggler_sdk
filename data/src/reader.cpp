#include "pj/engine/reader.hpp"

#include <deque>
#include <optional>
#include <vector>

#include "pj/engine/chunk.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"

namespace pj::engine {

DataReader::DataReader(const DataEngine& engine) : engine_(engine) {}

std::vector<DatasetId> DataReader::list_datasets() const {
  return engine_.list_datasets();
}

std::vector<TopicId> DataReader::list_topics(DatasetId dataset_id) const {
  return engine_.list_topics(dataset_id);
}

const TypeTreeNode* DataReader::get_type_tree(TopicId topic_id) const {
  const TopicStorage* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return nullptr;
  }
  SchemaId schema_id = storage->descriptor().schema_id;
  return engine_.type_registry().lookup(schema_id);
}

std::optional<TopicMetadata> DataReader::get_metadata(
    TopicId topic_id) const {
  const TopicStorage* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return std::nullopt;
  }
  return storage->metadata();
}

RangeCursor DataReader::range_query(const QueryRange& range) const {
  const TopicStorage* storage = engine_.get_topic_storage(range.topic_id);
  if (storage == nullptr) {
    static const std::deque<TopicChunk> kEmptyChunks;
    return pj::engine::range_query(kEmptyChunks, range.t_min, range.t_max);
  }
  return pj::engine::range_query(
      storage->sealed_chunks(), range.t_min, range.t_max);
}

LatestAtResult DataReader::latest_at(const QueryPoint& point) const {
  const TopicStorage* storage = engine_.get_topic_storage(point.topic_id);
  if (storage == nullptr) {
    return LatestAtResult{};
  }
  return pj::engine::latest_at(storage->sealed_chunks(), point.t);
}

}  // namespace pj::engine
