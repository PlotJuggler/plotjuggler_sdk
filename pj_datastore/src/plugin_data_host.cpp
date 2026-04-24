#include "pj_datastore/plugin_data_host.hpp"

#include <fmt/format.h>
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/dataset.hpp"
#include "pj_base/plugin_data_api.h"
#include "pj_base/type_tree.hpp"
#include "pj_datastore/arrow_import.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/encoding.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {
namespace {

using DataSourceHandle = PJ_data_source_handle_t;
using TopicHandle = PJ_topic_handle_t;
using FieldHandle = PJ_field_handle_t;

[[nodiscard]] std::string_view toStringView(PJ_string_view_t view) {
  return std::string_view(view.data == nullptr ? "" : view.data, view.size);
}

[[nodiscard]] Span<const uint8_t> toSpan(PJ_bytes_view_t view) {
  return Span<const uint8_t>(view.data, view.size);
}

[[nodiscard]] Expected<PrimitiveType> fromAbiType(PJ_primitive_type_t type) {
  const auto raw = static_cast<uint32_t>(type);
  if (raw > static_cast<uint32_t>(PrimitiveType::kString)) {
    return unexpected(fmt::format("unsupported primitive type value {}", raw));
  }
  return static_cast<PrimitiveType>(type);
}

template <typename T>
[[nodiscard]] T loadFromBytes(const uint8_t* data) {
  T value{};
  std::memcpy(&value, data, sizeof(T));
  return value;
}

[[nodiscard]] uint64_t readForOffset(const encoding::FrameOfReferenceEncoded& enc, std::size_t row) {
  const uint8_t* data = enc.offsets.data();
  switch (enc.offset_bytes) {
    case 1:
      return loadFromBytes<uint8_t>(data + row);
    case 2:
      return loadFromBytes<uint16_t>(data + row * 2);
    default:
      return loadFromBytes<uint32_t>(data + row * 4);
  }
}

template <typename T>
[[nodiscard]] T decodeNumericExact(const TopicChunk& chunk, std::size_t col_index, std::size_t row) {
  switch (chunk.columnEncoding(col_index)) {
    case EncodingType::kConstant: {
      const auto& enc = std::get<encoding::ConstantEncoded>(chunk.columns[col_index].data);
      return loadFromBytes<T>(enc.value_bytes.data());
    }
    case EncodingType::kFrameOfReference: {
      const auto& enc = std::get<encoding::FrameOfReferenceEncoded>(chunk.columns[col_index].data);
      const uint64_t offset = readForOffset(enc, row);
      return static_cast<T>(enc.reference + static_cast<int64_t>(offset));
    }
    case EncodingType::kRaw: {
      const StorageKind kind = storageKindOf(chunk.columns[col_index].descriptor->logical_type);
      const uint8_t* base = std::get<RawBuffer>(chunk.columns[col_index].data).data();
      switch (kind) {
        case StorageKind::kFloat32:
          return static_cast<T>(loadFromBytes<float>(base + row * sizeof(float)));
        case StorageKind::kFloat64:
          return static_cast<T>(loadFromBytes<double>(base + row * sizeof(double)));
        case StorageKind::kInt32:
          return static_cast<T>(loadFromBytes<int32_t>(base + row * sizeof(int32_t)));
        case StorageKind::kInt64:
          return static_cast<T>(loadFromBytes<int64_t>(base + row * sizeof(int64_t)));
        case StorageKind::kUint64:
          return static_cast<T>(loadFromBytes<uint64_t>(base + row * sizeof(uint64_t)));
        case StorageKind::kBool:
          return static_cast<T>(chunk.readBool(col_index, row));
        case StorageKind::kString:
          return T{};
      }
      return T{};
    }
    default:
      return T{};
  }
}

void flattenColumnsImpl(
    const TypeTreeNode& node, std::string_view prefix, FieldId& next_id, std::vector<ColumnDescriptor>& out) {
  std::string path = prefix.empty() ? node.name : fmt::format("{}.{}", prefix, node.name);
  switch (node.kind) {
    case TypeKind::kPrimitive: {
      ColumnDescriptor desc;
      desc.field_id = next_id++;
      desc.logical_type = node.primitive_type.value_or(PrimitiveType::kFloat64);
      desc.field_path = std::move(path);
      out.push_back(std::move(desc));
      return;
    }
    case TypeKind::kEnum: {
      ColumnDescriptor desc;
      desc.field_id = next_id++;
      desc.logical_type = node.primitive_type.value_or(PrimitiveType::kInt32);
      desc.field_path = std::move(path);
      out.push_back(std::move(desc));
      return;
    }
    case TypeKind::kStruct:
      for (const auto& child : node.children) {
        flattenColumnsImpl(*child, path, next_id, out);
      }
      return;
    case TypeKind::kArray:
      return;
  }
}

[[nodiscard]] std::vector<ColumnDescriptor> buildSchemaColumns(const TypeTreeNode& root) {
  std::vector<ColumnDescriptor> result;
  FieldId next_id = 0;
  if (root.kind == TypeKind::kStruct) {
    for (const auto& child : root.children) {
      flattenColumnsImpl(*child, "", next_id, result);
    }
  } else {
    flattenColumnsImpl(root, "", next_id, result);
  }
  return result;
}

[[nodiscard]] std::vector<ColumnDescriptor> effectiveColumns(const DataEngine& engine, const TopicStorage& storage) {
  const auto& stored = storage.columnDescriptors();
  if (!stored.empty()) {
    return stored;
  }
  if (const auto* type_tree = engine.typeRegistry().lookup(storage.descriptor().schema_id)) {
    return buildSchemaColumns(*type_tree);
  }
  const auto& chunks = storage.sealedChunks();
  if (!chunks.empty()) {
    std::vector<ColumnDescriptor> result;
    result.reserve(chunks.front().columns.size());
    for (const auto& col : chunks.front().columns) {
      result.push_back(*col.descriptor);
    }
    return result;
  }
  return {};
}

[[nodiscard]] const ColumnDescriptor* findFieldDescriptor(
    const std::vector<ColumnDescriptor>& columns, FieldId field_id) {
  for (const auto& col : columns) {
    if (col.field_id == field_id) {
      return &col;
    }
  }
  return nullptr;
}

}  // namespace

