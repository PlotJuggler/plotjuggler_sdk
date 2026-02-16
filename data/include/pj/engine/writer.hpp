#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_tree.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

class DataEngine;  // forward declaration

/// Handle returned by bind_topic_writer for fast-path column access.
struct TopicWriteHandle {
  TopicId topic_id;
  std::vector<FieldId> field_ids;
};

/// Handle for scalar convenience API — single numeric column per topic.
struct ScalarSeriesHandle {
  TopicId topic_id;
  FieldId value_field;
};

class DataWriter {
 public:
  explicit DataWriter(DataEngine& engine);

  // ---- Schema registration (delegates to engine's TypeRegistry) ----
  [[nodiscard]] absl::StatusOr<SchemaId> register_schema(
      std::string schema_name, std::shared_ptr<TypeTreeNode> type_tree);

  // ---- Topic registration ----
  [[nodiscard]] absl::StatusOr<TopicId> register_topic(
      DatasetId dataset_id, TopicDescriptor descriptor);

  // ---- Bind for fast path ----
  [[nodiscard]] absl::StatusOr<TopicWriteHandle> bind_topic_writer(
      TopicId topic_id);

  // ---- Field resolution ----
  [[nodiscard]] absl::StatusOr<FieldId> resolve_field(
      TopicId topic_id, std::string_view field_path);

  // ---- Row-at-a-time append ----
  void begin_row(TopicId topic_id, Timestamp t);
  void finish_row(TopicId topic_id);

  // ---- Set values for current row by column index ----
  void set_float32(TopicId topic_id, std::size_t col_index, float value);
  void set_float64(TopicId topic_id, std::size_t col_index, double value);
  void set_int8(TopicId topic_id, std::size_t col_index, int8_t value);
  void set_int16(TopicId topic_id, std::size_t col_index, int16_t value);
  void set_int32(TopicId topic_id, std::size_t col_index, int32_t value);
  void set_int64(TopicId topic_id, std::size_t col_index, int64_t value);
  void set_uint8(TopicId topic_id, std::size_t col_index, uint8_t value);
  void set_uint16(TopicId topic_id, std::size_t col_index, uint16_t value);
  void set_uint32(TopicId topic_id, std::size_t col_index, uint32_t value);
  void set_uint64(TopicId topic_id, std::size_t col_index, uint64_t value);
  void set_string(TopicId topic_id, std::size_t col_index,
                  std::string_view value);
  void set_bool(TopicId topic_id, std::size_t col_index, bool value);
  void set_null(TopicId topic_id, std::size_t col_index);

  // ---- Scalar convenience API ----
  [[nodiscard]] absl::StatusOr<ScalarSeriesHandle> register_scalar_series(
      DatasetId dataset_id, std::string_view topic_name,
      NumericType value_type);
  void append_scalar(const ScalarSeriesHandle& handle, Timestamp t,
                     NumericValue value);

  // ---- Flush ----
  [[nodiscard]] std::vector<TopicChunk> flush(TopicId topic_id);
  [[nodiscard]] std::vector<std::pair<TopicId, TopicChunk>> flush_all();

 private:
  DataEngine& engine_;
  absl::flat_hash_map<TopicId, TopicChunkBuilder> builders_;
  absl::flat_hash_map<TopicId, std::vector<TopicChunk>> pending_chunks_;

  // Column descriptors cached per topic (needed to recreate builders)
  absl::flat_hash_map<TopicId, std::vector<ColumnDescriptor>>
      topic_columns_;

  TopicChunkBuilder& get_or_create_builder(TopicId topic_id);

  // Build column descriptors from a type tree
  static std::vector<ColumnDescriptor> build_column_descriptors(
      const TypeTreeNode& root);

  // Seal current builder and move chunk to pending list
  void auto_seal(TopicId topic_id);
};

}  // namespace pj::engine
