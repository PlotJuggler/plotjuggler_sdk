#include "pj_datastore/plugin_host_write.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "pj_base/expected.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/arrow_import.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_host_types.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct PluginHostWrite::Impl {
  explicit Impl(DataEngine& eng) : engine_(eng), writer_(eng.createWriter()) {}

  DataEngine& engine_;
  DataWriter writer_;

  // topic_name → TopicId, keyed by (DatasetId, topic_name)
  struct DatasetTopicKey {
    DatasetId dataset_id;
    std::string topic_name;

    friend bool operator==(const DatasetTopicKey& a, const DatasetTopicKey& b) {
      return a.dataset_id == b.dataset_id && a.topic_name == b.topic_name;
    }

    template <typename H>
    friend H AbslHashValue(H h, const DatasetTopicKey& k) {
      return H::combine(std::move(h), k.dataset_id, k.topic_name);
    }
  };
  absl::flat_hash_map<DatasetTopicKey, TopicHandle> topic_cache_;

  // field_name → FieldHandle, keyed by (TopicId, field_name)
  struct TopicFieldKey {
    TopicId topic_id;
    std::string field_name;

    friend bool operator==(const TopicFieldKey& a, const TopicFieldKey& b) {
      return a.topic_id == b.topic_id && a.field_name == b.field_name;
    }

    template <typename H>
    friend H AbslHashValue(H h, const TopicFieldKey& k) {
      return H::combine(std::move(h), k.topic_id, k.field_name);
    }
  };
  absl::flat_hash_map<TopicFieldKey, FieldHandle> field_cache_;

  // field type registry per (topic, field_id)
  struct TopicFieldIdKey {
    TopicId topic_id;
    FieldId field_id;

    friend bool operator==(const TopicFieldIdKey& a, const TopicFieldIdKey& b) {
      return a.topic_id == b.topic_id && a.field_id == b.field_id;
    }

    template <typename H>
    friend H AbslHashValue(H h, const TopicFieldIdKey& k) {
      return H::combine(std::move(h), k.topic_id, k.field_id);
    }
  };
  absl::flat_hash_map<TopicFieldIdKey, FieldType> field_types_;
};

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------

PluginHostWrite::PluginHostWrite(DataEngine& engine) : impl_(std::make_unique<Impl>(engine)) {}
PluginHostWrite::~PluginHostWrite() = default;
PluginHostWrite::PluginHostWrite(PluginHostWrite&&) noexcept = default;
PluginHostWrite& PluginHostWrite::operator=(PluginHostWrite&&) noexcept = default;

// ---------------------------------------------------------------------------
// Structural operations
// ---------------------------------------------------------------------------

Expected<DataSourceHandle> PluginHostWrite::createDataSource(std::string_view name) {
  auto id_or = impl_->engine_.createDataset(DatasetDescriptor{.source_name = std::string(name), .time_domain_id = 0});
  if (!id_or.has_value()) {
    return PJ::unexpected(id_or.error());
  }
  return DataSourceHandle{.id = *id_or};
}

Expected<TopicHandle> PluginHostWrite::ensureTopic(DataSourceHandle source, std::string_view topic_name) {
  // Validate source exists
  const auto* dataset = impl_->engine_.getDataset(source.id);
  if (dataset == nullptr) {
    return PJ::unexpected(absl::StrCat("ensureTopic: data source ", source.id, " not found"));
  }

  // Check cache
  Impl::DatasetTopicKey key{.dataset_id = source.id, .topic_name = std::string(topic_name)};
  auto it = impl_->topic_cache_.find(key);
  if (it != impl_->topic_cache_.end()) {
    return it->second;
  }

  // Search existing topics in this dataset
  for (const TopicId tid : impl_->engine_.listTopics(source.id)) {
    const auto* storage = impl_->engine_.getTopicStorage(tid);
    if (storage != nullptr && storage->descriptor().name == topic_name) {
      TopicHandle handle{.id = tid};
      impl_->topic_cache_.emplace(std::move(key), handle);
      return handle;
    }
  }

  // Create new topic with schema_id=0 (inline/dynamic columns)
  TopicDescriptor desc;
  desc.name = std::string(topic_name);
  desc.schema_id = 0;
  auto tid_or = impl_->writer_.registerTopic(source.id, std::move(desc));
  if (!tid_or.has_value()) {
    return PJ::unexpected(tid_or.error());
  }

  TopicHandle handle{.id = *tid_or};
  impl_->topic_cache_.emplace(std::move(key), handle);
  return handle;
}