struct WriteCore {
  explicit WriteCore(DataEngine& engine) : engine_(engine), writer_(engine.createWriter()) {}

  DataEngine& engine_;
  DataWriter writer_;
  std::string last_error_;

  struct DatasetTopicKey {
    DatasetId dataset_id;
    std::string topic_name;

    friend bool operator==(const DatasetTopicKey& a, const DatasetTopicKey& b) {
      return a.dataset_id == b.dataset_id && a.topic_name == b.topic_name;
    }
  };

  struct DatasetTopicKeyHash {
    std::size_t operator()(const DatasetTopicKey& key) const noexcept {
      std::size_t h1 = std::hash<DatasetId>{}(key.dataset_id);
      std::size_t h2 = std::hash<std::string>{}(key.topic_name);
      return h1 ^ (h2 << 1);
    }
  };

  struct TopicFieldKey {
    TopicId topic_id;
    std::string field_name;

    friend bool operator==(const TopicFieldKey& a, const TopicFieldKey& b) {
      return a.topic_id == b.topic_id && a.field_name == b.field_name;
    }
  };

  struct TopicFieldKeyHash {
    std::size_t operator()(const TopicFieldKey& key) const noexcept {
      std::size_t h1 = std::hash<TopicId>{}(key.topic_id);
      std::size_t h2 = std::hash<std::string>{}(key.field_name);
      return h1 ^ (h2 << 1);
    }
  };

  struct TopicFieldIdKey {
    TopicId topic_id;
    FieldId field_id;

    friend bool operator==(const TopicFieldIdKey& a, const TopicFieldIdKey& b) {
      return a.topic_id == b.topic_id && a.field_id == b.field_id;
    }
  };

  struct TopicFieldIdKeyHash {
    std::size_t operator()(const TopicFieldIdKey& key) const noexcept {
      std::size_t h1 = std::hash<TopicId>{}(key.topic_id);
      std::size_t h2 = std::hash<FieldId>{}(key.field_id);
      return h1 ^ (h2 << 1);
    }
  };

  tsl::robin_map<DatasetTopicKey, TopicHandle, DatasetTopicKeyHash> topic_cache_;
  tsl::robin_map<TopicFieldKey, FieldHandle, TopicFieldKeyHash> field_cache_;
  tsl::robin_map<TopicFieldIdKey, PrimitiveType, TopicFieldIdKeyHash> field_types_;

  void setError(std::string message) {
    last_error_ = std::move(message);
  }

  [[nodiscard]] const char* lastError() const {
    return last_error_.empty() ? nullptr : last_error_.c_str();
  }

  [[nodiscard]] bool createDataSource(std::string_view name, DataSourceHandle* out_source) {
    auto id_or = engine_.createDataset(DatasetDescriptor{.source_name = std::string(name), .time_domain_id = 0});
    if (!id_or.has_value()) {
      setError(id_or.error());
      return false;
    }
    *out_source = DataSourceHandle{.id = *id_or};
    last_error_.clear();
    return true;
  }

  [[nodiscard]] bool ensureTopic(DataSourceHandle source, std::string_view topic_name, TopicHandle* out_topic) {
    const auto* dataset = engine_.getDataset(source.id);
    if (dataset == nullptr) {
      setError(fmt::format("data source {} not found", source.id));
      return false;
    }

    DatasetTopicKey key{.dataset_id = source.id, .topic_name = std::string(topic_name)};
    if (auto it = topic_cache_.find(key); it != topic_cache_.end()) {
      *out_topic = it->second;
      last_error_.clear();
      return true;
    }

    auto topic_ids = engine_.listTopics(source.id);
    std::sort(topic_ids.begin(), topic_ids.end());
    for (TopicId tid : topic_ids) {
      const auto* storage = engine_.getTopicStorage(tid);
      if (storage != nullptr && storage->descriptor().name == topic_name) {
        *out_topic = TopicHandle{.id = tid};
        topic_cache_.emplace(std::move(key), *out_topic);
        last_error_.clear();
        return true;
      }
    }

    TopicDescriptor desc;
    desc.name = std::string(topic_name);
    desc.schema_id = 0;
    auto tid_or = writer_.registerTopic(source.id, std::move(desc));
    if (!tid_or.has_value()) {
      setError(tid_or.error());
      return false;
    }

    *out_topic = TopicHandle{.id = *tid_or};
    topic_cache_.emplace(std::move(key), *out_topic);
    last_error_.clear();
    return true;
  }

