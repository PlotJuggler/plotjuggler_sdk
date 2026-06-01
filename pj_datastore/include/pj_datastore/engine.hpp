#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <memory>
#include <string>
#include <vector>

#include "pj_base/dataset.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/replace_result.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/type_registry.hpp"

namespace PJ {

class DataWriter;
class DataReader;

/// Central owner of datasets, topics, schemas, and committed chunks.
class DataEngine {
 public:
  /// Construct an empty engine instance.
  DataEngine();

  /// Destructor (defined in .cpp for pimpl).
  ~DataEngine();

  /// Move constructor.
  DataEngine(DataEngine&&) noexcept;

  /// Move assignment.
  DataEngine& operator=(DataEngine&&) noexcept;

  /// Deleted copy constructor.
  DataEngine(const DataEngine&) = delete;

  /// Deleted copy assignment.
  DataEngine& operator=(const DataEngine&) = delete;

  // Dataset management
  /// Create and register a dataset.
  [[nodiscard]] PJ::Expected<PJ::DatasetId> createDataset(PJ::DatasetDescriptor descriptor);

  /// Lookup dataset by id (nullptr if missing).
  [[nodiscard]] const PJ::DatasetInfo* getDataset(PJ::DatasetId id) const;

  // Topic management (called by DataWriter)
  /// Create a topic under a dataset.
  [[nodiscard]] PJ::Expected<PJ::TopicId> createTopic(PJ::DatasetId dataset_id, TopicDescriptor descriptor);

  /// Mutable topic storage lookup (nullptr if missing).
  [[nodiscard]] TopicStorage* getTopicStorage(PJ::TopicId id);

  /// Const topic storage lookup (nullptr if missing).
  [[nodiscard]] const TopicStorage* getTopicStorage(PJ::TopicId id) const;

  // Schema registry access
  /// Mutable schema registry access.
  [[nodiscard]] TypeRegistry& typeRegistry();

  /// Const schema registry access.
  [[nodiscard]] const TypeRegistry& typeRegistry() const;

  // Time domains
  /// Create a new time domain.
  [[nodiscard]] PJ::Expected<PJ::TimeDomainId> createTimeDomain(std::string name);

  /// Lookup time domain by id (nullptr if missing).
  [[nodiscard]] const PJ::TimeDomain* getTimeDomain(PJ::TimeDomainId id) const;

  /// Update display offset for one time domain.
  void setDisplayOffset(PJ::TimeDomainId id, PJ::Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  /// Commit flushed chunks into topic storage.
  /// Returns the deduplicated set of topic IDs that received at least one new chunk.
  /// Pass the return value directly to DerivedEngine::onSourceCommitted():
  ///   derived.onSourceCommitted(engine.commitChunks(writer.flushAll()));
  std::vector<PJ::TopicId> commitChunks(std::vector<std::pair<PJ::TopicId, TopicChunk>> chunks);

  /// Evict old chunks outside the retention window.
  void enforceRetention(PJ::Timestamp retention_window_ns);

  /// Move every committed chunk into `dst`, leaving this engine's storages
  /// empty (datasets, topics, schemas, time domains stay registered). Topics
  /// are matched by descriptor (`dataset_id` + `name`); both engines must have
  /// them registered. Monotonicity is enforced per topic: the source's
  /// earliest chunk timestamp must be >= the destination's `time_max()`. Any
  /// failure mutates neither engine.
  ///
  /// Zero-copy: dst's `std::deque<TopicChunk>` receives the chunks via
  /// `std::move` (column buffers/value arrays are pointer moves). Schema
  /// compatibility is the caller's responsibility — typically dst is kept in
  /// lockstep with the source via parallel registration at startup.
  PJ::Status flushTo(DataEngine& dst);

  /// In-place REPLACE of dataset `primary_id`'s data with the data ingested into
  /// dataset `staged_id` of a throwaway `staged` engine — used for reload so the
  /// primary DatasetId/TopicIds (and therefore all curve keys) stay stable.
  /// Topics are matched by name. Matched primary topics have their chunks cleared
  /// and replaced with the staged topic's chunks (each chunk's `topic_id` rewritten
  /// to the primary id); their inline column layout is copied across. Staged-only
  /// topics are created under `primary_id`. Primary-only topics are retired (chunks
  /// cleared, id excluded from `listTopics`, storage kept so cached reader pointers
  /// see an empty deque rather than dangling).
  ///
  /// Caller MUST invalidate every reader/adapter bound to `primary_id` BEFORE this
  /// call (no live `TopicChunk*` may survive into the chunk clear) and run no event
  /// loop between that invalidation and this call. The staged engine is drained.
  [[nodiscard]] PJ::Expected<DatasetReplaceResult>
  replaceDatasetFrom(DataEngine& staged, PJ::DatasetId staged_id, PJ::DatasetId primary_id);

  // Writer/Reader factories
  /// Create a writer bound to this engine.
  [[nodiscard]] DataWriter createWriter();

  /// Create a reader bound to this engine.
  [[nodiscard]] DataReader createReader() const;

  // Topic listing by dataset
  /// List all dataset ids.
  [[nodiscard]] std::vector<PJ::DatasetId> listDatasets() const;

  /// List topic ids for a dataset.
  [[nodiscard]] std::vector<PJ::TopicId> listTopics(PJ::DatasetId dataset_id) const;

 private:
  // Move src's sealed chunks into dst, re-stamping each chunk's topic_id to
  // dst's id. Shared by flushTo (append between mirrored topics) and
  // replaceDatasetFrom (after dst is cleared). Needs friend access to
  // TopicStorage::sealed_chunks_.
  static void adoptChunksFrom(TopicStorage& dst, TopicStorage& src);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace PJ