Expected<FieldHandle> PluginHostWrite::ensureField(TopicHandle topic, std::string_view field_name, FieldType type) {
  // Validate topic exists
  const auto* storage = impl_->engine_.getTopicStorage(topic.id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("ensureField: topic ", topic.id, " not found"));
  }

  // Check cache
  Impl::TopicFieldKey key{.topic_id = topic.id, .field_name = std::string(field_name)};
  auto it = impl_->field_cache_.find(key);
  if (it != impl_->field_cache_.end()) {
    // Verify type matches
    Impl::TopicFieldIdKey tid_key{.topic_id = topic.id, .field_id = it->second.id};
    auto type_it = impl_->field_types_.find(tid_key);
    if (type_it != impl_->field_types_.end() && type_it->second != type) {
      return PJ::unexpected(
          absl::StrCat("ensureField: field '", field_name, "' already exists with a different type"));
    }
    return it->second;
  }

  // Delegate to DataWriter::ensureColumn
  auto field_id_or = impl_->writer_.ensureColumn(topic.id, field_name, toPrimitiveType(type));
  if (!field_id_or.has_value()) {
    return PJ::unexpected(field_id_or.error());
  }

  FieldHandle handle{.topic = topic, .id = *field_id_or};
  impl_->field_cache_.emplace(std::move(key), handle);
  impl_->field_types_[{.topic_id = topic.id, .field_id = *field_id_or}] = type;
  return handle;
}

// ---------------------------------------------------------------------------
// Helpers — set one field value on the current row
// ---------------------------------------------------------------------------

namespace {

void setFieldValue(DataWriter& writer, TopicId topic_id, std::size_t col_index, const ValueRef& value) {
  std::visit(
      [&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          writer.setFloat32(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, double>) {
          writer.setFloat64(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, int32_t>) {
          writer.setInt32(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          writer.setInt64(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          writer.setUint64(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, bool>) {
          writer.setBool(topic_id, col_index, v);
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          writer.setString(topic_id, col_index, v);
        }
      },
      value);
}

Status validateValueType(FieldType expected, const ValueRef& value, std::string_view context) {
  if (value.index() != static_cast<std::size_t>(expected)) {
    return PJ::unexpected(absl::StrCat(context, ": ValueRef type mismatch"));
  }
  return PJ::okStatus();
}

}  // namespace

// ---------------------------------------------------------------------------
// Incremental logical writes
// ---------------------------------------------------------------------------

Status PluginHostWrite::appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) {
  // Validate topic
  const auto* storage = impl_->engine_.getTopicStorage(topic.id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("appendRecord: topic ", topic.id, " not found"));
  }

  // Check for duplicate field names
  absl::flat_hash_set<std::string_view> seen_names;
  for (const auto& f : fields) {
    if (!seen_names.insert(f.name).second) {
      return PJ::unexpected(absl::StrCat("appendRecord: duplicate field name '", f.name, "'"));
    }
  }

  // Resolve/create all fields first
  std::vector<std::pair<FieldId, const NamedFieldValue*>> resolved;
  resolved.reserve(fields.size());
  for (const auto& f : fields) {
    auto handle_or = ensureField(topic, f.name, f.type);
    if (!handle_or.has_value()) {
      return PJ::unexpected(handle_or.error());
    }
    resolved.emplace_back(handle_or->id, &f);
  }

  // Begin row
  auto begin_status = impl_->writer_.beginRow(topic.id, timestamp);
  if (!begin_status.has_value()) {
    return PJ::unexpected(begin_status.error());
  }

  // Set field values (validate type before writing to avoid storage-kind asserts)
  for (const auto& [field_id, fv] : resolved) {
    if (fv->is_null) {
      impl_->writer_.setNull(topic.id, static_cast<std::size_t>(field_id));
    } else {
      auto vt_status = validateValueType(fv->type, fv->value, "appendRecord");
      if (!vt_status.has_value()) {
        return vt_status;
      }
      setFieldValue(impl_->writer_, topic.id, static_cast<std::size_t>(field_id), fv->value);
    }
  }

  // Finish row
  auto finish_status = impl_->writer_.finishRow(topic.id);
  if (!finish_status.has_value()) {
    return PJ::unexpected(finish_status.error());
  }

  return PJ::okStatus();
}

Status PluginHostWrite::appendRecordFast(TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) {
  // Validate topic
  const auto* storage = impl_->engine_.getTopicStorage(topic.id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("appendRecordFast: topic ", topic.id, " not found"));
  }

  // Validate all handles belong to this topic, exist in the column layout, and check for duplicates
  const auto& col_descs = storage->columnDescriptors();
  absl::flat_hash_set<FieldId> seen_ids;
  for (const auto& f : fields) {
    if (f.field.topic != topic) {
      return PJ::unexpected(
          absl::StrCat("appendRecordFast: field handle belongs to topic ", f.field.topic.id, ", expected ", topic.id));
    }
    // Validate field_id is within the column layout bounds
    if (static_cast<std::size_t>(f.field.id) >= col_descs.size()) {
      return PJ::unexpected(
          absl::StrCat("appendRecordFast: field id ", f.field.id, " out of range (topic has ", col_descs.size(), " columns)"));
    }
    if (!seen_ids.insert(f.field.id).second) {
      return PJ::unexpected(absl::StrCat("appendRecordFast: duplicate field handle id ", f.field.id));
    }
    // Validate value type matches field type
    if (!f.is_null) {
      auto type_it = impl_->field_types_.find({.topic_id = topic.id, .field_id = f.field.id});
      if (type_it != impl_->field_types_.end()) {
        auto status = validateValueType(type_it->second, f.value, "appendRecordFast");
        if (!status.has_value()) {
          return status;
        }
      }
    }
  }

  // Begin row
  auto begin_status = impl_->writer_.beginRow(topic.id, timestamp);
  if (!begin_status.has_value()) {
    return PJ::unexpected(begin_status.error());
  }

  // Set field values
  for (const auto& f : fields) {
    if (f.is_null) {
      impl_->writer_.setNull(topic.id, static_cast<std::size_t>(f.field.id));
    } else {
      setFieldValue(impl_->writer_, topic.id, static_cast<std::size_t>(f.field.id), f.value);
    }
  }

  // Finish row
  auto finish_status = impl_->writer_.finishRow(topic.id);
  if (!finish_status.has_value()) {
    return PJ::unexpected(finish_status.error());
  }

  return PJ::okStatus();
}

// ---------------------------------------------------------------------------
// Bulk Arrow IPC writes
// ---------------------------------------------------------------------------

Status PluginHostWrite::appendArrowIpc(TopicHandle topic, Span<const uint8_t> ipc_stream,
                                       std::string_view timestamp_column) {
  // Validate topic
  const auto* storage = impl_->engine_.getTopicStorage(topic.id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("appendArrowIpc: topic ", topic.id, " not found"));
  }