  [[nodiscard]] bool lookupFieldType(TopicHandle topic, FieldId field_id, PrimitiveType* out_type) {
    const TopicFieldIdKey key{.topic_id = topic.id, .field_id = field_id};
    if (auto it = field_types_.find(key); it != field_types_.end()) {
      *out_type = it->second;
      return true;
    }

    const auto* storage = engine_.getTopicStorage(topic.id);
    if (storage == nullptr) {
      setError(fmt::format("topic {} not found", topic.id));
      return false;
    }
    const auto columns = effectiveColumns(engine_, *storage);
    const auto* desc = findFieldDescriptor(columns, field_id);
    if (desc == nullptr) {
      setError(fmt::format("field {} not found in topic {}", field_id, topic.id));
      return false;
    }

    *out_type = desc->logical_type;
    field_types_[key] = desc->logical_type;
    field_cache_[{.topic_id = topic.id, .field_name = desc->field_path}] = FieldHandle{.topic = topic, .id = field_id};
    return true;
  }

  [[nodiscard]] bool ensureField(
      TopicHandle topic, std::string_view field_name, PJ_primitive_type_t abi_type, FieldHandle* out_field) {
    const auto* storage = engine_.getTopicStorage(topic.id);
    if (storage == nullptr) {
      setError(fmt::format("topic {} not found", topic.id));
      return false;
    }

    auto type_or = fromAbiType(abi_type);
    if (!type_or.has_value()) {
      setError(type_or.error());
      return false;
    }
    const PrimitiveType type = *type_or;

    TopicFieldKey key{.topic_id = topic.id, .field_name = std::string(field_name)};
    if (auto it = field_cache_.find(key); it != field_cache_.end()) {
      PrimitiveType existing{};
      if (!lookupFieldType(topic, it->second.id, &existing)) {
        return false;
      }
      if (existing != type) {
        setError(fmt::format("field '{}' already exists with a different type", field_name));
        return false;
      }
      *out_field = it->second;
      last_error_.clear();
      return true;
    }

    auto field_id_or = writer_.ensureColumn(topic.id, field_name, type);
    if (!field_id_or.has_value()) {
      setError(field_id_or.error());
      return false;
    }

    *out_field = FieldHandle{.topic = topic, .id = *field_id_or};
    field_cache_.emplace(std::move(key), *out_field);
    field_types_[{.topic_id = topic.id, .field_id = *field_id_or}] = type;
    last_error_.clear();
    return true;
  }

  [[nodiscard]] bool validateScalar(const PJ_scalar_value_t& value, PrimitiveType expected, std::string_view where) {
    auto actual_or = fromAbiType(value.type);
    if (!actual_or.has_value()) {
      setError(actual_or.error());
      return false;
    }
    if (*actual_or != expected) {
      setError(fmt::format("{}: scalar type mismatch", where));
      return false;
    }
    return true;
  }

  void setFieldValue(
      TopicId topic_id, std::size_t col_index, PrimitiveType logical_type, const PJ_scalar_value_t& value) {
    switch (logical_type) {
      case PrimitiveType::kFloat32:
        writer_.set(topic_id, col_index, value.data.as_float32);
        break;
      case PrimitiveType::kFloat64:
        writer_.set(topic_id, col_index, value.data.as_float64);
        break;
      case PrimitiveType::kInt8:
        writer_.set(topic_id, col_index, static_cast<int64_t>(value.data.as_int8));
        break;
      case PrimitiveType::kInt16:
        writer_.set(topic_id, col_index, static_cast<int64_t>(value.data.as_int16));
        break;
      case PrimitiveType::kInt32:
        writer_.set(topic_id, col_index, value.data.as_int32);
        break;
      case PrimitiveType::kInt64:
        writer_.set(topic_id, col_index, value.data.as_int64);
        break;
      case PrimitiveType::kUint8:
        writer_.set(topic_id, col_index, static_cast<uint64_t>(value.data.as_uint8));
        break;
      case PrimitiveType::kUint16:
        writer_.set(topic_id, col_index, static_cast<uint64_t>(value.data.as_uint16));
        break;
      case PrimitiveType::kUint32:
        writer_.set(topic_id, col_index, static_cast<uint64_t>(value.data.as_uint32));
        break;
      case PrimitiveType::kUint64:
        writer_.set(topic_id, col_index, value.data.as_uint64);
        break;
      case PrimitiveType::kBool:
        writer_.set(topic_id, col_index, value.data.as_bool != 0);
        break;
      case PrimitiveType::kString:
        writer_.set(topic_id, col_index, toStringView(value.data.as_string));
        break;
      case PrimitiveType::kUnspecified:
        break;
    }
  }

