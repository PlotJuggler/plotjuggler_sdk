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
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "pj/base/span.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {

// Import base types into engine namespace
using pj::BitSpan;
using pj::DatasetId;
using pj::FieldId;
using pj::NumericType;
using pj::NumericValue;
using pj::PrimitiveType;
using pj::SchemaId;
using pj::Span;
using pj::Timestamp;
using pj::TopicId;
using pj::TypeKind;
using pj::TypeTreeNode;

class DataEngine;  // forward declaration

/// Handle returned by bind_topic_writer for fast-path column access.
struct TopicWriteHandle {
  /// Topic associated with this handle.
  TopicId topic_id;
  /// Field ids aligned to writer column order.
  std::vector<FieldId> field_ids;
};

/// Handle for scalar convenience API — single numeric column per topic.
struct ScalarSeriesHandle {
  /// Scalar topic id.
  TopicId topic_id;
  /// Value field id (always one logical scalar field).
  FieldId value_field;
};

/// Describes one column's data for bulk append_columns().
struct ColumnData {
  /// Column index in topic schema.
  std::size_t col_index;

  /// Arrow-compatible string data (offsets + concatenated bytes).
  struct StringData {
    Span<const uint32_t> offsets;  // (row_count + 1) entries
    Span<const char> values;
  };

  /// Type-safe column payload — variant index determines StorageKind.
  using Data = std::variant<
      Span<const float>,     // kFloat32
      Span<const double>,    // kFloat64
      Span<const int32_t>,   // kInt32
      Span<const int64_t>,   // kInt64
      Span<const uint64_t>,  // kUint64
      Span<const uint8_t>,   // kBool (one byte per bool)
      StringData             // kString
      >;

  Data data;

  /// Optional validity bits aligned to row order (empty = all valid).
  BitSpan validity;

  /// Derive row count from the active variant alternative.
  [[nodiscard]] std::size_t row_count() const;

  /// Derive StorageKind from the active variant alternative.
  [[nodiscard]] StorageKind kind() const;

  // Convenience factories
  static ColumnData Float32(std::size_t col, Span<const float> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Float64(std::size_t col, Span<const double> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int32(std::size_t col, Span<const int32_t> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int64(std::size_t col, Span<const int64_t> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Uint64(std::size_t col, Span<const uint64_t> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Bool(std::size_t col, Span<const uint8_t> values, BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData String(
      std::size_t col, Span<const uint32_t> offsets, Span<const char> str_data, BitSpan validity = {}) {
    return {col, Data{StringData{offsets, str_data}}, validity};
  }
};

class DataWriter {
 public:
  /// Create a writer bound to one engine instance.
  explicit DataWriter(DataEngine& engine);

  // ---- Schema registration (delegates to engine's TypeRegistry) ----
  /// Register a schema name -> type tree mapping.
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // ---- Topic registration ----
  /// Register a topic under `dataset_id`.
  [[nodiscard]] absl::StatusOr<TopicId> register_topic(DatasetId dataset_id, TopicDescriptor descriptor);

  // ---- Bind for fast path ----
  /// Resolve and cache topic columns for low-overhead writes.
  [[nodiscard]] absl::StatusOr<TopicWriteHandle> bind_topic_writer(TopicId topic_id);

  // ---- Field resolution ----
  /// Resolve one field path to its field id.
  [[nodiscard]] absl::StatusOr<FieldId> resolve_field(TopicId topic_id, std::string_view field_path);

  // ---- Row-at-a-time append ----
  /// Begin one row at timestamp `t`.
  [[nodiscard]] absl::Status begin_row(TopicId topic_id, Timestamp t);

  /// Finalize current row for `topic_id`.
  void finish_row(TopicId topic_id);

  // ---- Set values for current row by column index (7 storage types) ----
  /// Set float32 value in current row.
  void set_float32(TopicId topic_id, std::size_t col_index, float value);

  /// Set float64 value in current row.
  void set_float64(TopicId topic_id, std::size_t col_index, double value);

  /// Set int32 value in current row.
  void set_int32(TopicId topic_id, std::size_t col_index, int32_t value);

  /// Set int64 value in current row.
  void set_int64(TopicId topic_id, std::size_t col_index, int64_t value);

  /// Set uint64 value in current row.
  void set_uint64(TopicId topic_id, std::size_t col_index, uint64_t value);

  /// Set string value in current row.
  void set_string(TopicId topic_id, std::size_t col_index, std::string_view value);

  /// Set bool value in current row.
  void set_bool(TopicId topic_id, std::size_t col_index, bool value);

  /// Mark current row value as null.
  void set_null(TopicId topic_id, std::size_t col_index);

  // ---- Bulk column append ----
  /// Append aligned column batches and timestamps (auto-chunking if needed).
  [[nodiscard]] absl::Status append_columns(
      TopicId topic_id, Span<const Timestamp> timestamps, Span<const ColumnData> columns);

  // ---- Scalar convenience API ----
  /// Create/register a single-column scalar topic.
  [[nodiscard]] absl::StatusOr<ScalarSeriesHandle> register_scalar_series(
      DatasetId dataset_id, std::string_view topic_name, NumericType value_type);
  /// Append one scalar sample.
  void append_scalar(const ScalarSeriesHandle& handle, Timestamp t, NumericValue value);

  // ---- Flush ----
  /// Seal and return pending chunks for one topic.
  [[nodiscard]] std::vector<TopicChunk> flush(TopicId topic_id);

  /// Seal and return all pending chunks for all topics.
  [[nodiscard]] std::vector<std::pair<TopicId, TopicChunk>> flush_all();

 private:
  DataEngine& engine_;
  absl::flat_hash_map<TopicId, TopicChunkBuilder> builders_;
  absl::flat_hash_map<TopicId, std::vector<TopicChunk>> pending_chunks_;

  // Column descriptors cached per topic (needed to recreate builders)
  absl::flat_hash_map<TopicId, std::vector<ColumnDescriptor>> topic_columns_;

  TopicChunkBuilder& get_or_create_builder(TopicId topic_id);

  // Build column descriptors from a type tree
  static std::vector<ColumnDescriptor> build_column_descriptors(const TypeTreeNode& root);

  // Seal current builder and move chunk to pending list
  void auto_seal(TopicId topic_id);
};

}  // namespace pj::engine
