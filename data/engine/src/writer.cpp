#include "pj/engine/writer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "pj/base/assert.hpp"
#include "pj/base/expected.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"

namespace pj::engine {

// ---------------------------------------------------------------------------
// ColumnData methods
// ---------------------------------------------------------------------------

namespace {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace

std::size_t ColumnData::row_count() const {
  return std::visit(
      overloaded{
          [](const StringData& s) -> std::size_t { return s.offsets.empty() ? 0 : s.offsets.size() - 1; },
          [](const auto& span) -> std::size_t { return span.size(); },
      },
      data);
}

StorageKind ColumnData::kind() const {
  static constexpr StorageKind kinds[] = {
      StorageKind::kFloat32, StorageKind::kFloat64, StorageKind::kInt32,  StorageKind::kInt64,
      StorageKind::kUint64,  StorageKind::kBool,    StorageKind::kString,
  };
  return kinds[data.index()];
}

namespace {

/// Map NumericType to PrimitiveType (same enum values, different enum types).
constexpr PrimitiveType numeric_to_primitive(NumericType nt) noexcept {
  switch (nt) {
    case NumericType::kFloat32:
      return PrimitiveType::kFloat32;
    case NumericType::kFloat64:
      return PrimitiveType::kFloat64;
    case NumericType::kInt8:
      return PrimitiveType::kInt8;
    case NumericType::kInt16:
      return PrimitiveType::kInt16;
    case NumericType::kInt32:
      return PrimitiveType::kInt32;
    case NumericType::kInt64:
      return PrimitiveType::kInt64;
    case NumericType::kUint8:
      return PrimitiveType::kUint8;
    case NumericType::kUint16:
      return PrimitiveType::kUint16;
    case NumericType::kUint32:
      return PrimitiveType::kUint32;
    case NumericType::kUint64:
      return PrimitiveType::kUint64;
  }
  return PrimitiveType::kFloat64;  // unreachable
}

// Forward declarations — flatten_columns_impl and flatten_array_element_impl are mutually recursive.
void flatten_columns_impl(const TypeTreeNode& node, std::string_view prefix, FieldId& next_field_id,
                          std::vector<ColumnDescriptor>& out);

// Expand one array element (at path `element_path`, e.g., "poses[0]") into ColumnDescriptors.
// Handles: struct element (recurse into children), primitive/enum element (single column).
void flatten_array_element_impl(const TypeTreeNode& element_type, std::string_view element_path,
                                FieldId& next_field_id, std::vector<ColumnDescriptor>& out) {
  if (element_type.kind == TypeKind::kStruct) {
    for (const auto& child : element_type.children) {
      flatten_columns_impl(*child, element_path, next_field_id, out);
    }
  } else {
    ColumnDescriptor desc;
    desc.field_id = next_field_id++;
    desc.field_path = std::string(element_path);
    desc.logical_type = element_type.primitive_type.value_or(PrimitiveType::kFloat64);
    out.push_back(std::move(desc));
  }
}

/// Recursively flatten a type tree into ColumnDescriptors, collecting both
/// field paths and PrimitiveTypes for each leaf node.
void flatten_columns_impl(const TypeTreeNode& node, std::string_view prefix, FieldId& next_field_id,
                          std::vector<ColumnDescriptor>& out) {
  std::string current_path = prefix.empty() ? node.name : absl::StrCat(prefix, ".", node.name);

  if (node.kind == TypeKind::kStruct) {
    for (const auto& child : node.children) {
      flatten_columns_impl(*child, current_path, next_field_id, out);
    }
    return;
  }

  if (node.kind == TypeKind::kArray) {
    if (node.fixed_array_size.has_value()) {
      // Fixed-size: expand all elements now at schema registration time
      for (uint32_t i = 0; i < *node.fixed_array_size; ++i) {
        std::string elem_path = absl::StrCat(current_path, "[", i, "]");
        flatten_array_element_impl(*node.element_type, elem_path, next_field_id, out);
      }
    }
    // Variable-length: 0 columns initially — caller uses expand_array() to grow dynamically
    return;
  }

  // Leaf node (primitive or enum) -- produce a column descriptor
  ColumnDescriptor desc;
  desc.field_id = next_field_id++;
  desc.field_path = std::move(current_path);
  desc.logical_type = node.primitive_type.value_or(PrimitiveType::kFloat64);
  out.push_back(std::move(desc));
}

// Generate ColumnDescriptors for array element indices [from_idx, to_idx).
// Continues from `next_field_id`. For struct elements, each child field becomes a column.
void generate_array_element_columns(const TypeTreeNode& element_type, std::string_view array_path,
                                    uint32_t from_idx, uint32_t to_idx, FieldId& next_field_id,
                                    std::vector<ColumnDescriptor>& out) {
  for (uint32_t i = from_idx; i < to_idx; ++i) {
    std::string elem_path = absl::StrCat(array_path, "[", i, "]");
    flatten_array_element_impl(element_type, elem_path, next_field_id, out);
  }
}

// Find a TypeTreeNode child by dotted path relative to root's children.
// E.g., find_child_at_path(root, "body.poses") returns the "poses" node inside "body".
// Returns nullptr if any segment is not found or path passes through a non-struct.
const TypeTreeNode* find_child_at_path(const TypeTreeNode& root, std::string_view path) {
  std::string_view remaining = path;
  const TypeTreeNode* cur = &root;
  while (!remaining.empty()) {
    size_t dot = remaining.find('.');
    std::string_view segment = remaining.substr(0, dot);
    remaining = (dot == std::string_view::npos) ? std::string_view{} : remaining.substr(dot + 1);
    if (cur->kind != TypeKind::kStruct) return nullptr;
    const TypeTreeNode* found = nullptr;
    for (const auto& child : cur->children) {
      if (child->name == segment) {
        found = child.get();
        break;
      }
    }
    if (!found) return nullptr;
    cur = found;
  }
  return cur;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DataWriter::DataWriter(DataEngine& engine) : engine_(engine) {}

// ---------------------------------------------------------------------------
// Schema registration
// ---------------------------------------------------------------------------

Expected<SchemaId> DataWriter::register_schema(std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree) {
  return engine_.type_registry().register_schema(std::move(schema_name), std::move(type_tree));
}

// ---------------------------------------------------------------------------
// Topic registration
// ---------------------------------------------------------------------------

Expected<TopicId> DataWriter::register_topic(DatasetId dataset_id, TopicDescriptor descriptor) {
  return engine_.create_topic(dataset_id, std::move(descriptor));
}

// ---------------------------------------------------------------------------
// Bind for fast-path access
// ---------------------------------------------------------------------------

Expected<TopicWriteHandle> DataWriter::bind_topic_writer(TopicId topic_id) {
  const auto* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return pj::unexpected("Topic " + std::to_string(topic_id) + " not found");
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

Expected<FieldId> DataWriter::resolve_field(TopicId topic_id, std::string_view field_path) {
  // Ensure columns are cached by getting or creating the builder
  auto& builder = get_or_create_builder(topic_id);
  (void)builder;

  auto col_it = topic_columns_.find(topic_id);
  if (col_it == topic_columns_.end()) {
    return pj::unexpected("Topic " + std::to_string(topic_id) + " not found");
  }

  for (const auto& col : col_it->second) {
    if (col.field_path == field_path) {
      return col.field_id;
    }
  }
  return pj::unexpected("Field '" + std::string(field_path) + "' not found in topic " + std::to_string(topic_id));
}

// ---------------------------------------------------------------------------
// Row-at-a-time append
// ---------------------------------------------------------------------------

pj::Status DataWriter::begin_row(TopicId topic_id, Timestamp t) {
  auto* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return pj::unexpected(absl::StrCat("Topic ", topic_id, " not found"));
  }
  auto& builder = get_or_create_builder(topic_id);
  if (builder.row_count() > 0 && t < builder.last_timestamp()) {
    return pj::unexpected(
        absl::StrCat("Out-of-order timestamp: t=", t, " < last_timestamp=", builder.last_timestamp()));
  }
  builder.begin_row(t);
  return pj::ok_status();
}

pj::Status DataWriter::finish_row(pj::TopicId topic_id) {
  auto it = builders_.find(topic_id);
  if (it == builders_.end()) {
    return pj::unexpected(absl::StrCat("finish_row: no active row for topic ", topic_id));
  }
  it->second.finish_row();

  if (it->second.is_full()) {
    auto_seal(topic_id);
  }
  return pj::ok_status();
}

// ---------------------------------------------------------------------------
// Set values (6 storage types)
// ---------------------------------------------------------------------------

void DataWriter::set_float32(TopicId topic_id, std::size_t col_index, float value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_float32: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_float32(col_index, value);
  }
}

void DataWriter::set_float64(TopicId topic_id, std::size_t col_index, double value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_float64: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_float64(col_index, value);
  }
}

void DataWriter::set_int32(TopicId topic_id, std::size_t col_index, int32_t value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_int32: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_int32(col_index, value);
  }
}

void DataWriter::set_int64(TopicId topic_id, std::size_t col_index, int64_t value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_int64: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_int64(col_index, value);
  }
}

