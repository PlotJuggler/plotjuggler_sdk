#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/topic_storage.hpp"

namespace PJ {

class DataEngine;  // forward declaration

/// Handle returned by bind_topic_writer for fast-path column access.
struct TopicWriteHandle {
  /// Topic associated with this handle.
  PJ::TopicId topic_id;
  /// Field ids aligned to writer column order.
  std::vector<PJ::FieldId> field_ids;
};

/// Handle for scalar convenience API — single numeric column per topic.
struct ScalarSeriesHandle {
  /// Scalar topic id.
  PJ::TopicId topic_id;
  /// Value field id (always one logical scalar field).
  PJ::FieldId value_field;
};

/// Describes one column's data for bulk appendColumns().
struct ColumnData {
  /// Column index in topic schema.
  std::size_t col_index;

  /// Arrow-compatible string data (offsets + concatenated bytes).
  struct StringData {
    PJ::Span<const uint32_t> offsets;  // (row_count + 1) entries
    PJ::Span<const char> values;
  };

  /// Type-safe column payload — variant index determines StorageKind.
  using Data = std::variant<
      PJ::Span<const float>,     // kFloat32
      PJ::Span<const double>,    // kFloat64
      PJ::Span<const int32_t>,   // kInt32
      PJ::Span<const int64_t>,   // kInt64
      PJ::Span<const uint64_t>,  // kUint64
      PJ::Span<const uint8_t>,   // kBool (one byte per bool)
      StringData                 // kString
      >;

  Data data;

  /// Optional validity bits aligned to row order (empty = all valid).
  PJ::BitSpan validity;

  /// Derive row count from the active variant alternative.
  [[nodiscard]] std::size_t rowCount() const;

  /// Derive StorageKind from the active variant alternative.
  [[nodiscard]] StorageKind kind() const;

