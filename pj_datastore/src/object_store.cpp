// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/object_store.hpp"

#include <algorithm>

namespace PJ {

// --- Registration ---

Expected<ObjectTopicId> ObjectStore::registerTopic(const ObjectTopicDescriptor& descriptor) {
  std::unique_lock lock(store_mutex_);
  for (const auto& [tid, series] : topics_) {
    if (series->descriptor.topic_name == descriptor.topic_name &&
        series->descriptor.dataset_id == descriptor.dataset_id) {
      return unexpected("topic already registered: " + descriptor.topic_name);
    }
  }
  ObjectTopicId id{next_id_++};
  auto series = std::make_unique<ObjectSeries>();
  series->descriptor = descriptor;
  topics_.emplace_back(id, std::move(series));
  return id;
}

std::optional<ObjectTopicId> ObjectStore::findTopic(DatasetId dataset_id, std::string_view topic_name) const {
  std::shared_lock lock(store_mutex_);
  for (const auto& [tid, series] : topics_) {
    if (series->descriptor.dataset_id == dataset_id && series->descriptor.topic_name == topic_name) {
      return tid;
    }
  }
  return std::nullopt;
}

const ObjectTopicDescriptor& ObjectStore::descriptor(ObjectTopicId id) const {
  std::shared_lock lock(store_mutex_);
  const auto* s = findSeries(id);
  if (s == nullptr) {
    static const ObjectTopicDescriptor kEmpty{};
    return kEmpty;
  }
  return s->descriptor;
}

std::vector<ObjectTopicId> ObjectStore::listTopics() const {
  std::shared_lock lock(store_mutex_);
  std::vector<ObjectTopicId> result;
  result.reserve(topics_.size());
  for (const auto& [tid, _] : topics_) {
    result.push_back(tid);
  }
  return result;
}

std::vector<ObjectTopicId> ObjectStore::listTopics(DatasetId dataset_id) const {
  std::shared_lock lock(store_mutex_);
  std::vector<ObjectTopicId> result;
  for (const auto& [tid, series] : topics_) {
    if (series->descriptor.dataset_id == dataset_id) {
      result.push_back(tid);
    }
  }
  return result;
}

// --- Write ---

Status ObjectStore::pushOwned(ObjectTopicId id, Timestamp timestamp, std::vector<uint8_t> payload) {
  std::shared_lock store_lock(store_mutex_);
  auto* series = findSeries(id);
  if (series == nullptr) {
    return unexpected("unknown topic");
  }

  std::unique_lock lock(series->mutex);
  if (!series->entry_timestamps.empty() && timestamp < series->entry_timestamps.back()) {
    return unexpected("timestamp not monotonically non-decreasing");
  }

  size_t payload_size = payload.size();
  auto shared_data = std::make_shared<const std::vector<uint8_t>>(std::move(payload));

  ObjectEntry entry;
  entry.timestamp = timestamp;
  entry.payload = std::move(shared_data);
  series->entries.push_back(std::move(entry));
  series->entry_timestamps.push_back(timestamp);
  series->memory_bytes += payload_size;

  applyRetention(*series, timestamp);
  return {};
}

Status ObjectStore::pushLazy(ObjectTopicId id, Timestamp timestamp, LazyCallback fetch) {
  std::shared_lock store_lock(store_mutex_);
  auto* series = findSeries(id);
  if (series == nullptr) {
    return unexpected("unknown topic");
  }

  std::unique_lock lock(series->mutex);
  if (!series->entry_timestamps.empty() && timestamp < series->entry_timestamps.back()) {
    return unexpected("timestamp not monotonically non-decreasing");
  }

  ObjectEntry entry;
  entry.timestamp = timestamp;
  entry.payload = std::move(fetch);
  series->entries.push_back(std::move(entry));
  series->entry_timestamps.push_back(timestamp);

  applyRetention(*series, timestamp);
  return {};
}

// --- Read ---

std::optional<ResolvedObjectEntry> ObjectStore::latestAt(ObjectTopicId id, Timestamp timestamp) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return std::nullopt;
  }

  std::shared_lock lock(series->mutex);
  if (series->entry_timestamps.empty()) {
    return std::nullopt;
  }

  auto it = std::upper_bound(series->entry_timestamps.begin(), series->entry_timestamps.end(), timestamp);
  if (it == series->entry_timestamps.begin()) {
    return std::nullopt;
  }
  --it;
  auto idx = static_cast<size_t>(it - series->entry_timestamps.begin());
  return resolveEntry(series->entries[idx]);
}

std::optional<ResolvedObjectEntry> ObjectStore::at(ObjectTopicId id, size_t index) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return std::nullopt;
  }

  std::shared_lock lock(series->mutex);
  if (index >= series->entries.size()) {
    return std::nullopt;
  }
  return resolveEntry(series->entries[index]);
}

std::optional<size_t> ObjectStore::indexAt(ObjectTopicId id, Timestamp timestamp) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return std::nullopt;
  }

  std::shared_lock lock(series->mutex);
  if (series->entry_timestamps.empty()) {
    return std::nullopt;
  }

  auto it = std::upper_bound(series->entry_timestamps.begin(), series->entry_timestamps.end(), timestamp);
  if (it == series->entry_timestamps.begin()) {
    return std::nullopt;
  }
  --it;
  return static_cast<size_t>(it - series->entry_timestamps.begin());
}

