#pragma once
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "pj/engine/dataset.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

class DataWriter;
class DataReader;

class DataEngine {
public:
  DataEngine();

  // Dataset management
  [[nodiscard]] absl::StatusOr<DatasetId> create_dataset(
      DatasetDescriptor descriptor);
  [[nodiscard]] const DatasetInfo* get_dataset(DatasetId id) const;

  // Topic management (called by DataWriter)
  [[nodiscard]] absl::StatusOr<TopicId> create_topic(
      DatasetId dataset_id, TopicDescriptor descriptor);
  [[nodiscard]] TopicStorage* get_topic_storage(TopicId id);
  [[nodiscard]] const TopicStorage* get_topic_storage(TopicId id) const;

  // Schema registry access
  [[nodiscard]] TypeRegistry& type_registry();
  [[nodiscard]] const TypeRegistry& type_registry() const;

  // Time domains
  [[nodiscard]] absl::StatusOr<TimeDomainId> create_time_domain(
      std::string name);
  [[nodiscard]] const TimeDomain* get_time_domain(TimeDomainId id) const;
  void set_display_offset(TimeDomainId id, Timestamp offset);

  // Commit cycle: commit sealed chunks, enforce retention
  void commit_chunks(
      std::vector<std::pair<TopicId, TopicChunk>> chunks);
  void enforce_retention(Timestamp retention_window_ns);

  // Writer/Reader factories
  [[nodiscard]] DataWriter create_writer();
  [[nodiscard]] DataReader create_reader() const;

  // Topic listing by dataset
  [[nodiscard]] std::vector<DatasetId> list_datasets() const;
  [[nodiscard]] std::vector<TopicId> list_topics(DatasetId dataset_id) const;

private:
  TypeRegistry type_registry_;
  DatasetId next_dataset_id_ = 1;
  TopicId next_topic_id_ = 1;
  TimeDomainId next_time_domain_id_ = 1;

  absl::flat_hash_map<DatasetId, DatasetInfo> datasets_;
  absl::flat_hash_map<TopicId, TopicStorage> topics_;
  absl::flat_hash_map<TimeDomainId, TimeDomain> time_domains_;
};

}  // namespace pj::engine
