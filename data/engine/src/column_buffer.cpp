#include "pj/engine/column_buffer.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace pj::engine {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TypedColumnBuffer::TypedColumnBuffer(ColumnDescriptor descriptor) : descriptor_(std::move(descriptor)) {}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const ColumnDescriptor& TypedColumnBuffer::descriptor() const noexcept {
  return descriptor_;
}

std::size_t TypedColumnBuffer::row_count() const noexcept {
  return row_count_;
}

bool TypedColumnBuffer::has_nulls() const noexcept {
  return null_count_ > 0;
}

bool TypedColumnBuffer::is_valid(std::size_t row) const noexcept {
  if (!validity_initialized_) {
    return true;
  }
  return validity_.is_valid(row);
}

// ---------------------------------------------------------------------------
// Underlying buffers
// ---------------------------------------------------------------------------

const RawBuffer& TypedColumnBuffer::value_buffer() const noexcept {
  return values_;
}

const BitVector& TypedColumnBuffer::validity_buffer() const noexcept {
  return validity_;
}

const RawBuffer& TypedColumnBuffer::offsets_buffer() const noexcept {
  return offsets_;
}

// ---------------------------------------------------------------------------
// Validity (lazy initialization)
// ---------------------------------------------------------------------------

void TypedColumnBuffer::ensure_validity_initialized() {
  if (validity_initialized_) {
    return;
  }
  // Initialize bitmap with all existing rows marked valid.
  validity_.init_valid(row_count_);
  validity_initialized_ = true;
}

// ---------------------------------------------------------------------------
// Fixed-size append / read templates
// ---------------------------------------------------------------------------

template <typename T>
void TypedColumnBuffer::append_fixed(T value) {
  values_.append(&value, sizeof(T));
  if (validity_initialized_) {
    // Ensure bitmap has room for this row, then mark valid.
    validity_.ensure_size(row_count_ + 1);
    validity_.set_valid(row_count_);
  }
  ++row_count_;
}

template <typename T>
T TypedColumnBuffer::read_fixed(std::size_t row) const {
  T result{};
  std::memcpy(&result, values_.data() + row * sizeof(T), sizeof(T));
  return result;
}

// ---------------------------------------------------------------------------
// Typed append functions (6 storage types)
// ---------------------------------------------------------------------------

void TypedColumnBuffer::append_float32(float value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kFloat32);
  append_fixed(value);
}

void TypedColumnBuffer::append_float64(double value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kFloat64);
  append_fixed(value);
}

void TypedColumnBuffer::append_int32(int32_t value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kInt32);
  append_fixed(value);
}

void TypedColumnBuffer::append_int64(int64_t value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kInt64);
  append_fixed(value);
}

void TypedColumnBuffer::append_uint64(uint64_t value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kUint64);
  append_fixed(value);
}

void TypedColumnBuffer::append_bool(bool value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kBool);
  const auto byte = static_cast<uint8_t>(value ? 1 : 0);
  append_fixed(byte);
}

void TypedColumnBuffer::append_string(std::string_view value) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kString);
  // Write the initial offset (0) on first row.
  if (row_count_ == 0) {
    const uint32_t zero = 0;
    offsets_.append(&zero, sizeof(zero));
  }

  // Append string bytes to value buffer.
  values_.append(value.data(), value.size());

  // Append new end offset.
  const auto end_offset = static_cast<uint32_t>(values_.size());
  offsets_.append(&end_offset, sizeof(end_offset));

  // Update validity if initialized.
  if (validity_initialized_) {
    validity_.ensure_size(row_count_ + 1);
    validity_.set_valid(row_count_);
  }

  ++row_count_;
}

void TypedColumnBuffer::append_null() {
  ensure_validity_initialized();

  // Ensure bitmap has room for this row.
  validity_.ensure_size(row_count_ + 1);
  validity_.set_null(row_count_);

  // Append zero bytes for the value slot.
  const StorageKind kind = storage_kind_of(descriptor_.logical_type);
  if (kind == StorageKind::kString) {
    // For strings: write the initial offset if this is the first row.
    if (row_count_ == 0) {
      const uint32_t zero = 0;
      offsets_.append(&zero, sizeof(zero));
    }
    // Duplicate the current end offset (no new string data).
    const auto current_offset = static_cast<uint32_t>(values_.size());
    offsets_.append(&current_offset, sizeof(current_offset));
  } else {
    const std::size_t type_size = storage_kind_size(kind);
    // Append type_size zero bytes.
    const uint64_t zero = 0;  // 8 bytes, enough for any fixed type
    values_.append(&zero, type_size);
  }

  ++row_count_;
  ++null_count_;
}

// ---------------------------------------------------------------------------
// Bulk fixed-size append template
// ---------------------------------------------------------------------------

template <typename T>
void TypedColumnBuffer::append_fixed_bulk(Span<const T> data) {
  const std::size_t count = data.size();
  if (count == 0) {
    return;
  }
  values_.reserve(values_.size() + count * sizeof(T));
  values_.append(data.data(), count * sizeof(T));
  if (validity_initialized_) {
    const std::size_t new_total = row_count_ + count;
    validity_.ensure_size(new_total);
    for (std::size_t i = row_count_; i < new_total; ++i) {
      validity_.set_valid(i);
    }
  }
  row_count_ += count;
}

