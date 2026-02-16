#include "pj/engine/writer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"
#include "pj/engine/type_tree.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

namespace {

/// Map NumericType to PrimitiveType (same enum values, different enum types).
constexpr PrimitiveType numeric_to_primitive(NumericType nt) noexcept {
  switch (nt) {
    case NumericType::kFloat32: return PrimitiveType::kFloat32;
    case NumericType::kFloat64: return PrimitiveType::kFloat64;
    case NumericType::kInt8:    return PrimitiveType::kInt8;
    case NumericType::kInt16:   return PrimitiveType::kInt16;
    case NumericType::kInt32:   return PrimitiveType::kInt32;
    case NumericType::kInt64:   return PrimitiveType::kInt64;
    case NumericType::kUint8:   return PrimitiveType::kUint8;
    case NumericType::kUint16:  return PrimitiveType::kUint16;
    case NumericType::kUint32:  return PrimitiveType::kUint32;
    case NumericType::kUint64:  return PrimitiveType::kUint64;
  }
  return PrimitiveType::kFloat64;  // unreachable
}

/// Recursively flatten a type tree into ColumnDescriptors, collecting both
/// field paths and PrimitiveTypes for each leaf node.
void flatten_columns_impl(const TypeTreeNode& node, std::string_view prefix,
                          FieldId& next_field_id,
                          std::vector<ColumnDescriptor>& out) {
  std::string current_path =
      prefix.empty() ? node.name : absl::StrCat(prefix, ".", node.name);

  if (node.kind == TypeKind::kStruct) {
    for (const auto& child : node.children) {
      flatten_columns_impl(*child, current_path, next_field_id, out);
    }
    return;
  }

  // Leaf node (primitive, enum, array) -- produce a column descriptor
  ColumnDescriptor desc;
  desc.field_id = next_field_id++;
  desc.field_path = std::move(current_path);

  if (node.primitive_type.has_value()) {
    desc.logical_type = *node.primitive_type;
  } else {
    // Default to float64 for types without an explicit primitive
    desc.logical_type = PrimitiveType::kFloat64;
  }

  out.push_back(std::move(desc));
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DataWriter::DataWriter(DataEngine& engine) : engine_(engine) {}

// ---------------------------------------------------------------------------
// Schema registration
// ---------------------------------------------------------------------------

absl::StatusOr<SchemaId> DataWriter::register_schema(
    std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree) {
  return engine_.type_registry().register_schema(std::move(schema_name),
                                                 std::move(type_tree));
}

// ---------------------------------------------------------------------------
// Topic registration
// ---------------------------------------------------------------------------

absl::StatusOr<TopicId> DataWriter::register_topic(
    DatasetId dataset_id, TopicDescriptor descriptor) {
  return engine_.create_topic(dataset_id, std::move(descriptor));
}

// ---------------------------------------------------------------------------
// Bind for fast-path access
// ---------------------------------------------------------------------------

absl::StatusOr<TopicWriteHandle> DataWriter::bind_topic_writer(
    TopicId topic_id) {
  const auto* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("Topic ", topic_id, " not found"));
  }

  // Ensure column descriptors are cached
  auto& builder = get_or_create_builder(topic_id);
  (void)builder;  // we just need the side effect of caching columns

  const auto& columns = topic_columns_.at(topic_id);
  TopicWriteHandle handle;
  handle.topic_id = topic_id;
  handle.field_ids.reserve(columns.size());
  for (const auto& col : columns) {
    handle.field_ids.push_back(col.field_id);
  }
  return handle;
}

// ---------------------------------------------------------------------------
// Field resolution
// ---------------------------------------------------------------------------

absl::StatusOr<FieldId> DataWriter::resolve_field(
    TopicId topic_id, std::string_view field_path) {
  // Ensure columns are cached by getting or creating the builder
  auto& builder = get_or_create_builder(topic_id);
  (void)builder;

  auto col_it = topic_columns_.find(topic_id);
  if (col_it == topic_columns_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Topic ", topic_id, " not found"));
  }

  for (const auto& col : col_it->second) {
    if (col.field_path == field_path) {
      return col.field_id;
    }
  }
  return absl::NotFoundError(
      absl::StrCat("Field '", field_path, "' not found in topic ",
                    topic_id));
}