  // Parse schema from IPC stream
  auto schema_or = arrow_import::schemaFromIpc(ipc_stream);
  if (!schema_or.has_value()) {
    return PJ::unexpected(absl::StrCat("appendArrowIpc: ", schema_or.error()));
  }
  auto& [type_tree, raw_mappings] = *schema_or;

  // Find timestamp column index and build field mappings for non-timestamp columns
  int ts_arrow_col = -1;
  std::vector<arrow_import::ArrowColumnMapping> data_mappings;

  for (const auto& m : raw_mappings) {
    if (m.field_name == timestamp_column) {
      ts_arrow_col = m.arrow_column_index;
      continue;
    }

    // Determine FieldType from the storage kind the importer will actually produce.
    // storageKindOf(kInt8) = kInt64 while toFieldTypeWidened(kInt8) = kInt32 — the
    // column must match the importer's materialized storage, not the widened display type.
    const auto sk = storageKindOf(m.pj_type);
    FieldType ft = FieldType::kFloat64;
    switch (sk) {
      case StorageKind::kFloat32: ft = FieldType::kFloat32; break;
      case StorageKind::kFloat64: ft = FieldType::kFloat64; break;
      case StorageKind::kInt32: ft = FieldType::kInt32; break;
      case StorageKind::kInt64: ft = FieldType::kInt64; break;
      case StorageKind::kUint64: ft = FieldType::kUint64; break;
      case StorageKind::kBool: ft = FieldType::kBool; break;
      case StorageKind::kString: ft = FieldType::kString; break;
    }

    auto field_or = ensureField(topic, m.field_name, ft);
    if (!field_or.has_value()) {
      return PJ::unexpected(field_or.error());
    }

    // Keep original pj_type so the importer uses its widening path for narrow types
    arrow_import::ArrowColumnMapping adjusted = m;
    adjusted.pj_column_index = static_cast<std::size_t>(field_or->id);
    data_mappings.push_back(std::move(adjusted));
  }

  if (ts_arrow_col < 0) {
    return PJ::unexpected(
        absl::StrCat("appendArrowIpc: timestamp column '", timestamp_column, "' not found in IPC schema"));
  }

  // Import using existing arrow_import machinery
  auto status = arrow_import::importIpcStream(impl_->writer_, topic.id, ipc_stream, data_mappings, ts_arrow_col);
  if (!status.has_value()) {
    return PJ::unexpected(status.error());
  }

  return PJ::okStatus();
}

// ---------------------------------------------------------------------------
// Host-side flush
// ---------------------------------------------------------------------------

void PluginHostWrite::flush() {
  auto flushed = impl_->writer_.flushAll();
  if (!flushed.empty()) {
    impl_->engine_.commitChunks(std::move(flushed));
  }
}

}  // namespace PJ
