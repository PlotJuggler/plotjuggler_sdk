#include "pj/engine/column_buffer.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace pj::engine {

namespace {

// Returns the byte size of a fixed-size PrimitiveType.
// For kString, returns 0 (variable-length).
[[nodiscard]] constexpr std::size_t primitive_type_fixed_size(
    PrimitiveType type) noexcept {
  switch (type) {
    case PrimitiveType::kFloat32: return sizeof(float);
    case PrimitiveType::kFloat64: return sizeof(double);
    case PrimitiveType::kInt8:    return sizeof(int8_t);
    case PrimitiveType::kInt16:   return sizeof(int16_t);
    case PrimitiveType::kInt32:   return sizeof(int32_t);
    case PrimitiveType::kInt64:   return sizeof(int64_t);
    case PrimitiveType::kUint8:   return sizeof(uint8_t);
    case PrimitiveType::kUint16:  return sizeof(uint16_t);
    case PrimitiveType::kUint32:  return sizeof(uint32_t);
    case PrimitiveType::kUint64:  return sizeof(uint64_t);
    case PrimitiveType::kBool:    return sizeof(uint8_t);
    case PrimitiveType::kString:  return 0;
  }
  return 0;  // unreachable
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TypedColumnBuffer::TypedColumnBuffer(ColumnDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {}

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

// ---------------------------------------------------------------------------
// Underlying buffers
// ---------------------------------------------------------------------------

const RawBuffer& TypedColumnBuffer::value_buffer() const noexcept {
  return values_;
}

const RawBuffer& TypedColumnBuffer::validity_buffer() const noexcept {
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
  validity_bitmap::init(validity_, row_count_);
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
    const std::size_t needed =
        validity_bitmap::bytes_for_bits(row_count_ + 1);
    if (validity_.size() < needed) {
      validity_.resize(needed);
      // New byte is 0 by default from resize; we will set the bit below.
    }
    validity_bitmap::set_valid(validity_, row_count_);
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
// Typed append functions
// ---------------------------------------------------------------------------

void TypedColumnBuffer::append_float32(float value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_float64(double value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_int8(int8_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_int16(int16_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_int32(int32_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_int64(int64_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_uint8(uint8_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_uint16(uint16_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_uint32(uint32_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_uint64(uint64_t value) {
  append_fixed(value);
}

void TypedColumnBuffer::append_bool(bool value) {
  const auto byte = static_cast<uint8_t>(value ? 1 : 0);
  append_fixed(byte);
}

void TypedColumnBuffer::append_string(std::string_view value) {
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
    const std::size_t needed =
        validity_bitmap::bytes_for_bits(row_count_ + 1);
    if (validity_.size() < needed) {
      validity_.resize(needed);
    }
    validity_bitmap::set_valid(validity_, row_count_);
  }

  ++row_count_;
}

void TypedColumnBuffer::append_null() {
  ensure_validity_initialized();

  // Ensure bitmap has room for this row.
  const std::size_t needed = validity_bitmap::bytes_for_bits(row_count_ + 1);
  if (validity_.size() < needed) {
    validity_.resize(needed);
  }
  validity_bitmap::set_null(validity_, row_count_);

  // Append zero bytes for the value slot.
  if (descriptor_.logical_type == PrimitiveType::kString) {
    // For strings: write the initial offset if this is the first row.
    if (row_count_ == 0) {
      const uint32_t zero = 0;
      offsets_.append(&zero, sizeof(zero));
    }
    // Duplicate the current end offset (no new string data).
    const auto current_offset = static_cast<uint32_t>(values_.size());
    offsets_.append(&current_offset, sizeof(current_offset));
  } else {
    const std::size_t type_size =
        primitive_type_fixed_size(descriptor_.logical_type);
    // Append type_size zero bytes.
    const uint64_t zero = 0;  // 8 bytes, enough for any fixed type
    values_.append(&zero, type_size);
  }

  ++row_count_;
  ++null_count_;
}

// ---------------------------------------------------------------------------
// Typed read functions
// ---------------------------------------------------------------------------

float TypedColumnBuffer::read_float32(std::size_t row) const {
  return read_fixed<float>(row);
}

double TypedColumnBuffer::read_float64(std::size_t row) const {
  return read_fixed<double>(row);
}

int8_t TypedColumnBuffer::read_int8(std::size_t row) const {
  return read_fixed<int8_t>(row);
}

int16_t TypedColumnBuffer::read_int16(std::size_t row) const {
  return read_fixed<int16_t>(row);
}

int32_t TypedColumnBuffer::read_int32(std::size_t row) const {
  return read_fixed<int32_t>(row);
}

int64_t TypedColumnBuffer::read_int64(std::size_t row) const {
  return read_fixed<int64_t>(row);
}

uint8_t TypedColumnBuffer::read_uint8(std::size_t row) const {
  return read_fixed<uint8_t>(row);
}

uint16_t TypedColumnBuffer::read_uint16(std::size_t row) const {
  return read_fixed<uint16_t>(row);
}

uint32_t TypedColumnBuffer::read_uint32(std::size_t row) const {
  return read_fixed<uint32_t>(row);
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
  std::memcpy(&start_offset, offsets_.data() + row * sizeof(uint32_t),
              sizeof(uint32_t));
  std::memcpy(&end_offset, offsets_.data() + (row + 1) * sizeof(uint32_t),
              sizeof(uint32_t));
  return {reinterpret_cast<const char*>(values_.data()) + start_offset,
          end_offset - start_offset};
}

bool TypedColumnBuffer::is_null(std::size_t row) const {
  if (!validity_initialized_) {
    return false;
  }
  return !validity_bitmap::is_valid(validity_, row);
}

// ---------------------------------------------------------------------------
// read_as_double
// ---------------------------------------------------------------------------

double TypedColumnBuffer::read_as_double(std::size_t row) const {
  switch (descriptor_.logical_type) {
    case PrimitiveType::kFloat32:
      return static_cast<double>(read_float32(row));
    case PrimitiveType::kFloat64:
      return read_float64(row);
    case PrimitiveType::kInt8:
      return static_cast<double>(read_int8(row));
    case PrimitiveType::kInt16:
      return static_cast<double>(read_int16(row));
    case PrimitiveType::kInt32:
      return static_cast<double>(read_int32(row));
    case PrimitiveType::kInt64:
      return static_cast<double>(read_int64(row));
    case PrimitiveType::kUint8:
      return static_cast<double>(read_uint8(row));
    case PrimitiveType::kUint16:
      return static_cast<double>(read_uint16(row));
    case PrimitiveType::kUint32:
      return static_cast<double>(read_uint32(row));
    case PrimitiveType::kUint64:
      return static_cast<double>(read_uint64(row));
    case PrimitiveType::kBool:
      return read_bool(row) ? 1.0 : 0.0;
    case PrimitiveType::kString:
      return std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();  // unreachable
}

}  // namespace pj::engine
