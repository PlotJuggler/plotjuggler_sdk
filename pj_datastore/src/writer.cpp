// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_datastore/writer.hpp"

#include <fmt/format.h>
#include <tsl/robin_map.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/assert.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/type_registry.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Impl definition
// ---------------------------------------------------------------------------

struct DataWriter::Impl {
  explicit Impl(DataEngine& eng) : engine(eng) {}
  DataEngine& engine;
  tsl::robin_map<PJ::TopicId, TopicChunkBuilder> builders;
  tsl::robin_map<PJ::TopicId, std::vector<TopicChunk>> pending_chunks;
  tsl::robin_map<PJ::TopicId, std::vector<ColumnDescriptor>> topic_columns;
};

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

std::size_t ColumnData::rowCount() const {
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
void flatten_columns_impl(
    const TypeTreeNode& node, std::string_view prefix, FieldId& next_field_id, std::vector<ColumnDescriptor>& out);

// Expand one array element (at path `element_path`, e.g., "poses[0]") into ColumnDescriptors.
// Handles: struct element (recurse into children), primitive/enum element (single column).
void flatten_array_element_impl(
    const TypeTreeNode& element_type, std::string_view element_path, FieldId& next_field_id,
    std::vector<ColumnDescriptor>& out) {
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
void flatten_columns_impl(
    const TypeTreeNode& node, std::string_view prefix, FieldId& next_field_id, std::vector<ColumnDescriptor>& out) {
  std::string current_path = prefix.empty() ? node.name : fmt::format("{}.{}", prefix, node.name);

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
        std::string elem_path = fmt::format("{}[{}]", current_path, i);
        flatten_array_element_impl(*node.element_type, elem_path, next_field_id, out);
      }
    }
    // Variable-length: 0 columns initially — caller uses expandArray() to grow dynamically
    return;
  }

  // Leaf node (primitive or enum) -- produce a column descriptor
  ColumnDescriptor desc;
  desc.field_id = next_field_id++;
  desc.field_path = std::move(current_path);
  desc.logical_type = node.primitive_type.value_or(PrimitiveType::kFloat64);
  out.push_back(std::move(desc));
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
    if (cur->kind != TypeKind::kStruct) {
      return nullptr;
    }
    const TypeTreeNode* found = nullptr;
    for (const auto& child : cur->children) {
      if (child->name == segment) {
        found = child.get();
        break;
      }
    }
    if (!found) {
      return nullptr;
    }
    cur = found;
  }
  return cur;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------

DataWriter::DataWriter(DataEngine& engine) : impl_(std::make_unique<Impl>(engine)) {}

DataWriter::~DataWriter() = default;
DataWriter::DataWriter(DataWriter&&) noexcept = default;
DataWriter& DataWriter::operator=(DataWriter&&) noexcept = default;

// ---------------------------------------------------------------------------
// Schema registration
// ---------------------------------------------------------------------------

Expected<SchemaId> DataWriter::registerSchema(std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree) {
  return impl_->engine.typeRegistry().registerSchema(std::move(schema_name), std::move(type_tree));
}

// ---------------------------------------------------------------------------
// Topic registration
// ---------------------------------------------------------------------------

Expected<TopicId> DataWriter::registerTopic(DatasetId dataset_id, TopicDescriptor descriptor) {
  return impl_->engine.createTopic(dataset_id, std::move(descriptor));
}

// ---------------------------------------------------------------------------
// Bind for fast-path access
// ---------------------------------------------------------------------------

