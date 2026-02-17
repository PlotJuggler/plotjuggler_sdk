#include "pj/engine/chunk.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <variant>

#include "pj/base/assert.hpp"

namespace pj::engine {

namespace {

// Read a raw numeric value from a buffer as double, dispatching on StorageKind.
[[nodiscard]] double read_raw_as_double(const RawBuffer& buf, StorageKind kind, std::size_t row) {
  const std::size_t elem_size = storage_kind_size(kind);
  const uint8_t* ptr = buf.data() + row * elem_size;

  auto load = [&]<typename T>(const T* /*tag*/) -> double {
    T v{};
    std::memcpy(&v, ptr, sizeof(v));
    return static_cast<double>(v);
  };

  switch (kind) {
    case StorageKind::kFloat32:
      return load(static_cast<const float*>(nullptr));
    case StorageKind::kFloat64:
      return load(static_cast<const double*>(nullptr));
    case StorageKind::kInt32:
      return load(static_cast<const int32_t*>(nullptr));
    case StorageKind::kInt64:
      return load(static_cast<const int64_t*>(nullptr));
    case StorageKind::kUint64:
      return load(static_cast<const uint64_t*>(nullptr));
    case StorageKind::kBool:
    case StorageKind::kString:
      break;
  }
  return 0.0;  // unreachable for numeric types
}

}  // namespace

// ===========================================================================
// TopicChunkBuilder
// ===========================================================================

TopicChunkBuilder::TopicChunkBuilder(
    TopicId topic_id, SchemaId schema_id, std::vector<ColumnDescriptor> columns, uint32_t max_rows)
    : topic_id_(topic_id), schema_id_(schema_id), max_rows_(max_rows), column_descriptors_(std::move(columns)) {
  columns_.reserve(column_descriptors_.size());
  for (const auto& desc : column_descriptors_) {
    columns_.emplace_back(desc);
  }
  stats_.column_stats.resize(column_descriptors_.size());
  last_column_values_.resize(column_descriptors_.size(), 0.0);
}

void TopicChunkBuilder::begin_row(Timestamp timestamp) {
  PJ_ASSERT(!row_in_progress_, "begin_row called while row already in progress");
  PJ_ASSERT(timestamp >= last_timestamp_, "timestamps must be monotonically non-decreasing");
  row_in_progress_ = true;
  current_timestamp_ = timestamp;
  last_timestamp_ = timestamp;
  stats_.t_min = std::min(stats_.t_min, timestamp);
  stats_.t_max = std::max(stats_.t_max, timestamp);
}

// ---------------------------------------------------------------------------
// set_* methods (6 storage types)
// ---------------------------------------------------------------------------

void TopicChunkBuilder::set_float32(std::size_t col_index, float value) {
  PJ_ASSERT(row_in_progress_, "set_float32 called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_float32(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_float64(std::size_t col_index, double value) {
  PJ_ASSERT(row_in_progress_, "set_float64 called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_float64(value);
  update_column_stats(col_index, value);
}

void TopicChunkBuilder::set_int32(std::size_t col_index, int32_t value) {
  PJ_ASSERT(row_in_progress_, "set_int32 called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_int32(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_int64(std::size_t col_index, int64_t value) {
  PJ_ASSERT(row_in_progress_, "set_int64 called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_int64(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_uint64(std::size_t col_index, uint64_t value) {
  PJ_ASSERT(row_in_progress_, "set_uint64 called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_uint64(value);
  update_column_stats(col_index, static_cast<double>(value));
}

void TopicChunkBuilder::set_bool(std::size_t col_index, bool value) {
  PJ_ASSERT(row_in_progress_, "set_bool called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_bool(value);
  const double dval = value ? 1.0 : 0.0;
  update_column_stats(col_index, dval);
}

void TopicChunkBuilder::set_string(std::size_t col_index, std::string_view value) {
  PJ_ASSERT(row_in_progress_, "set_string called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_string(value);
  // For strings we don't track numeric min/max, but we do track run_count
  // and is_constant.
  auto& cs = stats_.column_stats[col_index];
  const std::size_t current_row = columns_[col_index].row_count() - 1;
  if (current_row == 0) {
    cs.run_count = 1;
    // is_constant stays true
  } else {
    // Compare with previous string value
    std::string_view prev = columns_[col_index].read_string(current_row - 1);
    if (value != prev) {
      cs.is_constant = false;
      cs.run_count++;
    }
  }
}

void TopicChunkBuilder::set_null(std::size_t col_index) {
  PJ_ASSERT(row_in_progress_, "set_null called without begin_row");
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_null();
  stats_.column_stats[col_index].null_count++;
}

void TopicChunkBuilder::finish_row() {
  PJ_ASSERT(row_in_progress_, "finish_row called without begin_row");
  // Auto-fill unset columns with null to maintain column alignment.
  const std::size_t expected = row_count() + 1;
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].row_count() < expected) {
      columns_[i].append_null();
      stats_.column_stats[i].null_count++;
    }
  }

  timestamps_.push_back(current_timestamp_);
  stats_.row_count++;
  row_in_progress_ = false;
}

// ---------------------------------------------------------------------------
// Bulk column append
// ---------------------------------------------------------------------------

void TopicChunkBuilder::append_timestamps(Span<const Timestamp> timestamps) {
  PJ_ASSERT(!row_in_progress_, "append_timestamps called while row in progress");
  const std::size_t count = timestamps.size();
  if (count == 0) {
    return;
  }

  PJ_ASSERT(timestamps[0] >= last_timestamp_, "timestamps must be monotonically non-decreasing");

  timestamps_.reserve(timestamps_.size() + count);
  timestamps_.insert(timestamps_.end(), timestamps.begin(), timestamps.end());

  // Update timestamp stats
  stats_.t_min = std::min(stats_.t_min, timestamps[0]);
  stats_.t_max = std::max(stats_.t_max, timestamps[count - 1]);
  last_timestamp_ = timestamps[count - 1];

  bulk_pending_rows_ = count;
}

void TopicChunkBuilder::append_column_float32(std::size_t col_index, Span<const float> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_float32_bulk(data);
}

void TopicChunkBuilder::append_column_float64(std::size_t col_index, Span<const double> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_float64_bulk(data);
}

void TopicChunkBuilder::append_column_int32(std::size_t col_index, Span<const int32_t> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_int32_bulk(data);
}

void TopicChunkBuilder::append_column_int64(std::size_t col_index, Span<const int64_t> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_int64_bulk(data);
}

void TopicChunkBuilder::append_column_uint64(std::size_t col_index, Span<const uint64_t> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_uint64_bulk(data);
}

void TopicChunkBuilder::append_column_bool(std::size_t col_index, Span<const uint8_t> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_bool_bulk(data);
}

void TopicChunkBuilder::append_column_strings(
    std::size_t col_index, Span<const uint32_t> offsets, Span<const char> data) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_strings_bulk(offsets, data);
}

void TopicChunkBuilder::append_column_validity(std::size_t col_index, BitSpan validity) {
  PJ_ASSERT(col_index < columns_.size(), "col_index out of bounds");
  columns_[col_index].append_validity_bulk(validity);

  // Note: null_count is computed in finish_bulk_append() via stats helpers.
}

void TopicChunkBuilder::finish_bulk_append() {
  PJ_ASSERT(!row_in_progress_, "finish_bulk_append called while row in progress");
  if (bulk_pending_rows_ == 0) {
    return;
  }

  const std::size_t count = bulk_pending_rows_;

  // Compute stats for all columns, now that both data and validity are set.
  for (std::size_t col = 0; col < columns_.size(); ++col) {
    const std::size_t first_row = columns_[col].row_count() - count;
    const auto kind = storage_kind_of(column_descriptors_[col].logical_type);

    if (kind == StorageKind::kString) {
      compute_bulk_string_stats(col, first_row, count);
    } else {
      compute_bulk_numeric_stats(col, kind, first_row, count);
    }
  }

  stats_.row_count += static_cast<uint32_t>(count);
  bulk_pending_rows_ = 0;
}

uint32_t TopicChunkBuilder::remaining_capacity() const noexcept {
  return max_rows_ - row_count();
}

// ---------------------------------------------------------------------------
// Bulk stats helpers — read from column buffer, skip nulls via validity bitmap
// ---------------------------------------------------------------------------

void TopicChunkBuilder::compute_bulk_numeric_stats(
    std::size_t col_index, StorageKind kind, std::size_t first_row, std::size_t count) {
  if (count == 0) {
    return;
  }

  auto& cs = stats_.column_stats[col_index];
  const auto& col = columns_[col_index];
  const bool has_validity = col.has_nulls();

  auto process = [&]<typename T>(const T* /*tag*/) {
    const auto* buf = reinterpret_cast<const T*>(col.value_buffer().data());
    double local_min = cs.min_value.value_or(std::numeric_limits<double>::max());
    double local_max = cs.max_value.value_or(std::numeric_limits<double>::lowest());
    double prev = last_column_values_[col_index];
    bool had_valid = cs.run_count > 0;

    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t row = first_row + i;
      if (has_validity && !col.is_valid(row)) {
        cs.null_count++;
        continue;
      }
      const double v = static_cast<double>(buf[row]);
      if (v < local_min) {
        local_min = v;
      }
      if (v > local_max) {
        local_max = v;
      }
      if (!had_valid) {
        cs.run_count = 1;
        had_valid = true;
      } else if (v != prev) {
        cs.is_constant = false;
        cs.run_count++;
      }
      prev = v;
    }
    if (had_valid) {
      cs.min_value = local_min;
      cs.max_value = local_max;
    }
    last_column_values_[col_index] = prev;
  };

  switch (kind) {
    case StorageKind::kFloat32:
      process(static_cast<const float*>(nullptr));
      break;
    case StorageKind::kFloat64:
      process(static_cast<const double*>(nullptr));
      break;
    case StorageKind::kInt32:
      process(static_cast<const int32_t*>(nullptr));
      break;
    case StorageKind::kInt64:
      process(static_cast<const int64_t*>(nullptr));
      break;
    case StorageKind::kUint64:
      process(static_cast<const uint64_t*>(nullptr));
      break;
    case StorageKind::kBool: {
      const auto* buf = col.value_buffer().data();
      double prev = last_column_values_[col_index];
      double local_min = cs.min_value.value_or(std::numeric_limits<double>::max());
      double local_max = cs.max_value.value_or(std::numeric_limits<double>::lowest());
      bool had_valid = cs.run_count > 0;
      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t row = first_row + i;
        if (has_validity && !col.is_valid(row)) {
          cs.null_count++;
          continue;
        }
        const double v = buf[row] ? 1.0 : 0.0;
        if (v < local_min) {
          local_min = v;
        }
        if (v > local_max) {
          local_max = v;
        }
        if (!had_valid) {
          cs.run_count = 1;
          had_valid = true;
        } else if (v != prev) {
          cs.is_constant = false;
          cs.run_count++;
        }
        prev = v;
      }
      if (had_valid) {
        cs.min_value = local_min;
        cs.max_value = local_max;
      }
      last_column_values_[col_index] = prev;
      break;
    }
    case StorageKind::kString:
      break;  // handled by compute_bulk_string_stats
  }
}

void TopicChunkBuilder::compute_bulk_string_stats(std::size_t col_index, std::size_t first_row, std::size_t count) {
  if (count == 0) {
    return;
  }

  auto& cs = stats_.column_stats[col_index];
  const auto& col = columns_[col_index];
  const bool has_validity = col.has_nulls();

  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t row = first_row + i;
    if (has_validity && !col.is_valid(row)) {
      cs.null_count++;
      continue;
    }
    if (cs.run_count == 0) {
      cs.run_count = 1;
    } else {
      std::string_view current = col.read_string(row);
      // Find previous non-null row to compare
      bool found_prev = false;
      for (std::size_t j = row; j > 0; --j) {
        if (!has_validity || col.is_valid(j - 1)) {
          if (current != col.read_string(j - 1)) {
            cs.is_constant = false;
            cs.run_count++;
          }
          found_prev = true;
          break;
        }
      }
      if (!found_prev) {
        cs.run_count = 1;
      }
    }
  }
}

bool TopicChunkBuilder::is_full() const noexcept {
  return row_count() >= max_rows_;
}

uint32_t TopicChunkBuilder::row_count() const noexcept {
  return stats_.row_count;
}

const ChunkStats& TopicChunkBuilder::stats() const noexcept {
  return stats_;
}

Timestamp TopicChunkBuilder::last_timestamp() const noexcept {
  return last_timestamp_;
}

void TopicChunkBuilder::update_column_stats(std::size_t col_index, double value) {
  auto& cs = stats_.column_stats[col_index];
  const std::size_t current_row = columns_[col_index].row_count() - 1;

  // Update min/max
  if (!cs.min_value.has_value() || value < *cs.min_value) {
    cs.min_value = value;
  }
  if (!cs.max_value.has_value() || value > *cs.max_value) {
    cs.max_value = value;
  }

  // Update run_count / is_constant using cached last value
  if (current_row == 0) {
    cs.run_count = 1;
  } else {
    if (value != last_column_values_[col_index]) {
      cs.is_constant = false;
      cs.run_count++;
    }
  }
  last_column_values_[col_index] = value;
}

// ---------------------------------------------------------------------------
// seal
// ---------------------------------------------------------------------------

TopicChunk TopicChunkBuilder::seal() {
  TopicChunk chunk;
  chunk.id = next_chunk_id_++;
  chunk.topic_id = topic_id_;
  chunk.schema_version = schema_id_;
  chunk.stats = stats_;
  chunk.column_descriptors = column_descriptors_;

  // Store raw timestamps
  chunk.timestamps = std::move(timestamps_);

  const std::size_t num_cols = columns_.size();
  chunk.encoded_columns.resize(num_cols);
  chunk.column_encodings.resize(num_cols);
  chunk.validity_bitmaps.resize(num_cols);
  chunk.encoding_data.resize(num_cols);

  for (std::size_t i = 0; i < num_cols; ++i) {
    const auto& col = columns_[i];
    const StorageKind kind = storage_kind_of(column_descriptors_[i].logical_type);
    const auto& cs = stats_.column_stats[i];

    switch (kind) {
      case StorageKind::kString: {
        // Dictionary-encode the string column
        chunk.column_encodings[i] = EncodingType::kDictionary;
        chunk.encoding_data[i] = encoding::dictionary_encode_strings(
            Span<const uint8_t>(col.offsets_buffer().data(), col.offsets_buffer().size()),
            Span<const uint8_t>(col.value_buffer().data(), col.value_buffer().size()), col.row_count());
        break;
      }
      case StorageKind::kBool: {
        if (cs.is_constant && col.row_count() > 0) {
          chunk.column_encodings[i] = EncodingType::kConstant;
          chunk.encoding_data[i] = encoding::constant_encode(
              Span<const uint8_t>(col.value_buffer().data(), col.value_buffer().size()), kind, col.row_count());
        } else {
          chunk.column_encodings[i] = EncodingType::kPackedBool;
          chunk.encoding_data[i] =
              encoding::pack_bools(Span<const uint8_t>(col.value_buffer().data(), col.row_count()));
        }
        break;
      }
      case StorageKind::kInt32:
      case StorageKind::kInt64: {
        // Signed integers: try constant, FOR, or raw
        if (cs.is_constant && col.row_count() > 0) {
          chunk.column_encodings[i] = EncodingType::kConstant;
          chunk.encoding_data[i] = encoding::constant_encode(
              Span<const uint8_t>(col.value_buffer().data(), col.value_buffer().size()), kind, col.row_count());
        } else if (cs.min_value.has_value() && cs.max_value.has_value()) {
          const auto min_val = static_cast<int64_t>(*cs.min_value);
          const auto max_val = static_cast<int64_t>(*cs.max_value);
          const auto range = static_cast<uint64_t>(max_val - min_val);
          const uint8_t ob = encoding::offset_bytes_for(range);

          if (ob < storage_kind_size(kind)) {
            chunk.column_encodings[i] = EncodingType::kFrameOfReference;
            chunk.encoding_data[i] = encoding::for_encode(
                Span<const uint8_t>(col.value_buffer().data(), col.value_buffer().size()), kind, col.row_count(),
                min_val, max_val);
          } else {
            chunk.column_encodings[i] = EncodingType::kRaw;
            chunk.encoding_data[i] = std::monostate{};
            chunk.encoded_columns[i].append(col.value_buffer().data(), col.value_buffer().size());
          }
        } else {
          chunk.column_encodings[i] = EncodingType::kRaw;
          chunk.encoding_data[i] = std::monostate{};
          chunk.encoded_columns[i].append(col.value_buffer().data(), col.value_buffer().size());
        }
        break;
      }
      default: {
        // kFloat32, kFloat64, kUint64: try constant, else raw
        if (cs.is_constant && col.row_count() > 0) {
          chunk.column_encodings[i] = EncodingType::kConstant;
          chunk.encoding_data[i] = encoding::constant_encode(
              Span<const uint8_t>(col.value_buffer().data(), col.value_buffer().size()), kind, col.row_count());
        } else {
          chunk.column_encodings[i] = EncodingType::kRaw;
          chunk.encoding_data[i] = std::monostate{};
          chunk.encoded_columns[i].append(col.value_buffer().data(), col.value_buffer().size());
        }
        break;
      }
    }

    // Copy validity bitmap if the column has nulls
    if (col.has_nulls()) {
      chunk.validity_bitmaps[i].assign_bytes(
          Span<const uint8_t>(col.validity_buffer().data(), col.validity_buffer().size_bytes()),
          col.validity_buffer().size_bits());
    }
  }

  return chunk;
}

// ===========================================================================
// TopicChunk decode helpers
// ===========================================================================

Timestamp TopicChunk::read_timestamp(std::size_t row) const {
  return timestamps[row];
}

void TopicChunk::read_timestamps(Span<Timestamp> out, std::size_t row_start) const {
  std::memcpy(out.data(), timestamps.data() + row_start, out.size() * sizeof(Timestamp));
}

double TopicChunk::read_numeric_as_double(std::size_t col_index, std::size_t row) const {
  switch (column_encodings[col_index]) {
    case EncodingType::kConstant:
      return encoding::constant_decode_as_double(std::get<encoding::ConstantEncoded>(encoding_data[col_index]));
    case EncodingType::kFrameOfReference:
      return encoding::for_decode_one_as_double(
          std::get<encoding::FrameOfReferenceEncoded>(encoding_data[col_index]), row);
    case EncodingType::kRaw: {
      const StorageKind kind = storage_kind_of(column_descriptors[col_index].logical_type);
      return read_raw_as_double(encoded_columns[col_index], kind, row);
    }
    default:
      return 0.0;
  }
}

std::string_view TopicChunk::read_string(std::size_t col_index, std::size_t row) const {
  return encoding::dictionary_lookup(std::get<encoding::DictionaryEncoded>(encoding_data[col_index]), row);
}

bool TopicChunk::read_bool(std::size_t col_index, std::size_t row) const {
  if (column_encodings[col_index] == EncodingType::kConstant) {
    const auto& enc = std::get<encoding::ConstantEncoded>(encoding_data[col_index]);
    uint8_t v = 0;
    std::memcpy(&v, enc.value_bytes.data(), sizeof(v));
    return v != 0;
  }
  return encoding::unpack_bool(std::get<encoding::PackedBools>(encoding_data[col_index]), row);
}

bool TopicChunk::is_null(std::size_t col_index, std::size_t row) const {
  if (validity_bitmaps[col_index].empty()) {
    return false;
  }
  return !validity_bitmaps[col_index].is_valid(row);
}

void TopicChunk::read_column_as_doubles(std::size_t col_index, Span<double> out, std::size_t row_start) const {
  const std::size_t count = out.size();
  switch (column_encodings[col_index]) {
    case EncodingType::kConstant: {
      const double val =
          encoding::constant_decode_as_double(std::get<encoding::ConstantEncoded>(encoding_data[col_index]));
      std::fill(out.begin(), out.end(), val);
      return;
    }
    case EncodingType::kFrameOfReference: {
      encoding::for_decode_range_as_doubles(
          std::get<encoding::FrameOfReferenceEncoded>(encoding_data[col_index]), out, row_start);
      return;
    }
    case EncodingType::kRaw:
      break;  // fall through to raw path below
    default: {
      // kBool, kString, kDictionary, kPackedBool -> NaN
      const double nan = std::numeric_limits<double>::quiet_NaN();
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = nan;
      }
      return;
    }
  }

  // Raw path: type dispatch once, tight inner loop
  const StorageKind kind = storage_kind_of(column_descriptors[col_index].logical_type);
  const uint8_t* base = encoded_columns[col_index].data();

  auto convert = [&]<typename T>(const T* /*tag*/) {
    const auto* src = reinterpret_cast<const T*>(base) + row_start;
    for (std::size_t i = 0; i < count; ++i) {
      out[i] = static_cast<double>(src[i]);
    }
  };

  switch (kind) {
    case StorageKind::kFloat32:
      return convert(static_cast<const float*>(nullptr));
    case StorageKind::kFloat64:
      return convert(static_cast<const double*>(nullptr));
    case StorageKind::kInt32:
      return convert(static_cast<const int32_t*>(nullptr));
    case StorageKind::kInt64:
      return convert(static_cast<const int64_t*>(nullptr));
    case StorageKind::kUint64:
      return convert(static_cast<const uint64_t*>(nullptr));
    case StorageKind::kBool:
    case StorageKind::kString: {
      const double nan = std::numeric_limits<double>::quiet_NaN();
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = nan;
      }
      return;
    }
  }
}

}  // namespace pj::engine