  // Convenience factories
  static ColumnData Float32(std::size_t col, PJ::Span<const float> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Float64(std::size_t col, PJ::Span<const double> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int32(std::size_t col, PJ::Span<const int32_t> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int64(std::size_t col, PJ::Span<const int64_t> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Uint64(std::size_t col, PJ::Span<const uint64_t> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Bool(std::size_t col, PJ::Span<const uint8_t> values, PJ::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData String(
      std::size_t col, PJ::Span<const uint32_t> offsets, PJ::Span<const char> str_data, PJ::BitSpan validity = {}) {
    return {col, Data{StringData{offsets, str_data}}, validity};
  }
};

class DataWriter {
 public:
  /// Create a writer bound to one engine instance.
  explicit DataWriter(DataEngine& engine);

  // ---- Schema registration (delegates to engine's TypeRegistry) ----
  /// Register a schema name -> type tree mapping.
  [[nodiscard]] PJ::Expected<PJ::SchemaId> registerSchema(
      std::string schema_name, std::shared_ptr<PJ::TypeTreeNode> type_tree);

  // ---- Topic registration ----
  /// Register a topic under `dataset_id`.
  [[nodiscard]] PJ::Expected<PJ::TopicId> registerTopic(PJ::DatasetId dataset_id, TopicDescriptor descriptor);

  // ---- Bind for fast path ----
  /// Resolve and cache topic columns for low-overhead writes.
  [[nodiscard]] PJ::Expected<TopicWriteHandle> bindTopicWriter(PJ::TopicId topic_id);

  // ---- Field resolution ----
  /// Resolve one field path to its field id.
  [[nodiscard]] PJ::Expected<PJ::FieldId> resolveField(PJ::TopicId topic_id, std::string_view field_path);

  // ---- Row-at-a-time append ----
  /// Begin one row at timestamp `t`.
  [[nodiscard]] PJ::Status beginRow(PJ::TopicId topic_id, PJ::Timestamp t);

  /// Finalize current row for `topic_id`. Returns error if begin_row was not called first.
  [[nodiscard]] PJ::Status finishRow(PJ::TopicId topic_id);

  /// Set a typed value in the current row.
  /// Supported T: float, double, int32_t, int64_t, uint64_t, bool, std::string_view.
  template <typename T>
  void set(PJ::TopicId topic_id, std::size_t col_index, T value);

  /// Mark current row value as null.
  void setNull(PJ::TopicId topic_id, std::size_t col_index);

  // ---- Bulk column append ----
  /// Append aligned column batches and timestamps (auto-chunking if needed).
  [[nodiscard]] PJ::Status appendColumns(
      PJ::TopicId topic_id, PJ::Span<const PJ::Timestamp> timestamps, PJ::Span<const ColumnData> columns);

  // ---- Scalar convenience API ----
  /// Create/register a single-column scalar topic.
  [[nodiscard]] PJ::Expected<ScalarSeriesHandle> registerScalarSeries(
      PJ::DatasetId dataset_id, std::string_view topic_name, PJ::NumericType value_type);
  /// Append one scalar sample.
  void appendScalar(const ScalarSeriesHandle& handle, PJ::Timestamp t, PJ::NumericValue value);

  // ---- Dynamic column addition ----
  /// Ensure a column with `field_path` and `type` exists for `topic_id`.
  /// - No-op if a column with this exact path already exists with the same type; returns its FieldId.
  /// - Returns error if the path already exists with a DIFFERENT type.
  /// - If new and a row is IN PROGRESS: returns error (applies only to new columns; existing columns
  ///   are returned safely even mid-row).
  /// - If new and no row in progress: seals any pending builder, appends a ColumnDescriptor, persists layout.
  /// Works for both typed (schema_id != 0) and schemaless (schema_id == 0) topics.
  /// NOTE: on typed topics, columns added via ensure_column are NOT reflected in getTypeTree() —
  /// they exist only in the physical column layout (TopicStorage::column_descriptors / chunk descriptors).
  [[nodiscard]] PJ::Expected<PJ::FieldId> ensureColumn(
      PJ::TopicId topic_id, std::string_view field_path, PJ::PrimitiveType type);

  // ---- Variable-length array expansion ----
  /// Ensure the variable-length array at `array_field_path` has at least `new_length`
  /// element columns. Must be called OUTSIDE a begin_row/finish_row block.
  /// - new_length <= current expansion: no-op, returns current count.
  /// - new_length > array_expansion_limit: clamps to limit, records truncation.
  /// - Otherwise: seals current builder, adds new ColumnDescriptors, updates TopicStorage.
  /// Returns actual expansion count (may be less than new_length if clamped).
  /// For typed topics (schema_id != 0): validates field against schema; element_type ignored.
  /// For schemaless topics (schema_id == 0): any field path accepted; uses element_type.
  [[nodiscard]] PJ::Expected<uint32_t> expandArray(
      PJ::TopicId topic_id, std::string_view array_field_path, uint32_t new_length,
      PJ::PrimitiveType element_type = PJ::PrimitiveType::kFloat64);

  // ---- Flush ----
  /// Seal and return pending chunks for one topic.
  [[nodiscard]] std::vector<TopicChunk> flush(PJ::TopicId topic_id);

  /// Seal and return all pending chunks for all topics.
  [[nodiscard]] std::vector<std::pair<PJ::TopicId, TopicChunk>> flushAll();

 private:
  DataEngine& engine_;
  absl::flat_hash_map<PJ::TopicId, TopicChunkBuilder> builders_;
  absl::flat_hash_map<PJ::TopicId, std::vector<TopicChunk>> pending_chunks_;

  // Column descriptors cached per topic (needed to recreate builders)
  absl::flat_hash_map<PJ::TopicId, std::vector<ColumnDescriptor>> topic_columns_;

  TopicChunkBuilder& getOrCreateBuilder(PJ::TopicId topic_id);

  // Populate topic_columns_[topic_id] from TopicStorage if not already cached.
  void ensureColsLoaded(PJ::TopicId topic_id, const TopicStorage& storage);

  // Build column descriptors from a type tree
  static std::vector<ColumnDescriptor> buildColumnDescriptors(const PJ::TypeTreeNode& root);

  // Seal current builder and move chunk to pending list
  void autoSeal(PJ::TopicId topic_id);

  // Seal and erase the current builder (if any) before a column layout change.
  // No-op if no builder exists; skips sealing if builder has zero rows.
  void sealBeforeLayoutChange(PJ::TopicId topic_id);
};

}  // namespace PJ