Expected<TopicWriteHandle> DataWriter::bindTopicWriter(TopicId topic_id) {
  const auto* storage = impl_->engine.getTopicStorage(topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(fmt::format("Topic {} not found", topic_id));
  }

  // Ensure column descriptors are cached
  auto& builder = getOrCreateBuilder(topic_id);
  (void)builder;  // we just need the side effect of caching columns

  const auto& columns = impl_->topic_columns.at(topic_id);
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

Expected<FieldId> DataWriter::resolveField(TopicId topic_id, std::string_view field_path) {
  // Ensure columns are cached by getting or creating the builder
  auto& builder = getOrCreateBuilder(topic_id);
  (void)builder;

  auto col_it = impl_->topic_columns.find(topic_id);
  if (col_it == impl_->topic_columns.end()) {
    return PJ::unexpected(fmt::format("Topic {} not found", topic_id));
  }

  for (const auto& col : col_it->second) {
    if (col.field_path == field_path) {
      return col.field_id;
    }
  }
  return PJ::unexpected(fmt::format("Field '{}' not found in topic {}", field_path, topic_id));
}

// ---------------------------------------------------------------------------
// Row-at-a-time append
// ---------------------------------------------------------------------------

PJ::Status DataWriter::beginRow(TopicId topic_id, Timestamp t) {
  auto* storage = impl_->engine.getTopicStorage(topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(fmt::format("Topic {} not found", topic_id));
  }
  auto& builder = getOrCreateBuilder(topic_id);
  if (builder.rowCount() > 0 && t < builder.lastTimestamp()) {
    return PJ::unexpected(fmt::format("Out-of-order timestamp: t={} < last_timestamp={}", t, builder.lastTimestamp()));
  }
  builder.beginRow(t);
  return PJ::okStatus();
}

PJ::Status DataWriter::finishRow(PJ::TopicId topic_id) {
  auto it = impl_->builders.find(topic_id);
  if (it == impl_->builders.end()) {
    return PJ::unexpected(fmt::format("finish_row: no active row for topic {}", topic_id));
  }
  it.value().finishRow();

  if (it->second.isFull()) {
    autoSeal(topic_id);
  }
  return PJ::okStatus();
}

// ---------------------------------------------------------------------------
// Set values — templatized
// ---------------------------------------------------------------------------

template <typename T>
void DataWriter::set(TopicId topic_id, std::size_t col_index, T value) {
  auto it = impl_->builders.find(topic_id);
  PJ_ASSERT(it != impl_->builders.end(), "set: no builder for topic");
  if (it != impl_->builders.end()) {
    it.value().set<T>(col_index, value);
  }
}

template void DataWriter::set<float>(TopicId, std::size_t, float);
template void DataWriter::set<double>(TopicId, std::size_t, double);
template void DataWriter::set<int32_t>(TopicId, std::size_t, int32_t);
template void DataWriter::set<int64_t>(TopicId, std::size_t, int64_t);
template void DataWriter::set<uint64_t>(TopicId, std::size_t, uint64_t);
template void DataWriter::set<bool>(TopicId, std::size_t, bool);
template void DataWriter::set<std::string_view>(TopicId, std::size_t, std::string_view);

void DataWriter::setNull(TopicId topic_id, std::size_t col_index) {
  auto it = impl_->builders.find(topic_id);
  PJ_ASSERT(it != impl_->builders.end(), "set_null: no builder for topic");
  if (it != impl_->builders.end()) {
    it.value().setNull(col_index);
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
          [&](Span<const float> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const double> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const int32_t> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const int64_t> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const uint64_t> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](Span<const uint8_t> d) { builder.appendColumn(col.col_index, d.subspan(offset, batch_size)); },
          [&](const ColumnData::StringData& s) {
            builder.appendColumnStrings(col.col_index, s.offsets.subspan(offset, batch_size + 1), s.values);
          },
      },
      col.data);

  // Apply validity bitmap if present
  if (!col.validity.empty()) {
    builder.appendColumnValidity(col.col_index, col.validity.subspan(offset, batch_size));
  }
}

}  // namespace

