#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "pj_base/span.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/buffer.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/encoding.hpp"

namespace PJ {

// Import base types into engine namespace
using PJ::BitSpan;
using PJ::ChunkId;
using PJ::kInvalidChunkId;
using PJ::SchemaId;
using PJ::Span;
using PJ::Timestamp;
using PJ::TopicId;

struct ColumnStats {
  /// Number of null rows in this column within the chunk.
  uint32_t null_count = 0;
  /// Number of value runs (for compression heuristics).
  uint32_t run_count = 0;
  /// True if all non-null values are equal.
  bool is_constant = true;
  /// Minimum non-null numeric value, if applicable.
  std::optional<double> min_value;
  /// Maximum non-null numeric value, if applicable.
  std::optional<double> max_value;
};

/// Per-chunk aggregate statistics.
struct ChunkStats {
  /// Minimum timestamp in chunk.
  Timestamp t_min = std::numeric_limits<Timestamp>::max();
  /// Maximum timestamp in chunk.
  Timestamp t_max = std::numeric_limits<Timestamp>::min();
  /// Number of rows in chunk.
  uint32_t row_count = 0;
  /// Per-column statistics aligned to schema columns.
  std::vector<ColumnStats> column_stats;
};

// Immutable sealed chunk - the result of sealing a TopicChunkBuilder
struct TopicChunk {
  /// Chunk identifier.
  ChunkId id = 0;
  /// Owning topic id.
  TopicId topic_id = 0;
  /// Schema version used when chunk was produced.
  SchemaId schema_version = 0;
  /// Chunk-level and column-level stats.
  ChunkStats stats;

  // Raw timestamp column (one int64 per row)
  /// One timestamp per row.
  std::vector<Timestamp> timestamps;

  // Per-column encoded data (parallel vectors, indexed by col_index).
  // Intentionally SoA rather than AoS: the chunk is immutable after seal(),
  // so misalignment is impossible, and SoA is cache-friendly for column scans.
  /// Raw bytes for kRaw columns.
  std::vector<RawBuffer> encoded_columns;  // Raw typed data for numeric cols
  /// Encoding kind for each column.
  std::vector<EncodingType> column_encodings;  // What encoding each column uses
  /// Validity bitmaps per column (empty means all valid).
  std::vector<BitVector> validity_bitmaps;  // Per-column (empty if no nulls)
  /// Column descriptors aligned with encoded columns.
  std::vector<ColumnDescriptor> column_descriptors;  // Metadata about each column

  // Per-column encoding data (variant: monostate for kRaw, or specific encoding)
  /// Encoding-specific payload per column.
  std::vector<encoding::ColumnEncodingData> encoding_data;

  // Decode helpers
  /// Read timestamp at row index.
  [[nodiscard]] Timestamp readTimestamp(std::size_t row) const;

  /// Bulk-read timestamps into `out` starting at `row_start`.
  void readTimestamps(Span<Timestamp> out, std::size_t row_start) const;

  /// Read numeric value at `col_index,row` as double.
  [[nodiscard]] double readNumericAsDouble(std::size_t col_index, std::size_t row) const;

  /// Read numeric value at `col_index,row` as int64_t.
  [[nodiscard]] int64_t readNumericAsInt64(std::size_t col_index, std::size_t row) const;

  /// Read numeric value at `col_index,row` as uint64_t.
  [[nodiscard]] uint64_t readNumericAsUint64(std::size_t col_index, std::size_t row) const;

  /// Read string value at `col_index,row`.
  [[nodiscard]] std::string_view readString(std::size_t col_index, std::size_t row) const;

  /// Read boolean value at `col_index,row`.
  [[nodiscard]] bool readBool(std::size_t col_index, std::size_t row) const;

  /// Return true if value at `col_index,row` is null.
  [[nodiscard]] bool isNull(std::size_t col_index, std::size_t row) const;

  // Bulk read: switch on type once, then tight inner loop.
  // For kBool/kString columns, fills NaN.
  /// Decode a numeric range into `out` starting at `row_start`.
  void readColumnAsDoubles(std::size_t col_index, Span<double> out, std::size_t row_start) const;
};

// Builder for constructing a chunk row by row
class TopicChunkBuilder {
 public:
  /// Create a builder for one topic/schema pair.
  TopicChunkBuilder(TopicId topic_id, SchemaId schema_id, std::vector<ColumnDescriptor> columns, uint32_t max_rows);

