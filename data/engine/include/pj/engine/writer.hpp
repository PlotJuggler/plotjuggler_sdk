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
#include "pj/base/expected.hpp"
#include "pj/base/span.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {

class DataEngine;  // forward declaration

/// Handle returned by bind_topic_writer for fast-path column access.
struct TopicWriteHandle {
  /// Topic associated with this handle.
  pj::TopicId topic_id;
  /// Field ids aligned to writer column order.
  std::vector<pj::FieldId> field_ids;
};

/// Handle for scalar convenience API — single numeric column per topic.
struct ScalarSeriesHandle {
  /// Scalar topic id.
  pj::TopicId topic_id;
  /// Value field id (always one logical scalar field).
  pj::FieldId value_field;
};

/// Describes one column's data for bulk append_columns().
struct ColumnData {
  /// Column index in topic schema.
  std::size_t col_index;

  /// Arrow-compatible string data (offsets + concatenated bytes).
  struct StringData {
    pj::Span<const uint32_t> offsets;  // (row_count + 1) entries
    pj::Span<const char> values;
  };

  /// Type-safe column payload — variant index determines StorageKind.
  using Data = std::variant<
      pj::Span<const float>,     // kFloat32
      pj::Span<const double>,    // kFloat64
      pj::Span<const int32_t>,   // kInt32
      pj::Span<const int64_t>,   // kInt64
      pj::Span<const uint64_t>,  // kUint64
      pj::Span<const uint8_t>,   // kBool (one byte per bool)
      StringData                 // kString
      >;

  Data data;

  /// Optional validity bits aligned to row order (empty = all valid).
  pj::BitSpan validity;

  /// Derive row count from the active variant alternative.
  [[nodiscard]] std::size_t row_count() const;

  /// Derive StorageKind from the active variant alternative.
  [[nodiscard]] StorageKind kind() const;

  // Convenience factories
  static ColumnData Float32(std::size_t col, pj::Span<const float> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Float64(std::size_t col, pj::Span<const double> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int32(std::size_t col, pj::Span<const int32_t> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Int64(std::size_t col, pj::Span<const int64_t> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Uint64(std::size_t col, pj::Span<const uint64_t> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData Bool(std::size_t col, pj::Span<const uint8_t> values, pj::BitSpan validity = {}) {
    return {col, Data{values}, validity};
  }
  static ColumnData String(
      std::size_t col, pj::Span<const uint32_t> offsets, pj::Span<const char> str_data, pj::BitSpan validity = {}) {
    return {col, Data{StringData{offsets, str_data}}, validity};
  }
};

class DataWriter {
 public:
  /// Create a writer bound to one engine instance.
  explicit DataWriter(DataEngine& engine);

  // ---- Schema registration (delegates to engine's TypeRegistry) ----
  /// Register a schema name -> type tree mapping.
  [[nodiscard]] pj::Expected<pj::SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<pj::TypeTreeNode> type_tree);

  // ---- Topic registration ----
  /// Register a topic under `dataset_id`.
  [[nodiscard]] pj::Expected<pj::TopicId> register_topic(pj::DatasetId dataset_id, TopicDescriptor descriptor);

  // ---- Bind for fast path ----
  /// Resolve and cache topic columns for low-overhead writes.
  [[nodiscard]] pj::Expected<TopicWriteHandle> bind_topic_writer(pj::TopicId topic_id);

  // ---- Field resolution ----
  /// Resolve one field path to its field id.
  [[nodiscard]] pj::Expected<pj::FieldId> resolve_field(pj::TopicId topic_id, std::string_view field_path);

  // ---- Row-at-a-time append ----
  /// Begin one row at timestamp `t`.
  [[nodiscard]] pj::Status begin_row(pj::TopicId topic_id, pj::Timestamp t);

  /// Finalize current row for `topic_id`. Returns error if begin_row was not called first.
  [[nodiscard]] pj::Status finish_row(pj::TopicId topic_id);

  // ---- Set values for current row by column index (7 storage types) ----
  /// Set float32 value in current row.
  void set_float32(pj::TopicId topic_id, std::size_t col_index, float value);

  /// Set float64 value in current row.
  void set_float64(pj::TopicId topic_id, std::size_t col_index, double value);

  /// Set int32 value in current row.
  void set_int32(pj::TopicId topic_id, std::size_t col_index, int32_t value);

  /// Set int64 value in current row.
  void set_int64(pj::TopicId topic_id, std::size_t col_index, int64_t value);

  /// Set uint64 value in current row.
  void set_uint64(pj::TopicId topic_id, std::size_t col_index, uint64_t value);

  /// Set string value in current row.
  void set_string(pj::TopicId topic_id, std::size_t col_index, std::string_view value);

  /// Set bool value in current row.
  void set_bool(pj::TopicId topic_id, std::size_t col_index, bool value);

  /// Mark current row value as null.
  void set_null(pj::TopicId topic_id, std::size_t col_index);

  // ---- Bulk column append ----
  /// Append aligned column batches and timestamps (auto-chunking if needed).
  [[nodiscard]] pj::Status append_columns(
      pj::TopicId topic_id, pj::Span<const pj::Timestamp> timestamps, pj::Span<const ColumnData> columns);

  // ---- Scalar convenience API ----
  /// Create/register a single-column scalar topic.
  [[nodiscard]] pj::Expected<ScalarSeriesHandle> register_scalar_series(
      pj::DatasetId dataset_id, std::string_view topic_name, pj::NumericType value_type);
  /// Append one scalar sample.
  void append_scalar(const ScalarSeriesHandle& handle, pj::Timestamp t, pj::NumericValue value);

  // ---- Variable-length array expansion ----
  /// Ensure the variable-length array at `array_field_path` has at least `new_length`
  /// element columns. Must be called OUTSIDE a begin_row/finish_row block.
  /// - new_length <= current expansion: no-op, returns current count.
  /// - new_length > array_expansion_limit: clamps to limit, records truncation.
  /// - Otherwise: seals current builder, adds new ColumnDescriptors, updates TopicStorage.
  /// Returns actual expansion count (may be less than new_length if clamped).
  [[nodiscard]] pj::Expected<uint32_t> expand_array(pj::TopicId topic_id,
                                                     std::string_view array_field_path,
                                                     uint32_t new_length);

  // ---- Flush ----
  /// Seal and return pending chunks for one topic.
  [[nodiscard]] std::vector<TopicChunk> flush(pj::TopicId topic_id);

  /// Seal and return all pending chunks for all topics.
  [[nodiscard]] std::vector<std::pair<pj::TopicId, TopicChunk>> flush_all();

 private:
  DataEngine& engine_;
  absl::flat_hash_map<pj::TopicId, TopicChunkBuilder> builders_;
  absl::flat_hash_map<pj::TopicId, std::vector<TopicChunk>> pending_chunks_;

  // Column descriptors cached per topic (needed to recreate builders)
  absl::flat_hash_map<pj::TopicId, std::vector<ColumnDescriptor>> topic_columns_;

  TopicChunkBuilder& get_or_create_builder(pj::TopicId topic_id);

  // Build column descriptors from a type tree
  static std::vector<ColumnDescriptor> build_column_descriptors(const pj::TypeTreeNode& root);

  // Seal current builder and move chunk to pending list
  void auto_seal(pj::TopicId topic_id);
};

}  // namespace pj::engine