// ---------------------------------------------------------------------------
// Typed bulk append functions (7 storage types)
// ---------------------------------------------------------------------------

void TypedColumnBuffer::append_float32_bulk(Span<const float> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kFloat32);
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_float64_bulk(Span<const double> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kFloat64);
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_int32_bulk(Span<const int32_t> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kInt32);
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_int64_bulk(Span<const int64_t> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kInt64);
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_uint64_bulk(Span<const uint64_t> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kUint64);
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_bool_bulk(Span<const uint8_t> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kBool);
  // Bool stored as uint8_t per element (1 byte per bool, not packed)
  append_fixed_bulk(data);
}

void TypedColumnBuffer::append_strings_bulk(Span<const uint32_t> offsets, Span<const char> data) {
  assert(storage_kind_of(descriptor_.logical_type) == StorageKind::kString);
  if (offsets.empty()) {
    return;
  }
  const std::size_t count = offsets.size() - 1;
  if (count == 0) {
    return;
  }

  // Write the initial offset (0) on first row if buffer is empty
  const uint32_t base_data_offset = static_cast<uint32_t>(values_.size());
  if (row_count_ == 0) {
    const uint32_t zero = 0;
    offsets_.append(&zero, sizeof(zero));
  }

  // Append all string data at once
  const uint32_t total_string_bytes = offsets[count] - offsets[0];
  assert(offsets[0] <= static_cast<uint32_t>(data.size()));
  assert(offsets[count] <= static_cast<uint32_t>(data.size()));
  values_.reserve(values_.size() + total_string_bytes);
  values_.append(data.data() + offsets[0], total_string_bytes);

  // Append adjusted offsets (rebase to our value buffer position)
  const uint32_t src_base = offsets[0];
  for (std::size_t i = 1; i <= count; ++i) {
    const uint32_t adjusted = base_data_offset + (offsets[i] - src_base);
    offsets_.append(&adjusted, sizeof(adjusted));
  }

  // Update validity if initialized
  if (validity_initialized_) {
    const std::size_t new_total = row_count_ + count;
    validity_.ensure_size(new_total);
    for (std::size_t i = row_count_; i < new_total; ++i) {
      validity_.set_valid(i);
    }
  }

  row_count_ += count;
}

void TypedColumnBuffer::append_validity_bulk(BitSpan validity) {
  const std::size_t count = validity.bit_length;
  if (count == 0) {
    return;
  }
  ensure_validity_initialized();

  // The validity bitmap covers the last `count` rows that were just appended.
  // row_count_ already includes them, so the range is
  // [row_count_ - count, row_count_).
  const std::size_t start_row = row_count_ - count;
  validity_.ensure_size(row_count_);

  for (std::size_t i = 0; i < count; ++i) {
    const bool valid = validity.test(i);
    if (valid) {
      validity_.set_valid(start_row + i);
    } else {
      validity_.set_null(start_row + i);
      ++null_count_;
    }
  }
}

// ---------------------------------------------------------------------------
// Typed read functions (6 storage types)
// ---------------------------------------------------------------------------

float TypedColumnBuffer::read_float32(std::size_t row) const {
  return read_fixed<float>(row);
}

double TypedColumnBuffer::read_float64(std::size_t row) const {
  return read_fixed<double>(row);
}

int32_t TypedColumnBuffer::read_int32(std::size_t row) const {
  return read_fixed<int32_t>(row);
}

int64_t TypedColumnBuffer::read_int64(std::size_t row) const {
  return read_fixed<int64_t>(row);
}

uint64_t TypedColumnBuffer::read_uint64(std::size_t row) const {
  return read_fixed<uint64_t>(row);
}

bool TypedColumnBuffer::read_bool(std::size_t row) const {
  return read_fixed<uint8_t>(row) != 0;
}

std::string_view TypedColumnBuffer::read_string(std::size_t row) const {
  uint32_t start_offset = 0;
  uint32_t end_offset = 0;
  std::memcpy(&start_offset, offsets_.data() + row * sizeof(uint32_t), sizeof(uint32_t));
  std::memcpy(&end_offset, offsets_.data() + (row + 1) * sizeof(uint32_t), sizeof(uint32_t));
  return {reinterpret_cast<const char*>(values_.data()) + start_offset, end_offset - start_offset};
}

bool TypedColumnBuffer::is_null(std::size_t row) const {
  if (!validity_initialized_) {
    return false;
  }
  return !validity_.is_valid(row);
}

// ---------------------------------------------------------------------------
// read_as_double
// ---------------------------------------------------------------------------

double TypedColumnBuffer::read_as_double(std::size_t row) const {
  switch (storage_kind_of(descriptor_.logical_type)) {
    case StorageKind::kFloat32:
      return static_cast<double>(read_float32(row));
    case StorageKind::kFloat64:
      return read_float64(row);
    case StorageKind::kInt32:
      return static_cast<double>(read_int32(row));
    case StorageKind::kInt64:
      return static_cast<double>(read_int64(row));
    case StorageKind::kUint64:
      return static_cast<double>(read_uint64(row));
    case StorageKind::kBool:
      return read_bool(row) ? 1.0 : 0.0;
    case StorageKind::kString:
      return std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();  // unreachable
}

}  // namespace pj::engine
