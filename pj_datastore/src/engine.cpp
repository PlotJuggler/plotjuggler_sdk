// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/engine.hpp"

#include <fmt/format.h>
#include <tsl/robin_map.h>

#include <algorithm>
#include <utility>

#include "pj_base/expected.hpp"
#include "pj_datastore/reader.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {

struct DataEngine::Impl {
  TypeRegistry type_registry;
  PJ::DatasetId next_dataset_id = 1;
  PJ::TopicId next_topic_id = 1;
  PJ::TimeDomainId next_time_domain_id = 1;
  tsl::robin_map<PJ::DatasetId, PJ::DatasetInfo> datasets;
  tsl::robin_map<PJ::TopicId, TopicStorage> topics;
  tsl::robin_map<PJ::TimeDomainId, PJ::TimeDomain> time_domains;
};

DataEngine::DataEngine() : impl_(std::make_unique<Impl>()) {}

DataEngine::~DataEngine() = default;

DataEngine::DataEngine(DataEngine&&) noexcept = default;

DataEngine& DataEngine::operator=(DataEngine&&) noexcept = default;

// ---------------------------------------------------------------------------
// Dataset management
// ---------------------------------------------------------------------------

Expected<DatasetId> DataEngine::createDataset(DatasetDescriptor descriptor) {
  DatasetId id = impl_->next_dataset_id++;

  // Verify time domain exists if specified
  if (descriptor.time_domain_id != 0) {
    auto it = impl_->time_domains.find(descriptor.time_domain_id);
    if (it == impl_->time_domains.end()) {
      return PJ::unexpected(fmt::format("Time domain {} not found", descriptor.time_domain_id));
    }
  }

  DatasetInfo info;
  info.id = id;
  info.source_name = std::move(descriptor.source_name);
  if (descriptor.time_domain_id != 0) {
    info.time_domain = impl_->time_domains.at(descriptor.time_domain_id);
  }
  impl_->datasets.emplace(id, std::move(info));
  return id;
}

