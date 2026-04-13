#pragma once

#include <memory>

#include "pj_base/plugin_data_api.h"

namespace PJ {

class DataEngine;
struct DatastoreSourceWriteHostState;
struct DatastoreParserWriteHostState;
struct DatastoreToolboxHostState;

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

  /// Update the visible time range reported to plugins via get_visible_range (in ns).
  /// Called by the application whenever the main chart's viewport changes.
  void setVisibleRange(int64_t t_min_ns, int64_t t_max_ns);

  /// Clear the visible range (subsequent get_visible_range calls will return false).
  void clearVisibleRange();

 private:
  std::unique_ptr<DatastoreToolboxHostState> state_;
};

}  // namespace PJ