  [[nodiscard]] bool appendRecord(
      TopicHandle topic, Timestamp timestamp, const PJ_named_field_value_t* fields, std::size_t field_count) {
    if (engine_.getTopicStorage(topic.id) == nullptr) {
      setError(fmt::format("topic {} not found", topic.id));
      return false;
    }

    tsl::robin_set<std::string_view> seen_names;
    struct ResolvedField {
      FieldHandle handle;
      PrimitiveType type;
      const PJ_named_field_value_t* raw;
    };
    std::vector<ResolvedField> resolved;
    resolved.reserve(field_count);
    for (std::size_t i = 0; i < field_count; ++i) {
      const auto& field = fields[i];
      const auto name = toStringView(field.name);
      if (!seen_names.insert(name).second) {
        setError(fmt::format("duplicate field name '{}'", name));
        return false;
      }
      if (field.is_null) {
        // Null values: look up existing field by name.
        TopicFieldKey key{.topic_id = topic.id, .field_name = std::string(name)};
        auto it = field_cache_.find(key);
        if (it == field_cache_.end()) {
          // Field has never been seen. Check if this is a typed null (the ABI
          // carries value.type even when is_null is true). A valid type lets
          // us create the column now; an untyped null (kNull) is silently
          // skipped — the column will be created when a non-null value arrives.
          auto type_or = fromAbiType(field.value.type);
          if (type_or.has_value()) {
            FieldHandle handle{};
            if (!ensureField(topic, name, field.value.type, &handle)) {
              return false;
            }
            resolved.push_back({handle, *type_or, &field});
          }
          continue;
        }
        PrimitiveType existing{};
        if (!lookupFieldType(topic, it->second.id, &existing)) {
          return false;
        }
        resolved.push_back({it->second, existing, &field});
      } else {
        auto type_or = fromAbiType(field.value.type);
        if (!type_or.has_value()) {
          setError(type_or.error());
          return false;
        }
        FieldHandle handle{};
        if (!ensureField(topic, name, field.value.type, &handle)) {
          return false;
        }
        if (!validateScalar(field.value, *type_or, "appendRecord")) {
          return false;
        }
        resolved.push_back({handle, *type_or, &field});
      }
    }

    auto begin_status = writer_.beginRow(topic.id, timestamp);
    if (!begin_status.has_value()) {
      setError(begin_status.error());
      return false;
    }
    for (const auto& field : resolved) {
      if (field.raw->is_null) {
        writer_.setNull(topic.id, static_cast<std::size_t>(field.handle.id));
      } else {
        setFieldValue(topic.id, static_cast<std::size_t>(field.handle.id), field.type, field.raw->value);
      }
    }
    auto finish_status = writer_.finishRow(topic.id);
    if (!finish_status.has_value()) {
      setError(finish_status.error());
      return false;
    }
    last_error_.clear();
    return true;
  }

  [[nodiscard]] bool appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, const PJ_bound_field_value_t* fields, std::size_t field_count) {
    if (engine_.getTopicStorage(topic.id) == nullptr) {
      setError(fmt::format("topic {} not found", topic.id));
      return false;
    }

    tsl::robin_set<FieldId> seen_ids;
    struct ResolvedField {
      PrimitiveType type;
      const PJ_bound_field_value_t* raw;
    };
    std::vector<ResolvedField> resolved;
    resolved.reserve(field_count);
    for (std::size_t i = 0; i < field_count; ++i) {
      const auto& field = fields[i];
      if (field.field.topic.id != topic.id) {
        setError("field handle does not belong to the target topic");
        return false;
      }
      if (!seen_ids.insert(field.field.id).second) {
        setError(fmt::format("duplicate field id {}", field.field.id));
        return false;
      }
      PrimitiveType type{};
      if (!lookupFieldType(topic, field.field.id, &type)) {
        return false;
      }
      if (!field.is_null && !validateScalar(field.value, type, "appendBoundRecord")) {
        return false;
      }
      resolved.push_back({type, &field});
    }

    auto begin_status = writer_.beginRow(topic.id, timestamp);
    if (!begin_status.has_value()) {
      setError(begin_status.error());
      return false;
    }
    for (const auto& field : resolved) {
      if (field.raw->is_null) {
        writer_.setNull(topic.id, static_cast<std::size_t>(field.raw->field.id));
      } else {
        setFieldValue(topic.id, static_cast<std::size_t>(field.raw->field.id), field.type, field.raw->value);
      }
    }
    auto finish_status = writer_.finishRow(topic.id);
    if (!finish_status.has_value()) {
      setError(finish_status.error());
      return false;
    }
    last_error_.clear();
    return true;
  }

  [[nodiscard]] bool appendArrowIpc(TopicHandle topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
    if (engine_.getTopicStorage(topic.id) == nullptr) {
      setError(fmt::format("topic {} not found", topic.id));
      return false;
    }

    auto schema_or = arrow_import::schemaFromIpc(toSpan(ipc_stream));
    if (!schema_or.has_value()) {
      setError(schema_or.error());
      return false;
    }

    const std::string_view timestamp_name = toStringView(timestamp_column);
    int ts_arrow_col = -1;
    std::vector<arrow_import::ArrowColumnMapping> mappings;
    for (const auto& mapping : schema_or->second) {
      if (mapping.field_name == timestamp_name) {
        ts_arrow_col = mapping.arrow_column_index;
        continue;
      }

      FieldHandle field{};
      if (!ensureField(topic, mapping.field_name, static_cast<PJ_primitive_type_t>(mapping.pj_type), &field)) {
        return false;
      }
      auto adjusted = mapping;
      adjusted.pj_column_index = static_cast<std::size_t>(field.id);
      mappings.push_back(std::move(adjusted));
    }

    if (ts_arrow_col < 0) {
      setError(fmt::format("timestamp column '{}' not found in IPC schema", timestamp_name));
      return false;
    }

    auto status = arrow_import::importIpcStream(writer_, topic.id, toSpan(ipc_stream), mappings, ts_arrow_col);
    if (!status.has_value()) {
      setError(status.error());
      return false;
    }
    last_error_.clear();
    return true;
  }

  void flushPending() {
    auto flushed = writer_.flushAll();
    if (!flushed.empty()) {
      engine_.commitChunks(std::move(flushed));
    }
  }
};