  // Start a new row with the given timestamp
  /// Begin a new row at `timestamp`.
  void beginRow(Timestamp timestamp);

  // Set values for the current row (by column index) — 7 storage types
  /// Set float32 value for current row.
  void setFloat32(std::size_t col_index, float value);

  /// Set float64 value for current row.
  void setFloat64(std::size_t col_index, double value);

  /// Set int32 value for current row.
  void setInt32(std::size_t col_index, int32_t value);

  /// Set int64 value for current row.
  void setInt64(std::size_t col_index, int64_t value);

  /// Set uint64 value for current row.
  void setUint64(std::size_t col_index, uint64_t value);

  /// Set bool value for current row.
  void setBool(std::size_t col_index, bool value);

  /// Set string value for current row.
  void setString(std::size_t col_index, std::string_view value);

  /// Mark value as null for current row.
  void setNull(std::size_t col_index);

  // Finalize the current row (append all columns)
  /// Finalize current row, auto-filling unset columns with null.
  void finishRow();

  // ---- Bulk column append ----
  // Call append_timestamps first, then append_column_* for each column,
  // then append_column_validity for columns with nulls, then finish_bulk_append.
  // Stats are computed in finish_bulk_append using the column's validity bitmap.
  /// Append a contiguous timestamp batch.
  void appendTimestamps(Span<const Timestamp> timestamps);

  /// Append one float32 column batch.
  void appendColumnFloat32(std::size_t col_index, Span<const float> data);

  /// Append one float64 column batch.
  void appendColumnFloat64(std::size_t col_index, Span<const double> data);

  /// Append one int32 column batch.
  void appendColumnInt32(std::size_t col_index, Span<const int32_t> data);

  /// Append one int64 column batch.
  void appendColumnInt64(std::size_t col_index, Span<const int64_t> data);

  /// Append one uint64 column batch.
  void appendColumnUint64(std::size_t col_index, Span<const uint64_t> data);

  /// Append one bool-byte (0/1) column batch.
  void appendColumnBool(std::size_t col_index, Span<const uint8_t> data);

  /// Append one string column batch from offsets+data views.
  void appendColumnStrings(std::size_t col_index, Span<const uint32_t> offsets, Span<const char> data);

  /// Append validity bits for last appended rows of this column.
  void appendColumnValidity(std::size_t col_index, BitSpan validity);

  /// Finalize pending bulk append and compute stats.
  void finishBulkAppend();

  /// Remaining row capacity before auto-seal.
  [[nodiscard]] uint32_t remainingCapacity() const noexcept;

  /// True if no more rows can be appended.
  [[nodiscard]] bool isFull() const noexcept;

  /// Number of finalized rows.
  [[nodiscard]] uint32_t rowCount() const noexcept;

  /// True if beginRow() has been called but finishRow() has not yet been called.
  [[nodiscard]] bool isRowInProgress() const noexcept;

  /// Current chunk statistics.
  [[nodiscard]] const ChunkStats& stats() const noexcept;

  /// Last appended timestamp.
  [[nodiscard]] Timestamp lastTimestamp() const noexcept;

  // Seal: finalize stats, apply encodings, produce immutable TopicChunk
  /// Seal builder into immutable chunk.
  [[nodiscard]] TopicChunk seal();

 private:
  TopicId topic_id_;
  SchemaId schema_id_;
  uint32_t max_rows_;
  static inline std::atomic<ChunkId> next_chunk_id_{1};  // monotonic counter

  std::vector<Timestamp> timestamps_;
  std::vector<TypedColumnBuffer> columns_;
  std::vector<ColumnDescriptor> column_descriptors_;
  ChunkStats stats_;

  // Track per-row state during begin_row/finish_row
  bool row_in_progress_ = false;
  Timestamp current_timestamp_ = 0;

  Timestamp last_timestamp_ = std::numeric_limits<Timestamp>::min();
  std::vector<double> last_column_values_;
  std::size_t bulk_pending_rows_ = 0;  // rows added via bulk but not yet finished

  void updateColumnStats(std::size_t col_index, double value);

  // Bulk stats computation: single pass over column buffer data.
  // Called by finishBulkAppend() after both data and validity are set.
  // Reads from column buffer and skips null positions via validity bitmap.
  void computeBulkNumericStats(std::size_t col_index, StorageKind kind, std::size_t first_row, std::size_t count);

  void computeBulkStringStats(std::size_t col_index, std::size_t first_row, std::size_t count);
};

}  // namespace PJ