PJ::Status DataWriter::appendColumns(
    TopicId topic_id, Span<const Timestamp> timestamps, Span<const ColumnData> columns) {
  auto* storage = impl_->engine.getTopicStorage(topic_id);
  if (storage == nullptr) {
    return PJ::unexpected(fmt::format("Topic {} not found", topic_id));
  }

  // Validate all column row counts match timestamp count
  for (const auto& col : columns) {
    const std::size_t n = col.rowCount();
    if (n != timestamps.size()) {
      return PJ::unexpected(
          fmt::format("Column {} has {} rows but {} timestamps provided", col.col_index, n, timestamps.size()));
    }

    if (!col.validity.empty()) {
      if (col.validity.bit_length != n) {
        return PJ::unexpected(fmt::format("Column {} validity bit_length mismatch", col.col_index));
      }
      const std::size_t available_bits = col.validity.bytes.size() * 8;
      if (col.validity.bit_offset + col.validity.bit_length > available_bits) {
        return PJ::unexpected(fmt::format("Column {} validity range out of bounds", col.col_index));
      }
    }
  }

  if (timestamps.empty()) {
    return PJ::okStatus();
  }

  // Validate timestamp ordering
  auto& builder = getOrCreateBuilder(topic_id);
  if (builder.rowCount() > 0 && timestamps[0] < builder.lastTimestamp()) {
    return PJ::unexpected(
        fmt::format("Out-of-order timestamp: t={} < last_timestamp={}", timestamps[0], builder.lastTimestamp()));
  }

  std::size_t offset = 0;
  const std::size_t total = timestamps.size();

  while (offset < total) {
    auto& b = getOrCreateBuilder(topic_id);
    const std::size_t batch_size = std::min(total - offset, static_cast<std::size_t>(b.remainingCapacity()));

    b.appendTimestamps(timestamps.subspan(offset, batch_size));
    for (const auto& col : columns) {
      append_single_column_to_builder(b, col, offset, batch_size);
    }
    b.finishBulkAppend();

    if (b.isFull()) {
      autoSeal(topic_id);
    }

    offset += batch_size;
  }

  return PJ::okStatus();
}

// ---------------------------------------------------------------------------
// Scalar convenience API
// ---------------------------------------------------------------------------

Expected<ScalarSeriesHandle> DataWriter::registerScalarSeries(
    DatasetId dataset_id, std::string_view topic_name, NumericType value_type) {
  // Create a topic descriptor for a scalar series (schema_id = 0)
  TopicDescriptor desc;
  desc.name = std::string(topic_name);
  desc.schema_id = 0;

  auto topic_id_or = impl_->engine.createTopic(dataset_id, std::move(desc));
  if (!topic_id_or.has_value()) {
    return PJ::unexpected(topic_id_or.error());
  }
  TopicId topic_id = *topic_id_or;

  // Build a single column descriptor for the "value" field
  ColumnDescriptor col_desc;
  col_desc.field_id = 0;
  col_desc.logical_type = numeric_to_primitive(value_type);
  col_desc.field_path = "value";

  std::vector<ColumnDescriptor> columns;
  columns.push_back(std::move(col_desc));
  impl_->topic_columns[topic_id] = columns;

  // Persist the column layout in TopicStorage so fresh writers and the derived
  // engine can resolve it without requiring a committed (sealed) chunk.
  if (auto* storage = impl_->engine.getTopicStorage(topic_id)) {
    storage->setColumnDescriptors(std::move(columns));
  }

  ScalarSeriesHandle handle{topic_id, 0};
  return handle;
}

void DataWriter::appendScalar(const ScalarSeriesHandle& handle, Timestamp t, NumericValue value) {
  auto& builder = getOrCreateBuilder(handle.topic_id);
  PJ_ASSERT(builder.rowCount() == 0 || t >= builder.lastTimestamp(), "append_scalar: out-of-order timestamp");
  builder.beginRow(t);

  const auto col = static_cast<std::size_t>(handle.value_field);
  std::visit(
      [&builder, col](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          builder.set(col, v);
        } else if constexpr (std::is_same_v<T, double>) {
          builder.set(col, v);
        } else if constexpr (std::is_same_v<T, int32_t>) {
          builder.set(col, v);
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int64_t>) {
          builder.set(col, static_cast<int64_t>(v));
        } else if constexpr (
            std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
            std::is_same_v<T, uint64_t>) {
          builder.set(col, static_cast<uint64_t>(v));
        }
      },
      value);

  builder.finishRow();

  if (builder.isFull()) {
    autoSeal(handle.topic_id);
  }
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------