struct CatalogSnapshotState {
  std::deque<std::string> names;
  std::vector<PJ_data_source_info_t> data_sources;
  std::vector<PJ_topic_info_t> topics;
  std::vector<PJ_field_info_t> fields;
};

struct MaterializedSeriesState {
  std::vector<Timestamp> timestamps;
  std::vector<uint8_t> validity_bits;
  std::vector<float> float32_values;
  std::vector<double> float64_values;
  std::vector<int8_t> int8_values;
  std::vector<int16_t> int16_values;
  std::vector<int32_t> int32_values;
  std::vector<int64_t> int64_values;
  std::vector<uint8_t> uint8_values;
  std::vector<uint16_t> uint16_values;
  std::vector<uint32_t> uint32_values;
  std::vector<uint64_t> uint64_values;
  std::vector<uint8_t> bool_values;
  std::vector<uint32_t> string_offsets;
  std::vector<char> string_bytes;
};

void releaseCatalogSnapshot(void* ctx) {
  delete static_cast<CatalogSnapshotState*>(ctx);
}

void releaseMaterializedSeries(void* ctx) {
  delete static_cast<MaterializedSeriesState*>(ctx);
}

PJ_string_view_t storeString(CatalogSnapshotState& state, std::string_view value) {
  state.names.emplace_back(value);
  const auto& stored = state.names.back();
  return PJ_string_view_t{stored.data(), stored.size()};
}

struct ToolboxCore {
  explicit ToolboxCore(DataEngine& engine) : write(engine), engine_(engine) {}

  WriteCore write;
  DataEngine& engine_;

  [[nodiscard]] bool acquireCatalogSnapshot(PJ_catalog_snapshot_t* out_snapshot) {
    auto* state = new CatalogSnapshotState{};
    auto dataset_ids = engine_.listDatasets();
    std::sort(dataset_ids.begin(), dataset_ids.end());
    state->data_sources.reserve(dataset_ids.size());

    for (DatasetId ds_id : dataset_ids) {
      const auto* dataset = engine_.getDataset(ds_id);
      if (dataset == nullptr) {
        continue;
      }

      const uint32_t first_topic = static_cast<uint32_t>(state->topics.size());
      auto topic_ids = engine_.listTopics(ds_id);
      std::sort(topic_ids.begin(), topic_ids.end());
      for (TopicId tid : topic_ids) {
        const auto* storage = engine_.getTopicStorage(tid);
        if (storage == nullptr) {
          continue;
        }
        const uint32_t first_field = static_cast<uint32_t>(state->fields.size());
        const auto columns = effectiveColumns(engine_, *storage);
        for (const auto& col : columns) {
          state->fields.push_back(
              PJ_field_info_t{
                  .handle = FieldHandle{.topic = TopicHandle{.id = tid}, .id = col.field_id},
                  .name = storeString(*state, col.field_path),
                  .type = static_cast<PJ_primitive_type_t>(col.logical_type),
              });
        }
        state->topics.push_back(
            PJ_topic_info_t{
                .handle = TopicHandle{.id = tid},
                .source = DataSourceHandle{.id = ds_id},
                .name = storeString(*state, storage->descriptor().name),
                .first_field = first_field,
                .field_count = static_cast<uint32_t>(state->fields.size()) - first_field,
            });
      }

      state->data_sources.push_back(
          PJ_data_source_info_t{
              .handle = DataSourceHandle{.id = ds_id},
              .name = storeString(*state, dataset->source_name),
              .first_topic = first_topic,
              .topic_count = static_cast<uint32_t>(state->topics.size()) - first_topic,
          });
    }

    *out_snapshot = PJ_catalog_snapshot_t{
        .data_sources = state->data_sources.data(),
        .data_source_count = state->data_sources.size(),
        .topics = state->topics.data(),
        .topic_count = state->topics.size(),
        .fields = state->fields.data(),
        .field_count = state->fields.size(),
        .release_ctx = state,
        .release = releaseCatalogSnapshot,
    };
    write.last_error_.clear();
    return true;
  }