// ---------------------------------------------------------------------------
// Row-at-a-time append
// ---------------------------------------------------------------------------

void DataWriter::begin_row(TopicId topic_id, Timestamp t) {
  auto& builder = get_or_create_builder(topic_id);
  builder.begin_row(t);
}

void DataWriter::finish_row(TopicId topic_id) {
  auto it = builders_.find(topic_id);
  if (it == builders_.end()) {
    return;  // no builder — nothing to finish
  }
  it->second.finish_row();

  if (it->second.is_full()) {
    auto_seal(topic_id);
  }
}

// ---------------------------------------------------------------------------
// Set values
// ---------------------------------------------------------------------------

void DataWriter::set_float32(TopicId topic_id, std::size_t col_index,
                             float value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_float32(col_index, value);
  }
}

void DataWriter::set_float64(TopicId topic_id, std::size_t col_index,
                             double value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_float64(col_index, value);
  }
}

void DataWriter::set_int8(TopicId topic_id, std::size_t col_index,
                          int8_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_int8(col_index, value);
  }
}

void DataWriter::set_int16(TopicId topic_id, std::size_t col_index,
                           int16_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_int16(col_index, value);
  }
}

void DataWriter::set_int32(TopicId topic_id, std::size_t col_index,
                           int32_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_int32(col_index, value);
  }
}

void DataWriter::set_int64(TopicId topic_id, std::size_t col_index,
                           int64_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_int64(col_index, value);
  }
}

void DataWriter::set_uint8(TopicId topic_id, std::size_t col_index,
                           uint8_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_uint8(col_index, value);
  }
}

void DataWriter::set_uint16(TopicId topic_id, std::size_t col_index,
                            uint16_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_uint16(col_index, value);
  }
}

void DataWriter::set_uint32(TopicId topic_id, std::size_t col_index,
                            uint32_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_uint32(col_index, value);
  }
}

void DataWriter::set_uint64(TopicId topic_id, std::size_t col_index,
                            uint64_t value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_uint64(col_index, value);
  }
}

void DataWriter::set_string(TopicId topic_id, std::size_t col_index,
                            std::string_view value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_string(col_index, value);
  }
}

void DataWriter::set_bool(TopicId topic_id, std::size_t col_index,
                           bool value) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_bool(col_index, value);
  }
}

void DataWriter::set_null(TopicId topic_id, std::size_t col_index) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    it->second.set_null(col_index);
  }
}

// ---------------------------------------------------------------------------
// Scalar convenience API
// ---------------------------------------------------------------------------

absl::StatusOr<ScalarSeriesHandle> DataWriter::register_scalar_series(
    DatasetId dataset_id, std::string_view topic_name,
    NumericType value_type) {
  // Create a topic descriptor for a scalar series (schema_id = 0)
  TopicDescriptor desc;
  desc.name = std::string(topic_name);
  desc.schema_id = 0;

  auto topic_id_or = engine_.create_topic(dataset_id, std::move(desc));
  if (!topic_id_or.ok()) {
    return topic_id_or.status();
  }
  TopicId topic_id = *topic_id_or;

  // Build a single column descriptor for the "value" field
  ColumnDescriptor col_desc;
  col_desc.field_id = 0;
  col_desc.logical_type = numeric_to_primitive(value_type);
  col_desc.field_path = "value";

  std::vector<ColumnDescriptor> columns;
  columns.push_back(std::move(col_desc));
  topic_columns_[topic_id] = std::move(columns);

  ScalarSeriesHandle handle{topic_id, 0};
  return handle;
}

void DataWriter::append_scalar(const ScalarSeriesHandle& handle,
                               Timestamp t, NumericValue value) {
  auto& builder = get_or_create_builder(handle.topic_id);
  builder.begin_row(t);

  std::visit(
      [&builder, col = static_cast<std::size_t>(handle.value_field)](
          const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          builder.set_float32(col, v);
        } else if constexpr (std::is_same_v<T, double>) {
          builder.set_float64(col, v);
        } else if constexpr (std::is_same_v<T, int8_t>) {
          builder.set_int8(col, v);
        } else if constexpr (std::is_same_v<T, int16_t>) {
          builder.set_int16(col, v);
        } else if constexpr (std::is_same_v<T, int32_t>) {
          builder.set_int32(col, v);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          builder.set_int64(col, v);
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          builder.set_uint8(col, v);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          builder.set_uint16(col, v);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          builder.set_uint32(col, v);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          builder.set_uint64(col, v);
        }
      },
      value);

  builder.finish_row();

  if (builder.is_full()) {
    auto_seal(handle.topic_id);
  }
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------