std::vector<TopicChunk> DataWriter::flush(TopicId topic_id) {
  std::vector<TopicChunk> result;

  // Collect any pending (auto-sealed) chunks
  auto pending_it = impl_->pending_chunks.find(topic_id);
  if (pending_it != impl_->pending_chunks.end()) {
    result = std::move(pending_it.value());
    impl_->pending_chunks.erase(pending_it);
  }

  // Seal the current builder if it has rows
  auto builder_it = impl_->builders.find(topic_id);
  if (builder_it != impl_->builders.end() && builder_it->second.rowCount() > 0) {
    result.push_back(builder_it.value().seal());
    impl_->builders.erase(builder_it);
  }

  return result;
}

std::vector<std::pair<TopicId, TopicChunk>> DataWriter::flushAll() {
  std::vector<std::pair<TopicId, TopicChunk>> result;

  // Collect all pending chunks
  for (auto it = impl_->pending_chunks.begin(); it != impl_->pending_chunks.end(); ++it) {
    for (auto& chunk : it.value()) {
      result.emplace_back(it->first, std::move(chunk));
    }
  }
  impl_->pending_chunks.clear();

  // Seal all non-empty builders
  // Collect topic IDs first to avoid modifying map during iteration
  std::vector<TopicId> builder_ids;
  builder_ids.reserve(impl_->builders.size());
  for (auto it = impl_->builders.begin(); it != impl_->builders.end(); ++it) {
    if (it->second.rowCount() > 0) {
      builder_ids.push_back(it->first);
    }
  }
  for (TopicId topic_id : builder_ids) {
    auto it = impl_->builders.find(topic_id);
    if (it != impl_->builders.end()) {
      result.emplace_back(topic_id, it.value().seal());
      impl_->builders.erase(it);
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Dynamic column addition
// ---------------------------------------------------------------------------

Expected<FieldId> DataWriter::ensureColumn(TopicId topic_id, std::string_view field_path, PrimitiveType type) {
  auto* storage = impl_->engine.getTopicStorage(topic_id);
  if (!storage) {
    return PJ::unexpected(fmt::format("ensure_column: topic {} not found", topic_id));
  }

  ensureColsLoaded(topic_id, *storage);
  auto& cols = impl_->topic_columns[topic_id];

  // No-op: column already exists — return existing field id.
  // Type mismatch is an error: caller must not re-register with a different type.
  for (const auto& col : cols) {
    if (col.field_path == field_path) {
      if (col.logical_type != type) {
        return PJ::unexpected(
            fmt::format("ensure_column: field '{}' already exists with a different type", field_path));
      }
      return col.field_id;
    }
  }

  // Guard: no row in progress
  auto builder_it = impl_->builders.find(topic_id);
  if (builder_it != impl_->builders.end() && builder_it->second.isRowInProgress()) {
    return PJ::unexpected(
        fmt::format(
            "ensure_column: topic {} has a row in progress; call finishRow() before adding new columns", topic_id));
  }

  // Seal the current builder (if any) before changing the column layout.
  sealBeforeLayoutChange(topic_id);

  // Append new column (field ids are always dense starting at 0 — assert the invariant)
  PJ_ASSERT(
      cols.empty() || cols.back().field_id == static_cast<FieldId>(cols.size() - 1),
      "ensure_column: field_id invariant broken — non-dense column ids detected");
  FieldId new_id = static_cast<FieldId>(cols.size());
  ColumnDescriptor desc;
  desc.field_id = new_id;
  desc.logical_type = type;
  desc.field_path = std::string(field_path);
  cols.push_back(std::move(desc));

  storage->setColumnDescriptors(cols);
  return new_id;
}

// ---------------------------------------------------------------------------
// Variable-length array expansion
// ---------------------------------------------------------------------------

PJ::Expected<uint32_t> DataWriter::expandArray(
    PJ::TopicId topic_id, std::string_view array_field_path, uint32_t new_length, PJ::PrimitiveType element_type) {
  // Validate topic exists
  TopicStorage* storage = impl_->engine.getTopicStorage(topic_id);
  if (!storage) {
    return PJ::unexpected(fmt::format("expand_array: topic {} not found", topic_id));
  }

  // Track the largest observed array length for metadata
  storage->updateMaxObservedArrayLength(new_length);

  // Read authoritative expansion count from TopicStorage — shared across all DataWriter instances.
  std::string path_key(array_field_path);
  const uint32_t current = storage->arrayExpansionCount(path_key);

  // Fast no-op
  if (new_length <= current) {
    return current;
  }

  // Get type tree — may be null for schemaless topics (schema_id == 0).
  SchemaId schema_id = storage->descriptor().schema_id;
  const TypeTreeNode* type_tree = impl_->engine.typeRegistry().lookup(schema_id);

  // Typed topics: validate the array field against the schema before touching any state.
  const TypeTreeNode* array_node = nullptr;
  if (type_tree) {
    array_node = find_child_at_path(*type_tree, array_field_path);
    if (!array_node) {
      return PJ::unexpected(fmt::format("expand_array: field '{}' not found in schema", array_field_path));
    }
    if (array_node->kind != TypeKind::kArray) {
      return PJ::unexpected(fmt::format("expand_array: field '{}' is not an array node", array_field_path));
    }
    if (array_node->fixed_array_size.has_value()) {
      return PJ::unexpected(
          fmt::format("expand_array: field '{}' is fixed-size; use schema declaration", array_field_path));
    }
  }

  // Apply expansion limit (clamp and record truncation) — common to both paths.
  uint32_t limit = storage->descriptor().array_expansion_limit;
  uint32_t actual = std::min(new_length, limit);
  if (new_length > limit) {
    storage->incrementTruncatedSampleCount();
  }
  if (actual <= current) {
    return current;
  }

  // Reject expansion if a row is currently in progress (between begin_row and finish_row).
  auto builder_it = impl_->builders.find(topic_id);
  if (builder_it != impl_->builders.end() && builder_it->second.isRowInProgress()) {
    return PJ::unexpected(
        fmt::format(
            "expand_array: topic {}"
            " has a row in progress; call finishRow() or abandon the row before calling expandArray()",
            topic_id));
  }

  // Seal and stage the current builder (if any) before changing the column layout.
  sealBeforeLayoutChange(topic_id);

  // Load current column descriptor list for this topic.
  ensureColsLoaded(topic_id, *storage);
  auto& cols = impl_->topic_columns[topic_id];

  if (!type_tree) {
    // Schemaless path: any field path is accepted; use element_type for new columns.
    PJ_ASSERT(
        cols.empty() || cols.back().field_id == static_cast<FieldId>(cols.size() - 1),
        "expand_array: field_id invariant broken — non-dense column ids detected");
    FieldId next_field_id = static_cast<FieldId>(cols.size());
    for (uint32_t i = current; i < actual; ++i) {
      std::string elem_path = fmt::format("{}[{}]", array_field_path, i);
      // Idempotent: skip if already present (e.g. added via ensure_column)
      bool already_exists = false;
      for (const auto& col : cols) {
        if (col.field_path == elem_path) {
          already_exists = true;
          break;
        }
      }
      if (!already_exists) {
        ColumnDescriptor desc;
        desc.field_id = next_field_id++;
        desc.logical_type = element_type;
        desc.field_path = std::move(elem_path);
        cols.push_back(std::move(desc));
      }
    }
  } else {
    // Typed path: generate columns from the schema element type.
    // Use a per-index existence check (same as schemaless) so that columns
    // manually added via ensure_column are not duplicated.
    PJ_ASSERT(
        cols.empty() || cols.back().field_id == static_cast<FieldId>(cols.size() - 1),
        "expand_array: field_id invariant broken — non-dense column ids detected");
    FieldId next_field_id = static_cast<FieldId>(cols.size());
    for (uint32_t i = current; i < actual; ++i) {
      std::string elem_prefix = fmt::format("{}[{}]", array_field_path, i);
      // Check for both exact match (primitive element) and prefix match (struct element fields).
      bool already_exists = false;
      for (const auto& col : cols) {
        if (col.field_path == elem_prefix || col.field_path.starts_with(elem_prefix + ".")) {
          already_exists = true;
          break;
        }
      }
      if (!already_exists) {
        flatten_array_element_impl(*array_node->element_type, elem_prefix, next_field_id, cols);
      }
    }
  }

  // Persist updated layout and expansion count in TopicStorage
  storage->setColumnDescriptors(cols);
  storage->setArrayExpansionCount(path_key, actual);

  return actual;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void DataWriter::ensureColsLoaded(TopicId topic_id, const TopicStorage& storage) {
  auto& cols = impl_->topic_columns[topic_id];
  if (!cols.empty()) {
    return;
  }
  // Always prefer the layout persisted in TopicStorage when it is non-empty.
  // expandArray() / ensureColumn() call storage->setColumnDescriptors() to record the
  // current (potentially grown) column layout. A second DataWriter created
  // after an expansion must see the expanded layout, not a stale rebuild.
  const auto& stored = storage.columnDescriptors();
  if (!stored.empty()) {
    cols = stored;
    return;
  }
  const auto* type_tree = impl_->engine.typeRegistry().lookup(storage.descriptor().schema_id);
  if (type_tree) {
    cols = buildColumnDescriptors(*type_tree);
    return;
  }
  // schema_id==0 with no stored layout: fall back to first committed chunk.
  const auto& chunks = storage.sealedChunks();
  if (!chunks.empty()) {
    cols.reserve(chunks[0].columns.size());
    for (const auto& col : chunks[0].columns) {
      cols.push_back(*col.descriptor);
    }
  }
  // else: stays empty — valid for brand-new schemaless topic
}

TopicChunkBuilder& DataWriter::getOrCreateBuilder(TopicId topic_id) {
  auto it = impl_->builders.find(topic_id);
  if (it != impl_->builders.end()) {
    return it.value();
  }

  const auto* storage = impl_->engine.getTopicStorage(topic_id);
  PJ_ASSERT(storage != nullptr, "get_or_create_builder: topic storage not found");

  const auto& desc = storage->descriptor();
  uint32_t max_rows = desc.max_chunk_rows;

  ensureColsLoaded(topic_id, *storage);
  auto col_it = impl_->topic_columns.find(topic_id);

  auto [insert_it, inserted] = impl_->builders.emplace(
      std::piecewise_construct, std::forward_as_tuple(topic_id),
      std::forward_as_tuple(topic_id, desc.schema_id, col_it->second, max_rows));

  return insert_it.value();
}

std::vector<ColumnDescriptor> DataWriter::buildColumnDescriptors(const TypeTreeNode& root) {
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

void DataWriter::autoSeal(TopicId topic_id) {
  auto it = impl_->builders.find(topic_id);
  if (it == impl_->builders.end()) {
    return;
  }
  impl_->pending_chunks[topic_id].push_back(it.value().seal());
  impl_->builders.erase(it);
}

void DataWriter::sealBeforeLayoutChange(TopicId topic_id) {
  auto it = impl_->builders.find(topic_id);
  if (it == impl_->builders.end()) {
    return;
  }
  if (it->second.rowCount() > 0) {
    impl_->pending_chunks[topic_id].push_back(it.value().seal());
  }
  impl_->builders.erase(it);
}

}  // namespace PJ