  [[nodiscard]] bool readSeries(FieldHandle field, PJ_materialized_series_t* out_series) {
    const auto* storage = engine_.getTopicStorage(field.topic.id);
    if (storage == nullptr) {
      write.setError(fmt::format("topic {} not found", field.topic.id));
      return false;
    }
    const auto columns = effectiveColumns(engine_, *storage);
    const auto* desc = findFieldDescriptor(columns, field.id);
    if (desc == nullptr) {
      write.setError(fmt::format("field {} not found in topic {}", field.id, field.topic.id));
      return false;
    }

    auto* state = new MaterializedSeriesState{};
    const auto& chunks = storage->sealedChunks();
    std::size_t total_rows = 0;
    for (const auto& chunk : chunks) {
      for (const auto& col : chunk.columns) {
        if (col.descriptor->field_id == field.id) {
          total_rows += chunk.stats.row_count;
          break;
        }
      }
    }

    state->timestamps.reserve(total_rows);
    state->validity_bits.assign((total_rows + 7) / 8, 0xFF);

    auto mark_null = [&](std::size_t row_index) {
      state->validity_bits[row_index / 8] &= static_cast<uint8_t>(~(1U << (row_index % 8)));
    };

    std::size_t row_index = 0;
    switch (desc->logical_type) {
      case PrimitiveType::kFloat32:
        state->float32_values.reserve(total_rows);
        break;
      case PrimitiveType::kFloat64:
        state->float64_values.reserve(total_rows);
        break;
      case PrimitiveType::kInt8:
        state->int8_values.reserve(total_rows);
        break;
      case PrimitiveType::kInt16:
        state->int16_values.reserve(total_rows);
        break;
      case PrimitiveType::kInt32:
        state->int32_values.reserve(total_rows);
        break;
      case PrimitiveType::kInt64:
        state->int64_values.reserve(total_rows);
        break;
      case PrimitiveType::kUint8:
        state->uint8_values.reserve(total_rows);
        break;
      case PrimitiveType::kUint16:
        state->uint16_values.reserve(total_rows);
        break;
      case PrimitiveType::kUint32:
        state->uint32_values.reserve(total_rows);
        break;
      case PrimitiveType::kUint64:
        state->uint64_values.reserve(total_rows);
        break;
      case PrimitiveType::kBool:
        state->bool_values.reserve(total_rows);
        break;
      case PrimitiveType::kString:
        state->string_offsets.push_back(0);
        break;
      case PrimitiveType::kUnspecified:
        break;
    }

    for (const auto& chunk : chunks) {
      int col_index = -1;
      for (std::size_t i = 0; i < chunk.columns.size(); ++i) {
        if (chunk.columns[i].descriptor->field_id == field.id) {
          col_index = static_cast<int>(i);
          break;
        }
      }
      if (col_index < 0) {
        continue;
      }
      for (uint32_t row = 0; row < chunk.stats.row_count; ++row) {
        state->timestamps.push_back(chunk.readTimestamp(row));
        const bool is_null = chunk.isNull(static_cast<std::size_t>(col_index), row);
        if (is_null) {
          mark_null(row_index);
        }
        switch (desc->logical_type) {
          case PrimitiveType::kFloat32:
            state->float32_values.push_back(
                is_null ? 0.0F : decodeNumericExact<float>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kFloat64:
            state->float64_values.push_back(
                is_null ? 0.0 : decodeNumericExact<double>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kInt8:
            state->int8_values.push_back(
                is_null ? 0 : decodeNumericExact<int8_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kInt16:
            state->int16_values.push_back(
                is_null ? 0 : decodeNumericExact<int16_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kInt32:
            state->int32_values.push_back(
                is_null ? 0 : decodeNumericExact<int32_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kInt64:
            state->int64_values.push_back(
                is_null ? 0 : decodeNumericExact<int64_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kUint8:
            state->uint8_values.push_back(
                is_null ? 0 : decodeNumericExact<uint8_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kUint16:
            state->uint16_values.push_back(
                is_null ? 0 : decodeNumericExact<uint16_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kUint32:
            state->uint32_values.push_back(
                is_null ? 0 : decodeNumericExact<uint32_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kUint64:
            state->uint64_values.push_back(
                is_null ? 0 : decodeNumericExact<uint64_t>(chunk, static_cast<std::size_t>(col_index), row));
            break;
          case PrimitiveType::kBool:
            state->bool_values.push_back(
                is_null ? 0 : static_cast<uint8_t>(chunk.readBool(static_cast<std::size_t>(col_index), row)));
            break;
          case PrimitiveType::kString: {
            if (!is_null) {
              const auto text = chunk.readString(static_cast<std::size_t>(col_index), row);
              state->string_bytes.insert(state->string_bytes.end(), text.begin(), text.end());
            }
            state->string_offsets.push_back(static_cast<uint32_t>(state->string_bytes.size()));
            break;
          }
          case PrimitiveType::kUnspecified:
            break;
        }
        ++row_index;
      }
    }

    *out_series = PJ_materialized_series_t{
        .source = DataSourceHandle{.id = storage->descriptor().dataset_id},
        .topic = field.topic,
        .field = field,
        .type = static_cast<PJ_primitive_type_t>(desc->logical_type),
        .timestamps = state->timestamps.data(),
        .row_count = state->timestamps.size(),
        .validity_bits = state->validity_bits.data(),
        .validity_size = state->validity_bits.size(),
        .values = {},
        .release_ctx = state,
        .release = releaseMaterializedSeries,
    };

    switch (desc->logical_type) {
      case PrimitiveType::kFloat32:
        out_series->values.as_float32 = state->float32_values.data();
        break;
      case PrimitiveType::kFloat64:
        out_series->values.as_float64 = state->float64_values.data();
        break;
      case PrimitiveType::kInt8:
        out_series->values.as_int8 = state->int8_values.data();
        break;
      case PrimitiveType::kInt16:
        out_series->values.as_int16 = state->int16_values.data();
        break;
      case PrimitiveType::kInt32:
        out_series->values.as_int32 = state->int32_values.data();
        break;
      case PrimitiveType::kInt64:
        out_series->values.as_int64 = state->int64_values.data();
        break;
      case PrimitiveType::kUint8:
        out_series->values.as_uint8 = state->uint8_values.data();
        break;
      case PrimitiveType::kUint16:
        out_series->values.as_uint16 = state->uint16_values.data();
        break;
      case PrimitiveType::kUint32:
        out_series->values.as_uint32 = state->uint32_values.data();
        break;
      case PrimitiveType::kUint64:
        out_series->values.as_uint64 = state->uint64_values.data();
        break;
      case PrimitiveType::kBool:
        out_series->values.as_bool = state->bool_values.data();
        break;
      case PrimitiveType::kString:
        out_series->values.as_string = PJ_string_series_values_t{
            .offsets = state->string_offsets.data(),
            .offset_count = state->string_offsets.size(),
            .bytes = state->string_bytes.data(),
            .byte_count = state->string_bytes.size(),
        };
        break;
      case PrimitiveType::kUnspecified:
        break;
    }
    write.last_error_.clear();
    return true;
  }
};

struct DatastoreSourceWriteHostState {
  DatastoreSourceWriteHostState(DataEngine& engine, DataSourceHandle source_handle)
      : core(engine), source(source_handle) {}
  WriteCore core;
  DataSourceHandle source;
};

struct DatastoreParserWriteHostState {
  DatastoreParserWriteHostState(DataEngine& engine, TopicHandle topic_handle) : core(engine), topic(topic_handle) {}
  WriteCore core;
  TopicHandle topic;
};

struct DatastoreToolboxHostState {
  explicit DatastoreToolboxHostState(DataEngine& engine) : core(engine) {}
  ToolboxCore core;
};

bool sourceEnsureTopic(void* ctx, PJ_string_view_t topic_name, TopicHandle* out_topic) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.ensureTopic(
      static_cast<DatastoreSourceWriteHostState*>(ctx)->source, toStringView(topic_name), out_topic);
}

bool sourceEnsureField(
    void* ctx, TopicHandle topic, PJ_string_view_t field_name, PJ_primitive_type_t type, FieldHandle* out_field) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.ensureField(
      topic, toStringView(field_name), type, out_field);
}

bool sourceAppendRecord(
    void* ctx, TopicHandle topic, int64_t timestamp, const PJ_named_field_value_t* fields, std::size_t field_count) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.appendRecord(topic, timestamp, fields, field_count);
}

bool sourceAppendRecordFast(
    void* ctx, TopicHandle topic, int64_t timestamp, const PJ_bound_field_value_t* fields, std::size_t field_count) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.appendBoundRecord(
      topic, timestamp, fields, field_count);
}

bool sourceAppendArrowIpc(void* ctx, TopicHandle topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.appendArrowIpc(topic, ipc_stream, timestamp_column);
}

const char* sourceLastError(void* ctx) {
  return static_cast<DatastoreSourceWriteHostState*>(ctx)->core.lastError();
}

bool parserEnsureField(void* ctx, PJ_string_view_t field_name, PJ_primitive_type_t type, FieldHandle* out_field) {
  auto* impl = static_cast<DatastoreParserWriteHostState*>(ctx);
  return impl->core.ensureField(impl->topic, toStringView(field_name), type, out_field);
}

bool parserAppendRecord(void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, std::size_t field_count) {
  auto* impl = static_cast<DatastoreParserWriteHostState*>(ctx);
  return impl->core.appendRecord(impl->topic, timestamp, fields, field_count);
}

bool parserAppendRecordFast(
    void* ctx, int64_t timestamp, const PJ_bound_field_value_t* fields, std::size_t field_count) {
  auto* impl = static_cast<DatastoreParserWriteHostState*>(ctx);
  return impl->core.appendBoundRecord(impl->topic, timestamp, fields, field_count);
}

bool parserAppendArrowIpc(void* ctx, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
  auto* impl = static_cast<DatastoreParserWriteHostState*>(ctx);
  return impl->core.appendArrowIpc(impl->topic, ipc_stream, timestamp_column);
}

const char* parserLastError(void* ctx) {
  return static_cast<DatastoreParserWriteHostState*>(ctx)->core.lastError();
}

bool toolboxCreateDataSource(void* ctx, PJ_string_view_t name, DataSourceHandle* out_source) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.createDataSource(toStringView(name), out_source);
}

bool toolboxEnsureTopic(void* ctx, DataSourceHandle source, PJ_string_view_t topic_name, TopicHandle* out_topic) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.ensureTopic(
      source, toStringView(topic_name), out_topic);
}

bool toolboxEnsureField(
    void* ctx, TopicHandle topic, PJ_string_view_t field_name, PJ_primitive_type_t type, FieldHandle* out_field) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.ensureField(
      topic, toStringView(field_name), type, out_field);
}

bool toolboxAppendRecord(
    void* ctx, TopicHandle topic, int64_t timestamp, const PJ_named_field_value_t* fields, std::size_t field_count) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.appendRecord(topic, timestamp, fields, field_count);
}

bool toolboxAppendRecordFast(
    void* ctx, TopicHandle topic, int64_t timestamp, const PJ_bound_field_value_t* fields, std::size_t field_count) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.appendBoundRecord(
      topic, timestamp, fields, field_count);
}

