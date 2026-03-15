#include "pj_datastore/reader.hpp"

#include <fmt/format.h>

#include <deque>
#include <optional>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/type_registry.hpp"

namespace PJ {

DataReader::DataReader(const DataEngine& engine) : engine_(engine) {}

std::vector<DatasetId> DataReader::listDatasets() const {
  return engine_.listDatasets();
}

std::vector<TopicId> DataReader::listTopics(DatasetId dataset_id) const {
  return engine_.listTopics(dataset_id);
}

const TypeTreeNode* DataReader::getTypeTree(TopicId topic_id) const {
  const TopicStorage* storage = engine_.getTopicStorage(topic_id);
  if (storage == nullptr) {
    return nullptr;
  }
  SchemaId schema_id = storage->descriptor().schema_id;
  return engine_.typeRegistry().lookup(schema_id);
}

std::optional<TopicMetadata> DataReader::getMetadata(TopicId topic_id) const {
  const TopicStorage* storage = engine_.getTopicStorage(topic_id);
  if (storage == nullptr) {
    return std::nullopt;
  }
  return storage->metadata();
}

Expected<RangeCursor> DataReader::rangeQuery(const QueryRange& range) const {
  const TopicStorage* storage = engine_.getTopicStorage(range.topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(fmt::format("Topic {} not found", range.topic_id));
  }
  return PJ::rangeQuery(storage->sealedChunks(), range.t_min, range.t_max);
}

PJ::Expected<std::optional<SampleRow>> DataReader::latestAt(const QueryPoint& point) const {
  const TopicStorage* storage = engine_.getTopicStorage(point.topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(fmt::format("Topic {} not found", point.topic_id));
  }
  return PJ::latestAt(storage->sealedChunks(), point.t);
}

}  // namespace PJ
