#pragma once

#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/plugin_host_types.hpp"

namespace PJ {

class DataEngine;

// ---------------------------------------------------------------------------
// PluginHostWrite — plugin-facing write service over pj_datastore
//
// This is the stable host service layer that the future plugin runtime will
// call.  Plugins interact exclusively through this interface and never touch
// DataWriter, TopicChunkBuilder, or commit/flush mechanics directly.
//
// Thread safety: not thread-safe.  The host runtime must serialise access.
// ---------------------------------------------------------------------------

class PluginHostWrite {
 public:
  explicit PluginHostWrite(DataEngine& engine);
  ~PluginHostWrite();

  PluginHostWrite(const PluginHostWrite&) = delete;
  PluginHostWrite& operator=(const PluginHostWrite&) = delete;
  PluginHostWrite(PluginHostWrite&&) noexcept;
  PluginHostWrite& operator=(PluginHostWrite&&) noexcept;

  // -- Structural operations ------------------------------------------------

  [[nodiscard]] Expected<DataSourceHandle> createDataSource(std::string_view name);

  [[nodiscard]] Expected<TopicHandle> ensureTopic(DataSourceHandle source, std::string_view topic_name);

  [[nodiscard]] Expected<FieldHandle> ensureField(TopicHandle topic, std::string_view field_name, FieldType type);

  // -- Incremental logical writes -------------------------------------------

  [[nodiscard]] Status appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields);

  [[nodiscard]] Status appendRecordFast(TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields);

  // -- Bulk Arrow IPC writes ------------------------------------------------

  [[nodiscard]] Status appendArrowIpc(TopicHandle topic, Span<const uint8_t> ipc_stream,
                                      std::string_view timestamp_column = "_timestamp");

  // -- Host-side control (not exposed to plugins) ---------------------------

  // Seals all pending chunk builders and commits sealed chunks to storage.
  void flush();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace PJ
