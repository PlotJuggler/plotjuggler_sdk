#pragma once
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "pj/base/dataset.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::DatasetDescriptor;
using pj::DatasetId;
using pj::DatasetInfo;
using pj::TimeDomain;
using pj::TimeDomainId;
using pj::Timestamp;
using pj::TopicId;

class DataWriter;
class DataReader;

/// Central owner of datasets, topics, schemas, and committed chunks.
class DataEngine {
 public:
  /// Construct an empty engine instance.
  DataEngine();

  // Dataset management
  /// Create and register a dataset.
  [[nodiscard]] absl::StatusOr<DatasetId> create_dataset(DatasetDescriptor descriptor);

  /// Lookup dataset by id (nullptr if missing).
  [[nodiscard]] const DatasetInfo* get_dataset(DatasetId id) const;

  // Topic management (called by DataWriter)
  /// Create a topic under a dataset.
  [[nodiscard]] absl::StatusOr<TopicId> create_topic(DatasetId dataset_id, TopicDescriptor descriptor);

  /// Mutable topic storage lookup (nullptr if missing).
  [[nodiscard]] TopicStorage* get_topic_storage(TopicId id);

  /// Const topic storage lookup (nullptr if missing).
  [[nodiscard]] const TopicStorage* get_topic_storage(TopicId id) const;

  // Schema registry access
  /// Mutable schema registry access.
  [[nodiscard]] TypeRegistry& type_registry();

  /// Const schema registry access.
  [[nodiscard]] const TypeRegistry& type_registry() const;

  // Time domains
  /// Create a new time domain.
  [[nodiscard]] absl::StatusOr<TimeDomainId> create_time_domain(std::string name);

  /// Lookup time domain by id (nullptr if missing).
  [[nodiscard]] const TimeDomain* get_time_domain(TimeDomainId id) const;

  /// Update display offset for one time domain.
  void set_display_offset(TimeDomainId id, Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  /// Commit flushed chunks into topic storage.
  void commit_chunks(std::vector<std::pair<TopicId, TopicChunk>> chunks);

  /// Evict old chunks outside retention window.
  void enforce_retention(Timestamp retention_window_ns);

  // Writer/Reader factories
  /// Create a writer bound to this engine.
  [[nodiscard]] DataWriter create_writer();

  /// Create a reader bound to this engine.
  [[nodiscard]] DataReader create_reader() const;

  // Topic listing by dataset
  /// List all dataset ids.
  [[nodiscard]] std::vector<DatasetId> list_datasets() const;

  /// List topic ids for a dataset.
  [[nodiscard]] std::vector<TopicId> list_topics(DatasetId dataset_id) const;

 private:
  /// Global schema registry.
  TypeRegistry type_registry_;
  /// Id generator state for datasets.
  DatasetId next_dataset_id_ = 1;
  /// Id generator state for topics.
  TopicId next_topic_id_ = 1;
  /// Id generator state for time domains.
  TimeDomainId next_time_domain_id_ = 1;

  /// Dataset storage by id.
  absl::flat_hash_map<DatasetId, DatasetInfo> datasets_;
  /// Topic storage by id.
  absl::flat_hash_map<TopicId, TopicStorage> topics_;
  /// Time-domain storage by id.
  absl::flat_hash_map<TimeDomainId, TimeDomain> time_domains_;
};

}  // namespace pj::engine
