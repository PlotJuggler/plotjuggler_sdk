#pragma once

#include <memory>

#include "pj_base/plugin_data_api.h"
#include "pj_base/types.hpp"

namespace PJ {

class DataEngine;
class ObjectStore;
struct DatastoreSourceWriteHostState;
struct DatastoreSourceObjectWriteHostState;
struct DatastoreParserWriteHostState;
struct DatastoreParserObjectWriteHostState;
struct DatastoreToolboxHostState;
struct DatastoreToolboxObjectReadHostState;

class DatastoreSourceWriteHost {
 public:
  DatastoreSourceWriteHost(DataEngine& engine, PJ_data_source_handle_t source);
  ~DatastoreSourceWriteHost();

  DatastoreSourceWriteHost(const DatastoreSourceWriteHost&) = delete;
  DatastoreSourceWriteHost& operator=(const DatastoreSourceWriteHost&) = delete;
  DatastoreSourceWriteHost(DatastoreSourceWriteHost&&) noexcept;
  DatastoreSourceWriteHost& operator=(DatastoreSourceWriteHost&&) noexcept;

  [[nodiscard]] PJ_source_write_host_t raw() noexcept;
  void flushPending();

 private:
  std::unique_ptr<DatastoreSourceWriteHostState> state_;
};

/// Host-side implementation of the scalar-peer object-write surface exposed
/// as `pj.source_object_write.v1`. Bridges the C ABI onto
/// `pj_datastore::ObjectStore`. One instance per DataSource session; the
/// `DatasetId` scopes newly-registered topics to the enclosing dataset.
class DatastoreSourceObjectWriteHost {
 public:
  DatastoreSourceObjectWriteHost(ObjectStore& store, DatasetId dataset_id);
  ~DatastoreSourceObjectWriteHost();

  DatastoreSourceObjectWriteHost(const DatastoreSourceObjectWriteHost&) = delete;
  DatastoreSourceObjectWriteHost& operator=(const DatastoreSourceObjectWriteHost&) = delete;
  DatastoreSourceObjectWriteHost(DatastoreSourceObjectWriteHost&&) noexcept;
  DatastoreSourceObjectWriteHost& operator=(DatastoreSourceObjectWriteHost&&) noexcept;

  [[nodiscard]] PJ_object_write_host_t raw() noexcept;

 private:
  std::unique_ptr<DatastoreSourceObjectWriteHostState> state_;
};

class DatastoreParserWriteHost {
 public:
  DatastoreParserWriteHost(DataEngine& engine, PJ_topic_handle_t topic);
  ~DatastoreParserWriteHost();

  DatastoreParserWriteHost(const DatastoreParserWriteHost&) = delete;
  DatastoreParserWriteHost& operator=(const DatastoreParserWriteHost&) = delete;
  DatastoreParserWriteHost(DatastoreParserWriteHost&&) noexcept;
  DatastoreParserWriteHost& operator=(DatastoreParserWriteHost&&) noexcept;

  [[nodiscard]] PJ_parser_write_host_t raw() noexcept;
  void flushPending();

 private:
  std::unique_ptr<DatastoreParserWriteHostState> state_;
};

/// Host-side implementation of the toolbox object-read surface exposed as
/// `pj.toolbox_object_read.v1`. Bridges the C ABI onto
/// `pj_datastore::ObjectStore`, allocating an owning handle per successful
/// `read_latest_at`. The handle keeps bytes alive independent of the
/// store's internal state, matching the `shared_ptr` model.
class DatastoreToolboxObjectReadHost {
 public:
  explicit DatastoreToolboxObjectReadHost(ObjectStore& store);
  ~DatastoreToolboxObjectReadHost();

  DatastoreToolboxObjectReadHost(const DatastoreToolboxObjectReadHost&) = delete;
  DatastoreToolboxObjectReadHost& operator=(const DatastoreToolboxObjectReadHost&) = delete;
  DatastoreToolboxObjectReadHost(DatastoreToolboxObjectReadHost&&) noexcept;
  DatastoreToolboxObjectReadHost& operator=(DatastoreToolboxObjectReadHost&&) noexcept;

  [[nodiscard]] PJ_object_read_host_t raw() noexcept;

 private:
  std::unique_ptr<DatastoreToolboxObjectReadHostState> state_;
};

/// Host-side implementation of the parser-scoped object write surface
/// exposed as `pj.parser_object_write.v1`. The target ObjectTopic is bound
/// at construction time (matching the scalar `DatastoreParserWriteHost`
/// pattern); the parser never names topics.
///
/// @param topic_id the raw `ObjectTopicId::id` of the bound topic.
class DatastoreParserObjectWriteHost {
 public:
  DatastoreParserObjectWriteHost(ObjectStore& store, uint32_t topic_id);
  ~DatastoreParserObjectWriteHost();

  DatastoreParserObjectWriteHost(const DatastoreParserObjectWriteHost&) = delete;
  DatastoreParserObjectWriteHost& operator=(const DatastoreParserObjectWriteHost&) = delete;
  DatastoreParserObjectWriteHost(DatastoreParserObjectWriteHost&&) noexcept;
  DatastoreParserObjectWriteHost& operator=(DatastoreParserObjectWriteHost&&) noexcept;

  [[nodiscard]] PJ_parser_object_write_host_t raw() noexcept;

 private:
  std::unique_ptr<DatastoreParserObjectWriteHostState> state_;
};

class DatastoreToolboxHost {
 public:
  explicit DatastoreToolboxHost(DataEngine& engine);
  ~DatastoreToolboxHost();

  DatastoreToolboxHost(const DatastoreToolboxHost&) = delete;
  DatastoreToolboxHost& operator=(const DatastoreToolboxHost&) = delete;
  DatastoreToolboxHost(DatastoreToolboxHost&&) noexcept;
  DatastoreToolboxHost& operator=(DatastoreToolboxHost&&) noexcept;

  [[nodiscard]] PJ_toolbox_host_t raw() noexcept;
  void flushPending();

 private:
  std::unique_ptr<DatastoreToolboxHostState> state_;
};

}  // namespace PJ
