#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

// ObjectStore: timestamped opaque byte payloads stored beside the columnar
// DataEngine, selectable by time. See docs/OBJECT_STORE_DESIGN.md.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {

struct ObjectTopicId {
  uint32_t id = 0;

  bool operator==(const ObjectTopicId& other) const {
    return id == other.id;
  }
  bool operator!=(const ObjectTopicId& other) const {
    return id != other.id;
  }
};

/// Identity for an object topic: dataset scope + name (unique per dataset) plus
/// opaque metadata_json retained verbatim for callers that interpret bytes.
struct ObjectTopicDescriptor {
  DatasetId dataset_id = 0;
  std::string topic_name;
  std::string metadata_json;
};

/// Outcome of ObjectStore::replaceDatasetFrom() — the object-side parallel of
/// DatasetReplaceResult. The remapped pairs let the caller re-register object
/// parsers under the stable primary ObjectTopicId after the swap.
struct ObjectDatasetReplaceResult {
  /// (staged ObjectTopicId, primary ObjectTopicId) for every adopted topic
  /// (both matched and newly added), so a parser keyed by the staged id can be
  /// re-registered under the primary id. A newly added topic is simply one whose
  /// primary id did not exist before — callers needing only the remap use this.
  std::vector<std::pair<ObjectTopicId, ObjectTopicId>> remapped;
  /// Primary object topics removed (no staged match).
  std::vector<ObjectTopicId> removed_topics;
};

/// Eager payload: store-owned bytes, counted against the retention budget.
using SharedBuffer = std::shared_ptr<const std::vector<uint8_t>>;

/// Lazy payload: idempotent, thread-safe fetcher returning bytes + anchor.
/// Invoked on every read; bytes are not counted against the retention budget.
using LazyCallback = std::function<sdk::PayloadView()>;

struct ObjectEntry {
  Timestamp timestamp = 0;
  // Eager owned bytes or a lazy resolver; resolveEntry discriminates via std::get_if.
  std::variant<SharedBuffer, LazyCallback> payload;
};

struct ResolvedObjectEntry {
  Timestamp timestamp = 0;
  // Non-owning Span over the bytes plus an opaque anchor (any shared_ptr<T>).
  // Consumers read `payload.bytes`; retain `payload.anchor` to keep the bytes
  // alive past the resolve call. resolveEntry never casts the anchor.
  sdk::PayloadView payload;
};

struct RetentionBudget {
  int64_t time_window_ns = 0;
  size_t max_memory_bytes = 0;
};

/// Read view over a topic's entry timestamps that holds the series read lock for
/// its lifetime; keep it short-lived to avoid blocking writers.
class EntryTimestampsView {
 public:
  EntryTimestampsView() = default;
  EntryTimestampsView(std::shared_lock<std::shared_mutex> lock, const std::vector<Timestamp>* timestamps)
      : lock_(std::move(lock)), timestamps_(timestamps) {}

  [[nodiscard]] bool empty() const {
    return timestamps_ == nullptr || timestamps_->empty();
  }
  [[nodiscard]] size_t size() const {
    return timestamps_ != nullptr ? timestamps_->size() : 0;
  }
  [[nodiscard]] Timestamp operator[](size_t i) const {
    return (*timestamps_)[i];
  }
  [[nodiscard]] const Timestamp* begin() const {
    return timestamps_ != nullptr ? timestamps_->data() : nullptr;
  }
  [[nodiscard]] const Timestamp* end() const {
    return timestamps_ != nullptr ? timestamps_->data() + timestamps_->size() : nullptr;
  }

 private:
  std::shared_lock<std::shared_mutex> lock_;
  const std::vector<Timestamp>* timestamps_ = nullptr;
};

/// Timestamped opaque-blob store living alongside DataEngine: payloads selected
/// by time but never expanded into scalar columns (images, point clouds,
/// annotations). Owned (`pushOwned`) or lazy (`pushLazy`) entries, per-topic
/// retention, at-or-before lookup. Thread-safe: one shared_mutex per series +
/// one global lock for registration. Does NOT decode payloads or own renderer/
/// UI policy. See docs/OBJECT_STORE_DESIGN.md.
class ObjectStore {
 public:
  ObjectStore() = default;
  ~ObjectStore() = default;

  ObjectStore(const ObjectStore&) = delete;
  ObjectStore& operator=(const ObjectStore&) = delete;
  ObjectStore(ObjectStore&&) = delete;
  ObjectStore& operator=(ObjectStore&&) = delete;

  // --- Registration ---

  Expected<ObjectTopicId> registerTopic(const ObjectTopicDescriptor& descriptor);