const DatasetInfo* DataEngine::getDataset(DatasetId id) const {
  auto it = impl_->datasets.find(id);
  if (it == impl_->datasets.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Topic management
// ---------------------------------------------------------------------------

Expected<TopicId> DataEngine::createTopic(DatasetId dataset_id, TopicDescriptor descriptor) {
  auto it = impl_->datasets.find(dataset_id);
  if (it == impl_->datasets.end()) {
    return PJ::unexpected(fmt::format("Dataset {} not found", dataset_id));
  }

  // Validate schema_id if non-zero (zero means inline columns, e.g. scalar series)
  if (descriptor.schema_id != 0) {
    if (impl_->type_registry.lookup(descriptor.schema_id) == nullptr) {
      return PJ::unexpected(fmt::format("Schema {} not found", descriptor.schema_id));
    }
  }

  TopicId id = impl_->next_topic_id++;
  descriptor.dataset_id = dataset_id;
  impl_->topics.emplace(
      std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id, std::move(descriptor)));
  it.value().topic_ids.push_back(id);
  return id;
}

TopicStorage* DataEngine::getTopicStorage(TopicId id) {
  auto it = impl_->topics.find(id);
  if (it == impl_->topics.end()) {
    return nullptr;
  }
  return &it.value();
}

const TopicStorage* DataEngine::getTopicStorage(TopicId id) const {
  auto it = impl_->topics.find(id);
  if (it == impl_->topics.end()) {
    return nullptr;
  }
  return &it->second;
}

// ---------------------------------------------------------------------------
// Schema registry
// ---------------------------------------------------------------------------

TypeRegistry& DataEngine::typeRegistry() {
  return impl_->type_registry;
}

const TypeRegistry& DataEngine::typeRegistry() const {
  return impl_->type_registry;
}

// ---------------------------------------------------------------------------
// Time domains
// ---------------------------------------------------------------------------

Expected<TimeDomainId> DataEngine::createTimeDomain(std::string name) {
  TimeDomainId id = impl_->next_time_domain_id++;
  TimeDomain td;
  td.id = id;
  td.name = std::move(name);
  impl_->time_domains.emplace(id, std::move(td));
  return id;
}

const TimeDomain* DataEngine::getTimeDomain(TimeDomainId id) const {
  auto it = impl_->time_domains.find(id);
  if (it == impl_->time_domains.end()) {
    return nullptr;
  }
  return &it->second;
}

void DataEngine::setDisplayOffset(TimeDomainId id, Timestamp offset) {
  auto it = impl_->time_domains.find(id);
  if (it != impl_->time_domains.end()) {
    it.value().display_offset = offset;
  }
}

// ---------------------------------------------------------------------------
// Commit cycle
// ---------------------------------------------------------------------------

std::vector<TopicId> DataEngine::commitChunks(
    std::vector<std::pair<TopicId, TopicChunk>> chunks) {  // NOLINT(performance-unnecessary-value-param)
  std::vector<TopicId> changed;
  for (auto& [topic_id, chunk] : chunks) {
    auto* storage = getTopicStorage(topic_id);
    if (storage != nullptr) {
      auto status = storage->appendSealedChunk(std::move(chunk));
      if (!status.has_value()) {
        continue;  // chunk rejected (e.g. out-of-order); do not mark topic as changed
      }
      if (changed.empty() || changed.back() != topic_id) {
        changed.push_back(topic_id);
      }
    }
  }
  // Deduplicate (flushAll() may emit multiple chunks for one topic).
  std::sort(changed.begin(), changed.end());
  changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
  return changed;
}

void DataEngine::enforceRetention(Timestamp retention_window_ns) {
  for (auto it = impl_->topics.begin(); it != impl_->topics.end(); ++it) {
    auto& storage = it.value();
    if (!storage.empty()) {
      Timestamp t_max = storage.time_max();
      storage.evictBefore(t_max - retention_window_ns);
    }
  }
}

Status DataEngine::flushTo(DataEngine& dst) {
  if (&dst == this) {
    return PJ::unexpected("flushTo: source and destination are the same engine");
  }

  // Phase 1: validate. Walk every src topic with sealed chunks and look up
  // the matching dst topic by descriptor (dataset_id + name). Verify
  // monotonicity against dst's current time_max. No mutation yet.
  struct Step {
    TopicStorage* src;
    TopicStorage* dst;
  };
  std::vector<Step> plan;
  plan.reserve(impl_->topics.size());

  for (auto it = impl_->topics.begin(); it != impl_->topics.end(); ++it) {
    auto& src_storage = it.value();
    if (src_storage.empty()) {
      continue;
    }
    TopicStorage* dst_storage = nullptr;
    for (auto dst_it = dst.impl_->topics.begin(); dst_it != dst.impl_->topics.end(); ++dst_it) {
      auto& candidate = dst_it.value();
      if (candidate.descriptor().dataset_id == src_storage.descriptor().dataset_id &&
          candidate.descriptor().name == src_storage.descriptor().name) {
        dst_storage = &candidate;
        break;
      }
    }
    if (dst_storage == nullptr) {
      return PJ::unexpected(
          "flushTo: destination has no topic '" + src_storage.descriptor().name + "' for dataset " +
          std::to_string(src_storage.descriptor().dataset_id));
    }
    if (!dst_storage->empty() && src_storage.time_min() < dst_storage->time_max()) {
      return PJ::unexpected("flushTo: monotonicity violation for topic '" + src_storage.descriptor().name + "'");
    }
    plan.push_back({&src_storage, dst_storage});
  }

  // Phase 2: execute. friend access lets us move sealed_chunks_ directly
  // between TopicStorage instances of different engines — the deque move
  // transfers chunk ownership without copying any column data or value
  // buffers. Each chunk's TopicChunkStats (t_min/t_max/row_count) rides
  // along inside the chunk by value, so dst's time_min/time_max queries
  // reflect the new state immediately after the move.
  for (auto& step : plan) {
    auto drained = std::move(step.src->sealed_chunks_);
    step.src->sealed_chunks_.clear();  // post-move state: deque is valid but empty.
    for (auto& chunk : drained) {
      step.dst->sealed_chunks_.push_back(std::move(chunk));
    }
  }

  return {};
}

// ---------------------------------------------------------------------------
// Listing helpers
// ---------------------------------------------------------------------------

std::vector<DatasetId> DataEngine::listDatasets() const {
  std::vector<DatasetId> result;
  result.reserve(impl_->datasets.size());
  for (const auto& [id, info] : impl_->datasets) {
    result.push_back(id);
  }
  return result;
}

std::vector<TopicId> DataEngine::listTopics(DatasetId dataset_id) const {
  auto it = impl_->datasets.find(dataset_id);
  if (it == impl_->datasets.end()) {
    return {};
  }
  return it->second.topic_ids;
}

// ---------------------------------------------------------------------------
// Writer/Reader factories
// ---------------------------------------------------------------------------

DataWriter DataEngine::createWriter() {
  return DataWriter(*this);
}

DataReader DataEngine::createReader() const {
  return DataReader(*this);
}

}  // namespace PJ