bool toolboxAppendArrowIpc(
    void* ctx, TopicHandle topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.appendArrowIpc(topic, ipc_stream, timestamp_column);
}

bool toolboxAcquireCatalogSnapshot(void* ctx, PJ_catalog_snapshot_t* out_snapshot) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.acquireCatalogSnapshot(out_snapshot);
}

bool toolboxReadSeries(void* ctx, FieldHandle field, PJ_materialized_series_t* out_series) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.readSeries(field, out_series);
}

const char* toolboxLastError(void* ctx) {
  return static_cast<DatastoreToolboxHostState*>(ctx)->core.write.lastError();
}

const PJ_source_write_host_vtable_t kSourceWriteVTable = {
    PJ_PLUGIN_DATA_API_VERSION,
    sizeof(PJ_source_write_host_vtable_t),
    sourceLastError,
    sourceEnsureTopic,
    sourceEnsureField,
    sourceAppendRecord,
    sourceAppendRecordFast,
    sourceAppendArrowIpc,
};

const PJ_parser_write_host_vtable_t kParserWriteVTable = {
    PJ_PLUGIN_DATA_API_VERSION,
    sizeof(PJ_parser_write_host_vtable_t),
    parserLastError,
    parserEnsureField,
    parserAppendRecord,
    parserAppendRecordFast,
    parserAppendArrowIpc,
};