std::vector<TopicChunk> DataWriter::flush(TopicId topic_id) {
  std::vector<TopicChunk> result;

  // Collect any pending (auto-sealed) chunks
  auto pending_it = pending_chunks_.find(topic_id);
  if (pending_it != pending_chunks_.end()) {
    result = std::move(pending_it->second);
    pending_chunks_.erase(pending_it);
  }

  // Seal the current builder if it has rows
  auto builder_it = builders_.find(topic_id);
  if (builder_it != builders_.end() && builder_it->second.row_count() > 0) {
    result.push_back(builder_it->second.seal());
    builders_.erase(builder_it);
  }

  return result;
}

std::vector<std::pair<TopicId, TopicChunk>> DataWriter::flush_all() {
  std::vector<std::pair<TopicId, TopicChunk>> result;

  // Collect all pending chunks
  for (auto& [topic_id, chunks] : pending_chunks_) {
    for (auto& chunk : chunks) {
      result.emplace_back(topic_id, std::move(chunk));
    }
  }
  pending_chunks_.clear();

  // Seal all non-empty builders
  // Collect topic IDs first to avoid modifying map during iteration
  std::vector<TopicId> builder_ids;
  builder_ids.reserve(builders_.size());
  for (const auto& [topic_id, builder] : builders_) {
    if (builder.row_count() > 0) {
      builder_ids.push_back(topic_id);
    }
  }
  for (TopicId topic_id : builder_ids) {
    auto it = builders_.find(topic_id);
    if (it != builders_.end()) {
      result.emplace_back(topic_id, it->second.seal());
      builders_.erase(it);
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

TopicChunkBuilder& DataWriter::get_or_create_builder(TopicId topic_id) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    return it->second;
  }

  // Look up the topic storage to get descriptor info
  const auto* storage = engine_.get_topic_storage(topic_id);
  assert(storage != nullptr && "get_or_create_builder called with unknown topic_id");

  const auto& desc = storage->descriptor();
  SchemaId schema_id = desc.schema_id;
  uint32_t max_rows = desc.max_chunk_rows;

  // Build or retrieve cached column descriptors
  auto col_it = topic_columns_.find(topic_id);
  if (col_it == topic_columns_.end()) {
    // Schema-based topic: look up type tree and flatten
    const auto* type_tree = engine_.type_registry().lookup(schema_id);
    if (type_tree != nullptr) {
      topic_columns_[topic_id] = build_column_descriptors(*type_tree);
    } else {
      // No schema found — this shouldn't happen for valid topics
      // but handle gracefully with empty columns
      topic_columns_[topic_id] = {};
    }
    col_it = topic_columns_.find(topic_id);
  }

  auto [insert_it, inserted] = builders_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(topic_id),
      std::forward_as_tuple(topic_id, schema_id, col_it->second, max_rows));

  return insert_it->second;
}

std::vector<ColumnDescriptor> DataWriter::build_column_descriptors(
    const TypeTreeNode& root) {
  std::vector<ColumnDescriptor> result;
  FieldId next_id = 0;

  if (root.kind != TypeKind::kStruct) {
    // Single-leaf type tree
    ColumnDescriptor desc;
    desc.field_id = next_id++;
    desc.field_path = root.name;
    desc.logical_type =
        root.primitive_type.value_or(PrimitiveType::kFloat64);
    result.push_back(std::move(desc));
    return result;
  }

  // Struct: skip root name, flatten children
  for (const auto& child : root.children) {
    flatten_columns_impl(*child, "", next_id, result);
  }
  return result;
}

void DataWriter::auto_seal(TopicId topic_id) {
  auto it = builders_.find(topic_id);
  if (it == builders_.end()) {
    return;
  }
  pending_chunks_[topic_id].push_back(it->second.seal());
  builders_.erase(it);
}

}  // namespace pj::engine
