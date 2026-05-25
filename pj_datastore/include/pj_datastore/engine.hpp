#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <memory>
#include <string>
#include <vector>

#include "pj_base/dataset.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/types.hpp"
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

  /// Evict old chunks outside retention window.
  void enforceRetention(PJ::Timestamp retention_window_ns);

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace PJ
