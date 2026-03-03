#pragma once
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "PJ/base/dataset.hpp"
#include "PJ/base/expected.hpp"
#include "PJ/base/types.hpp"
#include "PJ/engine/topic_storage.hpp"
#include "PJ/engine/type_registry.hpp"

namespace PJ::engine {

class DataWriter;
class DataReader;

/// Central owner of datasets, topics, schemas, and committed chunks.
class DataEngine {
 public:
  /// Construct an empty engine instance.
  DataEngine();

  // Dataset management
  /// Create and register a dataset.
  [[nodiscard]] PJ::Expected<PJ::DatasetId> create_dataset(PJ::DatasetDescriptor descriptor);

  /// Lookup dataset by id (nullptr if missing).
  [[nodiscard]] const PJ::DatasetInfo* get_dataset(PJ::DatasetId id) const;

  // Topic management (called by DataWriter)
  /// Create a topic under a dataset.
  [[nodiscard]] PJ::Expected<PJ::TopicId> create_topic(PJ::DatasetId dataset_id, TopicDescriptor descriptor);

  /// Mutable topic storage lookup (nullptr if missing).
  [[nodiscard]] TopicStorage* get_topic_storage(PJ::TopicId id);

  /// Const topic storage lookup (nullptr if missing).
  [[nodiscard]] const TopicStorage* get_topic_storage(PJ::TopicId id) const;

  // Schema registry access
  /// Mutable schema registry access.
  [[nodiscard]] TypeRegistry& type_registry();

  /// Const schema registry access.
  [[nodiscard]] const TypeRegistry& type_registry() const;

  // Time domains
  /// Create a new time domain.
  [[nodiscard]] PJ::Expected<PJ::TimeDomainId> create_time_domain(std::string name);

  /// Lookup time domain by id (nullptr if missing).
  [[nodiscard]] const PJ::TimeDomain* get_time_domain(PJ::TimeDomainId id) const;

  /// Update display offset for one time domain.
  void set_display_offset(PJ::TimeDomainId id, PJ::Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  /// Commit flushed chunks into topic storage.
  /// Returns the deduplicated set of topic IDs that received at least one new chunk.
  /// Pass the return value directly to DerivedEngine::on_source_committed():
  ///   derived.on_source_committed(engine.commit_chunks(writer.flush_all()));
  std::vector<PJ::TopicId> commit_chunks(std::vector<std::pair<PJ::TopicId, TopicChunk>> chunks);

  /// Evict old chunks outside retention window.
  void enforce_retention(PJ::Timestamp retention_window_ns);

  // Writer/Reader factories
  /// Create a writer bound to this engine.
  [[nodiscard]] DataWriter create_writer();

  /// Create a reader bound to this engine.
  [[nodiscard]] DataReader create_reader() const;

  // Topic listing by dataset
  /// List all dataset ids.
  [[nodiscard]] std::vector<PJ::DatasetId> list_datasets() const;

  /// List topic ids for a dataset.
  [[nodiscard]] std::vector<PJ::TopicId> list_topics(PJ::DatasetId dataset_id) const;

 private:
  /// Global schema registry.
  TypeRegistry type_registry_;
  /// Id generator state for datasets.
  PJ::DatasetId next_dataset_id_ = 1;
  /// Id generator state for topics.
  PJ::TopicId next_topic_id_ = 1;
  /// Id generator state for time domains.
  PJ::TimeDomainId next_time_domain_id_ = 1;

  /// Dataset storage by id.
  absl::flat_hash_map<PJ::DatasetId, PJ::DatasetInfo> datasets_;
  /// Topic storage by id.
  absl::flat_hash_map<PJ::TopicId, TopicStorage> topics_;
  /// Time-domain storage by id.
  absl::flat_hash_map<PJ::TimeDomainId, PJ::TimeDomain> time_domains_;
};

}  // namespace PJ::engine
