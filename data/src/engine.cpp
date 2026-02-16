#include "pj/engine/engine.hpp"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace pj::engine {

DataEngine::DataEngine() = default;

// ---------------------------------------------------------------------------
// Dataset management
// ---------------------------------------------------------------------------

absl::StatusOr<DatasetId> DataEngine::create_dataset(
    DatasetDescriptor descriptor) {
  DatasetId id = next_dataset_id_++;

  // Verify time domain exists if specified
  if (descriptor.time_domain_id != 0) {
    auto it = time_domains_.find(descriptor.time_domain_id);
    if (it == time_domains_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Time domain ", descriptor.time_domain_id,
                       " not found"));
    }
  }

  DatasetInfo info;
  info.id = id;
  info.source_name = std::move(descriptor.source_name);
  if (descriptor.time_domain_id != 0) {
    info.time_domain = time_domains_.at(descriptor.time_domain_id);
  }
  datasets_.emplace(id, std::move(info));
  return id;
}

const DatasetInfo* DataEngine::get_dataset(DatasetId id) const {
  auto it = datasets_.find(id);
  if (it == datasets_.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Topic management
// ---------------------------------------------------------------------------

absl::StatusOr<TopicId> DataEngine::create_topic(
    DatasetId dataset_id, TopicDescriptor descriptor) {
  auto it = datasets_.find(dataset_id);
  if (it == datasets_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Dataset ", dataset_id, " not found"));
  }

  TopicId id = next_topic_id_++;
  descriptor.dataset_id = dataset_id;
  topics_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(id),
      std::forward_as_tuple(id, std::move(descriptor)));
  it->second.topic_ids.push_back(id);
  return id;
}

TopicStorage* DataEngine::get_topic_storage(TopicId id) {
  auto it = topics_.find(id);
  if (it == topics_.end()) {
    return nullptr;
  }
  return &it->second;
}

const TopicStorage* DataEngine::get_topic_storage(TopicId id) const {
  auto it = topics_.find(id);
  if (it == topics_.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Schema registry
// ---------------------------------------------------------------------------

TypeRegistry& DataEngine::type_registry() {
  return type_registry_;
}

const TypeRegistry& DataEngine::type_registry() const {
  return type_registry_;
}

// ---------------------------------------------------------------------------
// Time domains
// ---------------------------------------------------------------------------

absl::StatusOr<TimeDomainId> DataEngine::create_time_domain(std::string name) {
  TimeDomainId id = next_time_domain_id_++;
  TimeDomain td;
  td.id = id;
  td.name = std::move(name);
  time_domains_.emplace(id, std::move(td));
  return id;
}

const TimeDomain* DataEngine::get_time_domain(TimeDomainId id) const {
  auto it = time_domains_.find(id);
  if (it == time_domains_.end()) {
    return nullptr;
  }
  return &it->second;
}

void DataEngine::set_display_offset(TimeDomainId id, Timestamp offset) {
  auto it = time_domains_.find(id);
  if (it != time_domains_.end()) {
    it->second.display_offset = offset;
  }
}

// ---------------------------------------------------------------------------
// Commit cycle
// ---------------------------------------------------------------------------

void DataEngine::commit_chunks(
    std::vector<std::pair<TopicId, TopicChunk>> chunks) {  // NOLINT(performance-unnecessary-value-param)
  for (auto& [topic_id, chunk] : chunks) {
    auto* storage = get_topic_storage(topic_id);
    if (storage != nullptr) {
      storage->append_sealed_chunk(std::move(chunk));
    }
  }
}

void DataEngine::enforce_retention(Timestamp retention_window_ns) {
  for (auto& [topic_id, storage] : topics_) {
    Timestamp t_max = storage.time_max();
    if (t_max > 0) {
      storage.evict_before(t_max - retention_window_ns);
    }
  }
}

// ---------------------------------------------------------------------------
// Listing helpers
// ---------------------------------------------------------------------------

std::vector<DatasetId> DataEngine::list_datasets() const {
  std::vector<DatasetId> result;
  result.reserve(datasets_.size());
  for (const auto& [id, info] : datasets_) {
    result.push_back(id);
  }
  return result;
}

std::vector<TopicId> DataEngine::list_topics(DatasetId dataset_id) const {
  auto it = datasets_.find(dataset_id);
  if (it == datasets_.end()) {
    return {};
  }
  return it->second.topic_ids;
}

}  // namespace pj::engine