const PJ_toolbox_host_vtable_t kToolboxVTable = {
    PJ_PLUGIN_DATA_API_VERSION, sizeof(PJ_toolbox_host_vtable_t),
    toolboxLastError,           toolboxCreateDataSource,
    toolboxEnsureTopic,         toolboxEnsureField,
    toolboxAppendRecord,        toolboxAppendRecordFast,
    toolboxAppendArrowIpc,      toolboxAcquireCatalogSnapshot,
    toolboxReadSeries,
};

DatastoreSourceWriteHost::DatastoreSourceWriteHost(DataEngine& engine, DataSourceHandle source)
    : state_(std::make_unique<DatastoreSourceWriteHostState>(engine, source)) {}
DatastoreSourceWriteHost::~DatastoreSourceWriteHost() = default;
DatastoreSourceWriteHost::DatastoreSourceWriteHost(DatastoreSourceWriteHost&&) noexcept = default;
DatastoreSourceWriteHost& DatastoreSourceWriteHost::operator=(DatastoreSourceWriteHost&&) noexcept = default;

PJ_source_write_host_t DatastoreSourceWriteHost::raw() noexcept {
  return PJ_source_write_host_t{.ctx = state_.get(), .vtable = &kSourceWriteVTable};
}

void DatastoreSourceWriteHost::flushPending() {
  state_->core.flushPending();
}

DatastoreParserWriteHost::DatastoreParserWriteHost(DataEngine& engine, TopicHandle topic)
    : state_(std::make_unique<DatastoreParserWriteHostState>(engine, topic)) {}
DatastoreParserWriteHost::~DatastoreParserWriteHost() = default;
DatastoreParserWriteHost::DatastoreParserWriteHost(DatastoreParserWriteHost&&) noexcept = default;
DatastoreParserWriteHost& DatastoreParserWriteHost::operator=(DatastoreParserWriteHost&&) noexcept = default;

PJ_parser_write_host_t DatastoreParserWriteHost::raw() noexcept {
  return PJ_parser_write_host_t{.ctx = state_.get(), .vtable = &kParserWriteVTable};
}

void DatastoreParserWriteHost::flushPending() {
  state_->core.flushPending();
}

DatastoreToolboxHost::DatastoreToolboxHost(DataEngine& engine)
    : state_(std::make_unique<DatastoreToolboxHostState>(engine)) {}
DatastoreToolboxHost::~DatastoreToolboxHost() = default;
DatastoreToolboxHost::DatastoreToolboxHost(DatastoreToolboxHost&&) noexcept = default;
DatastoreToolboxHost& DatastoreToolboxHost::operator=(DatastoreToolboxHost&&) noexcept = default;

PJ_toolbox_host_t DatastoreToolboxHost::raw() noexcept {
  return PJ_toolbox_host_t{.ctx = state_.get(), .vtable = &kToolboxVTable};
}

void DatastoreToolboxHost::flushPending() {
  state_->core.write.flushPending();
}

}  // namespace PJ