void DataWriter::set_uint64(TopicId topic_id, std::size_t col_index, uint64_t value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_uint64: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_uint64(col_index, value);
  }
}

void DataWriter::set_string(TopicId topic_id, std::size_t col_index, std::string_view value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_string: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_string(col_index, value);
  }
}

void DataWriter::set_bool(TopicId topic_id, std::size_t col_index, bool value) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_bool: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_bool(col_index, value);
  }
}

void DataWriter::set_null(TopicId topic_id, std::size_t col_index) {
  auto it = builders_.find(topic_id);
  PJ_ASSERT(it != builders_.end(), "set_null: no builder for topic");
  if (it != builders_.end()) {
    it->second.set_null(col_index);
  }
}

// ---------------------------------------------------------------------------
// Bulk column append
// ---------------------------------------------------------------------------

namespace {

void append_single_column_to_builder(
    TopicChunkBuilder& builder, const ColumnData& col, std::size_t offset, std::size_t batch_size) {
  std::visit(
      overloaded{
          [&](Span<const float> d) { builder.append_column_float32(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const double> d) { builder.append_column_float64(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const int32_t> d) { builder.append_column_int32(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const int64_t> d) { builder.append_column_int64(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const uint64_t> d) { builder.append_column_uint64(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const uint8_t> d) { builder.append_column_bool(col.col_index, d.subspan(offset, batch_size)); },
          [&](const ColumnData::StringData& s) {
            builder.append_column_strings(col.col_index, s.offsets.subspan(offset, batch_size + 1), s.values);
          },
      },
      col.data);

  // Apply validity bitmap if present
  if (!col.validity.empty()) {
    builder.append_column_validity(col.col_index, col.validity.subspan(offset, batch_size));
  }
}

}  // namespace

pj::Status DataWriter::append_columns(
    TopicId topic_id, Span<const Timestamp> timestamps, Span<const ColumnData> columns) {
  auto* storage = engine_.get_topic_storage(topic_id);
  if (storage == nullptr) {
    return pj::unexpected(absl::StrCat("Topic ", topic_id, " not found"));
  }

  // Validate all column row counts match timestamp count
  for (const auto& col : columns) {
    const std::size_t n = col.row_count();
    if (n != timestamps.size()) {
      return pj::unexpected(
          absl::StrCat("Column ", col.col_index, " has ", n, " rows but ", timestamps.size(), " timestamps provided"));
    }

    if (!col.validity.empty()) {
      if (col.validity.bit_length != n) {
        return pj::unexpected(absl::StrCat("Column ", col.col_index, " validity bit_length mismatch"));
      }
      const std::size_t available_bits = col.validity.bytes.size() * 8;
      if (col.validity.bit_offset + col.validity.bit_length > available_bits) {
        return pj::unexpected(absl::StrCat("Column ", col.col_index, " validity range out of bounds"));
      }
    }
  }

  if (timestamps.empty()) {
    return pj::ok_status();
  }

  // Validate timestamp ordering
  auto& builder = get_or_create_builder(topic_id);
  if (builder.row_count() > 0 && timestamps[0] < builder.last_timestamp()) {
    return pj::unexpected(
        absl::StrCat("Out-of-order timestamp: t=", timestamps[0], " < last_timestamp=", builder.last_timestamp()));
  }

  std::size_t offset = 0;
  const std::size_t total = timestamps.size();

  while (offset < total) {
    auto& b = get_or_create_builder(topic_id);
    const std::size_t batch_size = std::min(total - offset, static_cast<std::size_t>(b.remaining_capacity()));

    b.append_timestamps(timestamps.subspan(offset, batch_size));
    for (const auto& col : columns) {
      append_single_column_to_builder(b, col, offset, batch_size);
    }
    b.finish_bulk_append();

    if (b.is_full()) {
      auto_seal(topic_id);
    }

    offset += batch_size;
  }

  return pj::ok_status();
}

// ---------------------------------------------------------------------------
// Scalar convenience API
// ---------------------------------------------------------------------------

Expected<ScalarSeriesHandle> DataWriter::register_scalar_series(
    DatasetId dataset_id, std::string_view topic_name, NumericType value_type) {
  // Create a topic descriptor for a scalar series (schema_id = 0)
  TopicDescriptor desc;
  desc.name = std::string(topic_name);
  desc.schema_id = 0;

  auto topic_id_or = engine_.create_topic(dataset_id, std::move(desc));
  if (!topic_id_or.has_value()) {
    return pj::unexpected(topic_id_or.error());
  }
  TopicId topic_id = *topic_id_or;

  // Build a single column descriptor for the "value" field
  ColumnDescriptor col_desc;
  col_desc.field_id = 0;
  col_desc.logical_type = numeric_to_primitive(value_type);
  col_desc.field_path = "value";

  std::vector<ColumnDescriptor> columns;
  columns.push_back(std::move(col_desc));
  topic_columns_[topic_id] = columns;

  // Persist the column layout in TopicStorage so fresh writers and the derived
  // engine can resolve it without requiring a committed (sealed) chunk.
  if (auto* storage = engine_.get_topic_storage(topic_id)) {
    storage->set_column_descriptors(std::move(columns));
  }

  ScalarSeriesHandle handle{topic_id, 0};
  return handle;
}

void DataWriter::append_scalar(const ScalarSeriesHandle& handle, Timestamp t, NumericValue value) {
  auto& builder = get_or_create_builder(handle.topic_id);
  PJ_ASSERT(builder.row_count() == 0 || t >= builder.last_timestamp(), "append_scalar: out-of-order timestamp");
  builder.begin_row(t);

  const auto col = static_cast<std::size_t>(handle.value_field);
  std::visit(
      [&builder, col](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          builder.set_float32(col, v);
        } else if constexpr (std::is_same_v<T, double>) {
          builder.set_float64(col, v);
        } else if constexpr (std::is_same_v<T, int32_t>) {
          builder.set_int32(col, v);
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int64_t>) {
          builder.set_int64(col, static_cast<int64_t>(v));
        } else if constexpr (
            std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
            std::is_same_v<T, uint64_t>) {
          builder.set_uint64(col, static_cast<uint64_t>(v));
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
// Variable-length array expansion
// ---------------------------------------------------------------------------

pj::Expected<uint32_t> DataWriter::expand_array(pj::TopicId topic_id, std::string_view array_field_path,
                                                  uint32_t new_length) {
  // Validate topic exists
  TopicStorage* storage = engine_.get_topic_storage(topic_id);
  if (!storage) {
    return pj::unexpected(absl::StrCat("expand_array: topic ", topic_id, " not found"));
  }

  // Track the largest observed array length for metadata
  storage->update_max_observed_array_length(new_length);

  // Read authoritative expansion count from TopicStorage — shared across all DataWriter instances.
  std::string path_key(array_field_path);
  const uint32_t current = storage->array_expansion_count(path_key);

  // Fast no-op
  if (new_length <= current) return current;

  // Validate array field
  SchemaId schema_id = storage->descriptor().schema_id;
  const TypeTreeNode* type_tree = engine_.type_registry().lookup(schema_id);
  if (!type_tree) {
    return pj::unexpected(absl::StrCat("expand_array: schema_id=", schema_id,
                                        " not found (schema_id=0 topics not supported for array expansion)"));
  }
  const TypeTreeNode* array_node = find_child_at_path(*type_tree, array_field_path);
  if (!array_node) {
    return pj::unexpected(absl::StrCat("expand_array: field '", array_field_path, "' not found in schema"));
  }
  if (array_node->kind != TypeKind::kArray) {
    return pj::unexpected(absl::StrCat("expand_array: field '", array_field_path, "' is not an array node"));
  }
  if (array_node->fixed_array_size.has_value()) {
    return pj::unexpected(
        absl::StrCat("expand_array: field '", array_field_path, "' is fixed-size; use schema declaration"));
  }

  // Apply expansion limit (clamp and record truncation)
  uint32_t limit = storage->descriptor().array_expansion_limit;
  uint32_t actual = std::min(new_length, limit);
  if (new_length > limit) {
    storage->increment_truncated_sample_count();
  }
  if (actual <= current) return current;

  // Reject expansion if a row is currently in progress (between begin_row and finish_row).
  auto builder_it = builders_.find(topic_id);
  if (builder_it != builders_.end() && builder_it->second.is_row_in_progress()) {
    return pj::unexpected(absl::StrCat(
        "expand_array: topic ", topic_id,
        " has a row in progress; call finish_row() or abandon the row before calling expand_array()"));
  }

  // Seal and stage the current builder (if any) before changing the column layout.
  if (builder_it != builders_.end()) {
    if (builder_it->second.row_count() > 0) {
      pending_chunks_[topic_id].push_back(builder_it->second.seal());
    }
    builders_.erase(builder_it);
  }

  // Get or build current column descriptor list for this topic.
  // Prefer storage->column_descriptors() so second writers pick up prior expansions.
  auto& cols = topic_columns_[topic_id];
  if (cols.empty()) {
    const auto& stored = storage->column_descriptors();
    if (!stored.empty()) {
      cols = stored;
    } else {
      cols = build_column_descriptors(*type_tree);
    }
  }

  // Append new element columns from [current, actual)
  FieldId next_field_id = static_cast<FieldId>(cols.size());
  generate_array_element_columns(*array_node->element_type, array_field_path, current, actual, next_field_id, cols);

  // Persist updated layout and expansion count in TopicStorage
  storage->set_column_descriptors(cols);
  storage->set_array_expansion_count(path_key, actual);

  return actual;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

TopicChunkBuilder& DataWriter::get_or_create_builder(TopicId topic_id) {
  auto it = builders_.find(topic_id);
  if (it != builders_.end()) {
    return it->second;
  }

  const auto* storage = engine_.get_topic_storage(topic_id);
  PJ_ASSERT(storage != nullptr, "get_or_create_builder: topic storage not found");

  const auto& desc = storage->descriptor();
  SchemaId schema_id = desc.schema_id;
  uint32_t max_rows = desc.max_chunk_rows;

  // Build or retrieve cached column descriptors
  auto col_it = topic_columns_.find(topic_id);
  if (col_it == topic_columns_.end()) {
    const auto* type_tree = engine_.type_registry().lookup(schema_id);
    // Always prefer the layout persisted in TopicStorage when it is non-empty.
    // expand_array() calls storage->set_column_descriptors() to record the
    // current (potentially grown) column layout. A second DataWriter created
    // after an expansion must see the expanded layout, not a stale rebuild
    // from the type tree which would yield 0 columns for variable-length arrays.
    const auto& stored = storage->column_descriptors();
    if (!stored.empty()) {
      topic_columns_[topic_id] = stored;
    } else if (type_tree != nullptr) {
      topic_columns_[topic_id] = build_column_descriptors(*type_tree);
    } else {
      // schema_id == 0 (inline layout, e.g. topics created via register_scalar_series).
      // No stored layout yet: fall back to the first committed chunk.
      const auto& chunks = storage->sealed_chunks();
      topic_columns_[topic_id] = chunks.empty() ? std::vector<ColumnDescriptor>{} : chunks[0].column_descriptors;
    }
    col_it = topic_columns_.find(topic_id);
  }

  auto [insert_it, inserted] = builders_.emplace(
      std::piecewise_construct, std::forward_as_tuple(topic_id),
      std::forward_as_tuple(topic_id, schema_id, col_it->second, max_rows));

  return insert_it->second;
}

std::vector<ColumnDescriptor> DataWriter::build_column_descriptors(const TypeTreeNode& root) {
  std::vector<ColumnDescriptor> result;
  FieldId next_id = 0;

  if (root.kind != TypeKind::kStruct) {
    ColumnDescriptor desc;
    desc.field_id = next_id++;
    desc.field_path = root.name;
    desc.logical_type = root.primitive_type.value_or(PrimitiveType::kFloat64);
    result.push_back(std::move(desc));
    return result;
  }

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