  // Resolve a topic id by (dataset_id, topic_name) without registering. Returns
  // nullopt if no topic with that key exists. Used by hosts that need to bind a
  // parser-side write surface to a topic the source already registered.
  std::optional<ObjectTopicId> findTopic(DatasetId dataset_id, std::string_view topic_name) const;

  const ObjectTopicDescriptor& descriptor(ObjectTopicId id) const;

  std::vector<ObjectTopicId> listTopics() const;
  std::vector<ObjectTopicId> listTopics(DatasetId dataset_id) const;

  // --- Write ---

  Status pushOwned(ObjectTopicId id, Timestamp timestamp, std::vector<uint8_t> payload);

  // Fetcher runs on every read; the store retains the anchor via PayloadView
  // and never copies. The closure can return a view over bytes the producer
  // already owns (chunk cache, mmap, hand-off between stores).
  Status pushLazy(ObjectTopicId id, Timestamp timestamp, LazyCallback fetch);

  // --- Read ---

  std::optional<ResolvedObjectEntry> latestAt(ObjectTopicId id, Timestamp timestamp) const;

  std::optional<ResolvedObjectEntry> at(ObjectTopicId id, size_t index) const;

  std::optional<size_t> indexAt(ObjectTopicId id, Timestamp timestamp) const;

  size_t entryCount(ObjectTopicId id) const;

  std::pair<Timestamp, Timestamp> timeRange(ObjectTopicId id) const;

  EntryTimestampsView entryTimestamps(ObjectTopicId id) const;

  // --- Retention ---

  void setRetentionBudget(ObjectTopicId id, RetentionBudget budget);
  RetentionBudget retentionBudget(ObjectTopicId id) const;
  size_t memoryUsage(ObjectTopicId id) const;

  // --- Explicit eviction ---

  void evictBefore(ObjectTopicId id, Timestamp threshold);
  void evictAllBefore(Timestamp threshold);

  // --- Cross-store flush ---

  // Move every entry into `dst`, leaving this store empty (registrations kept).
  // Topics are matched by descriptor (dataset_id + topic_name); both stores
  // must share descriptors. Monotonicity is enforced per series: the earliest
  // moved timestamp must be >= the destination's last. Any validation failure
  // returns an error and mutates neither store.
  //
  // Zero-copy: each ObjectEntry is moved by value, so the variant's shared_ptr
  // or closure transfers as a pointer move — bytes are never copied. Lazy
  // entries keep their semantics; their closure re-runs only on a dst read.
  // Afterward, dst's retention budget is applied to each touched series.
  Status flushTo(ObjectStore& dst);

  // In-place REPLACE of object dataset `primary_id` with the topics ingested
  // into `staged` dataset `staged_id` — the object-store half of reload. Topics
  // are matched by name. Matched topics keep their primary ObjectTopicId and have
  // their entries replaced; staged-only topics are registered under `primary_id`;
  // primary-only topics are removed. Returns the staged->primary id map so the
  // caller re-registers parsers under the stable ids. Either fully applies or
  // (on a validation error) mutates neither store.
  [[nodiscard]] Expected<ObjectDatasetReplaceResult>
  replaceDatasetFrom(ObjectStore& staged, DatasetId staged_id, DatasetId primary_id);

  // --- Lifecycle ---

  void removeTopic(ObjectTopicId id);
  void clear();

 private:
  struct ObjectSeries {
    ObjectTopicDescriptor descriptor;
    std::deque<ObjectEntry> entries;
    std::vector<Timestamp> entry_timestamps;
    RetentionBudget budget;
    size_t memory_bytes = 0;
    mutable std::shared_mutex mutex;
  };

  ObjectSeries* findSeries(ObjectTopicId id);
  const ObjectSeries* findSeries(ObjectTopicId id) const;

  // Erase a topic from topics_. Caller must already hold store_mutex_ (used by
  // removeTopic under its own lock and by replaceDatasetFrom under the dual lock).
  void eraseTopicLocked(ObjectTopicId id);

  static std::optional<size_t> upperBoundIndex(const std::vector<Timestamp>& timestamps, Timestamp ts);
  static ResolvedObjectEntry resolveEntry(const ObjectEntry& entry);

  void evictFront(ObjectSeries& series);
  void applyRetention(ObjectSeries& series, Timestamp newest_ts);

  mutable std::shared_mutex store_mutex_;
  std::vector<std::pair<ObjectTopicId, std::unique_ptr<ObjectSeries>>> topics_;
  uint32_t next_id_ = 1;
};

}  // namespace PJ
