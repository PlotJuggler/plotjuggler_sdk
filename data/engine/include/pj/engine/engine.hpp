#pragma once
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "pj/base/dataset.hpp"
#include "pj/base/expected.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"

namespace pj::engine {

class DataWriter;
class DataReader;

/// Central owner of datasets, topics, schemas, and committed chunks.
class DataEngine {
 public:
  /// Construct an empty engine instance.
  DataEngine();

  // Dataset management
  /// Create and register a dataset.
  [[nodiscard]] pj::Expected<pj::DatasetId> create_dataset(pj::DatasetDescriptor descriptor);

  /// Lookup dataset by id (nullptr if missing).
  [[nodiscard]] const pj::DatasetInfo* get_dataset(pj::DatasetId id) const;

  // Topic management (called by DataWriter)
  /// Create a topic under a dataset.
  [[nodiscard]] pj::Expected<pj::TopicId> create_topic(pj::DatasetId dataset_id, TopicDescriptor descriptor);

  /// Mutable topic storage lookup (nullptr if missing).
  [[nodiscard]] TopicStorage* get_topic_storage(pj::TopicId id);

  /// Const topic storage lookup (nullptr if missing).
  [[nodiscard]] const TopicStorage* get_topic_storage(pj::TopicId id) const;

  // Schema registry access
  /// Mutable schema registry access.
  [[nodiscard]] TypeRegistry& type_registry();

  /// Const schema registry access.
  [[nodiscard]] const TypeRegistry& type_registry() const;

  // Time domains
  /// Create a new time domain.
  [[nodiscard]] pj::Expected<pj::TimeDomainId> create_time_domain(std::string name);

  /// Lookup time domain by id (nullptr if missing).
  [[nodiscard]] const pj::TimeDomain* get_time_domain(pj::TimeDomainId id) const;

  /// Update display offset for one time domain.
  void set_display_offset(pj::TimeDomainId id, pj::Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  /// Commit flushed chunks into topic storage.
  /// Returns the deduplicated set of topic IDs that received at least one new chunk.
  /// Pass the return value directly to DerivedEngine::on_source_committed():
  ///   derived.on_source_committed(engine.commit_chunks(writer.flush_all()));
  std::vector<pj::TopicId> commit_chunks(std::vector<std::pair<pj::TopicId, TopicChunk>> chunks);

  /// Evict old chunks outside retention window.
  void enforce_retention(pj::Timestamp retention_window_ns);

  // Writer/Reader factories
  /// Create a writer bound to this engine.
  [[nodiscard]] DataWriter create_writer();

  /// Create a reader bound to this engine.
  [[nodiscard]] DataReader create_reader() const;

  // Topic listing by dataset
  /// List all dataset ids.
  [[nodiscard]] std::vector<pj::DatasetId> list_datasets() const;

  /// List topic ids for a dataset.
  [[nodiscard]] std::vector<pj::TopicId> list_topics(pj::DatasetId dataset_id) const;

 private:
  /// Global schema registry.
  TypeRegistry type_registry_;
  /// Id generator state for datasets.
  pj::DatasetId next_dataset_id_ = 1;
  /// Id generator state for topics.
  pj::TopicId next_topic_id_ = 1;
  /// Id generator state for time domains.
  pj::TimeDomainId next_time_domain_id_ = 1;

  /// Dataset storage by id.
  absl::flat_hash_map<pj::DatasetId, pj::DatasetInfo> datasets_;
  /// Topic storage by id.
  absl::flat_hash_map<pj::TopicId, TopicStorage> topics_;
  /// Time-domain storage by id.
  absl::flat_hash_map<pj::TimeDomainId, pj::TimeDomain> time_domains_;
};

}  // namespace pj::engine