size_t ObjectStore::entryCount(ObjectTopicId id) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return 0;
  }

  std::shared_lock lock(series->mutex);
  return series->entries.size();
}

std::pair<Timestamp, Timestamp> ObjectStore::timeRange(ObjectTopicId id) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return {0, 0};
  }

  std::shared_lock lock(series->mutex);
  if (series->entry_timestamps.empty()) {
    return {0, 0};
  }
  return {series->entry_timestamps.front(), series->entry_timestamps.back()};
}

EntryTimestampsView ObjectStore::entryTimestamps(ObjectTopicId id) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return {};
  }

  std::shared_lock lock(series->mutex);
  return {std::move(lock), &series->entry_timestamps};
}

// --- Retention ---

void ObjectStore::setRetentionBudget(ObjectTopicId id, RetentionBudget budget) {
  std::shared_lock store_lock(store_mutex_);
  auto* series = findSeries(id);
  if (series == nullptr) {
    return;
  }
  std::unique_lock lock(series->mutex);
  series->budget = budget;
}

RetentionBudget ObjectStore::retentionBudget(ObjectTopicId id) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return {};
  }
  std::shared_lock lock(series->mutex);
  return series->budget;
}

size_t ObjectStore::memoryUsage(ObjectTopicId id) const {
  std::shared_lock store_lock(store_mutex_);
  const auto* series = findSeries(id);
  if (series == nullptr) {
    return 0;
  }
  std::shared_lock lock(series->mutex);
  return series->memory_bytes;
}

// --- Explicit eviction ---

void ObjectStore::evictBefore(ObjectTopicId id, Timestamp threshold) {
  std::shared_lock store_lock(store_mutex_);
  auto* series = findSeries(id);
  if (series == nullptr) {
    return;
  }
  std::unique_lock lock(series->mutex);
  while (!series->entries.empty() && series->entry_timestamps.front() < threshold) {
    evictFront(*series);
  }
}

void ObjectStore::evictAllBefore(Timestamp threshold) {
  std::shared_lock store_lock(store_mutex_);
  for (auto& [tid, series] : topics_) {
    std::unique_lock lock(series->mutex);
    while (!series->entries.empty() && series->entry_timestamps.front() < threshold) {
      evictFront(*series);
    }
  }
}

// --- Lifecycle ---

void ObjectStore::removeTopic(ObjectTopicId id) {
  std::unique_lock lock(store_mutex_);
  auto it = std::find_if(topics_.begin(), topics_.end(), [&](const auto& pair) { return pair.first == id; });
  if (it != topics_.end()) {
    topics_.erase(it);
  }
}

void ObjectStore::clear() {
  std::unique_lock lock(store_mutex_);
  topics_.clear();
  next_id_ = 1;
}

// --- Private helpers ---

ObjectStore::ObjectSeries* ObjectStore::findSeries(ObjectTopicId id) {
  for (auto& [tid, series] : topics_) {
    if (tid == id) {
      return series.get();
    }
  }
  return nullptr;
}

const ObjectStore::ObjectSeries* ObjectStore::findSeries(ObjectTopicId id) const {
  for (const auto& [tid, series] : topics_) {
    if (tid == id) {
      return series.get();
    }
  }
  return nullptr;
}

ResolvedObjectEntry ObjectStore::resolveEntry(const ObjectEntry& entry) {
  ResolvedObjectEntry resolved;
  resolved.timestamp = entry.timestamp;

  if (const auto* owned = std::get_if<SharedBuffer>(&entry.payload)) {
    if (*owned) {
      resolved.payload = sdk::PayloadView{
          Span<const uint8_t>{(*owned)->data(), (*owned)->size()},
          sdk::BufferAnchor{*owned},
      };
    }
  } else if (const auto* lazy = std::get_if<LazyCallback>(&entry.payload)) {
    resolved.payload = (*lazy)();
  }

  return resolved;
}

void ObjectStore::evictFront(ObjectSeries& series) {
  if (series.entries.empty()) {
    return;
  }

  const auto& front = series.entries.front();
  if (const auto* owned = std::get_if<SharedBuffer>(&front.payload); owned != nullptr && *owned) {
    series.memory_bytes -= (*owned)->size();
  }

  series.entries.pop_front();
  series.entry_timestamps.erase(series.entry_timestamps.begin());
}

void ObjectStore::applyRetention(ObjectSeries& series, Timestamp newest_ts) {
  if (series.budget.time_window_ns > 0) {
    Timestamp threshold = newest_ts - series.budget.time_window_ns;
    while (!series.entries.empty() && series.entry_timestamps.front() < threshold) {
      evictFront(series);
    }
  }
  if (series.budget.max_memory_bytes > 0) {
    while (!series.entries.empty() && series.memory_bytes > series.budget.max_memory_bytes) {
      evictFront(series);
    }
  }
}

}  // namespace PJ
