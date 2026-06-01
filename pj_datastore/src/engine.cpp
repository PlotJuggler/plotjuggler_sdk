// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/engine.hpp"

#include <fmt/format.h>
#include <tsl/robin_map.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  // Topics retired by replaceDatasetFrom(): their storage is kept (so any
  // cached reader pointer dereferences an empty deque, not freed memory) but
  // they are hidden from listTopics so the catalog drops them.
  std::unordered_set<PJ::TopicId> retired_topic_ids;
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

  // Phase 2: execute. adoptChunksFrom moves sealed_chunks_ directly between
  // TopicStorage instances (no column/value copy); the chunks' stats ride along
  // by value, so dst's time_min/time_max reflect the new state immediately. The
  // topic-id rewrite is a no-op here because flushTo's mirrored dst shares the
  // source's TopicId.
  for (auto& step : plan) {
    adoptChunksFrom(*step.dst, *step.src);
  }

  return {};
}

void DataEngine::adoptChunksFrom(TopicStorage& dst, TopicStorage& src) {
  // friend access: drain src's deque into dst, re-stamping each chunk's topic_id
  // to dst's id (a no-op when the two storages already share one, e.g. flushTo).
  // Appends — callers that need replace semantics clear dst first.
  std::deque<TopicChunk> drained = std::move(src.sealed_chunks_);
  src.sealed_chunks_.clear();  // post-move state: deque is valid but empty.
  const TopicId dst_id = dst.topic_id();
  for (auto& chunk : drained) {
    chunk.topic_id = dst_id;
    dst.sealed_chunks_.push_back(std::move(chunk));
  }
}

Expected<DatasetReplaceResult> DataEngine::replaceDatasetFrom(
    DataEngine& staged, DatasetId staged_id, DatasetId primary_id) {
  if (&staged == this) {
    return PJ::unexpected("replaceDatasetFrom: staged and primary are the same engine");
  }
  auto primary_it = impl_->datasets.find(primary_id);
  if (primary_it == impl_->datasets.end()) {
    return PJ::unexpected(fmt::format("replaceDatasetFrom: primary dataset {} not found", primary_id));
  }
  if (staged.impl_->datasets.find(staged_id) == staged.impl_->datasets.end()) {
    return PJ::unexpected(fmt::format("replaceDatasetFrom: staged dataset {} not found", staged_id));
  }

  // Snapshot primary topic name -> primary TopicId (includes ids retired by a
  // previous replace, so a topic that comes back re-binds to its stable id).
  std::unordered_map<std::string, TopicId> primary_by_name;
  for (const TopicId tid : primary_it->second.topic_ids) {
    if (const auto* storage = getTopicStorage(tid)) {
      primary_by_name.emplace(storage->descriptor().name, tid);
    }
  }

  DatasetReplaceResult result;
  std::unordered_set<std::string> staged_names;

  // Adopt every staged topic into the primary dataset, by name.
  for (const TopicId staged_tid : staged.listTopics(staged_id)) {
    TopicStorage* staged_storage = staged.getTopicStorage(staged_tid);
    if (staged_storage == nullptr) {
      continue;
    }
    const std::string name = staged_storage->descriptor().name;
    staged_names.insert(name);

    const auto match = primary_by_name.find(name);
    const bool is_new = (match == primary_by_name.end());
    TopicId primary_tid = 0;
    if (is_new) {
      // Schema-less by design: post-replace reads resolve columns from the moved
      // chunks' own descriptors (and the copied inline layout below), so a new
      // topic needs no registry schema (which would not exist in this engine).
      TopicDescriptor desc = staged_storage->descriptor();
      desc.dataset_id = primary_id;
      desc.schema_id = 0;
      auto created = createTopic(primary_id, std::move(desc));
      if (!created.has_value()) {
        return PJ::unexpected("replaceDatasetFrom: createTopic failed for '" + name + "': " + created.error());
      }
      primary_tid = *created;
      result.added_topics.push_back(primary_tid);
    } else {
      primary_tid = match->second;
      impl_->retired_topic_ids.erase(primary_tid);  // un-retire if it had vanished
      result.replaced_topics.push_back(primary_tid);
    }

    // Re-fetch AFTER any createTopic above (emplace may have rehashed impl_->topics).
    TopicStorage* primary_storage = getTopicStorage(primary_tid);
    primary_storage->clearChunks();
    // Adopt the staged topic's inline layout (move, not copy — the staged engine
    // is discarded next) then re-stamp + move its chunks onto the primary id.
    primary_storage->column_descriptors_ = std::move(staged_storage->column_descriptors_);
    adoptChunksFrom(*primary_storage, *staged_storage);
  }

  // Retire primary topics the new data no longer provides.
  for (const auto& [name, primary_tid] : primary_by_name) {
    if (staged_names.count(name) == 0 && impl_->retired_topic_ids.count(primary_tid) == 0) {
      if (auto* storage = getTopicStorage(primary_tid)) {
        storage->clearChunks();
      }
      impl_->retired_topic_ids.insert(primary_tid);
      result.retired_topics.push_back(primary_tid);
    }
  }

  return result;
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
  if (impl_->retired_topic_ids.empty()) {
    return it->second.topic_ids;
  }
  std::vector<TopicId> result;
  result.reserve(it->second.topic_ids.size());
  for (const TopicId tid : it->second.topic_ids) {
    if (impl_->retired_topic_ids.count(tid) == 0) {
      result.push_back(tid);
    }
  }
  return result;
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
