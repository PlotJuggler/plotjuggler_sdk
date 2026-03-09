#pragma once

#include <memory>

#include "pj_base/expected.hpp"
#include "pj_datastore/plugin_host_types.hpp"

namespace PJ {

class DataEngine;

// ---------------------------------------------------------------------------
// PluginHostRead — plugin-facing read service over pj_datastore
//
// Provides catalog enumeration and single-field materialization.
// Plugins interact exclusively through this interface and never touch
// DataReader, RangeCursor, or TopicChunk directly.
//
// Thread safety: not thread-safe.  The host runtime must serialise access.
// ---------------------------------------------------------------------------

class PluginHostRead {
 public:
  explicit PluginHostRead(const DataEngine& engine);
  ~PluginHostRead();

  PluginHostRead(const PluginHostRead&) = delete;
  PluginHostRead& operator=(const PluginHostRead&) = delete;
  PluginHostRead(PluginHostRead&&) noexcept;
  PluginHostRead& operator=(PluginHostRead&&) noexcept;

  // -- Catalog enumeration --------------------------------------------------

  // Returns a borrowed, read-only snapshot of the source/topic/field tree.
  // Valid until the next structural catalog mutation or destruction of this
  // object.  Appending sample data to existing fields does NOT invalidate.
  [[nodiscard]] CatalogView catalogView();

  // -- Materialized field read ----------------------------------------------

  // Returns the full retained history of one field as caller-owned vectors.
  [[nodiscard]] Expected<MaterializedSeries> readSeries(FieldHandle field) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace PJ
