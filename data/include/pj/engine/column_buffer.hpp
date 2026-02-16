#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "pj/engine/buffer.hpp"
#include "pj/engine/type_tree.hpp"
#include "pj/engine/types.hpp"

namespace pj::engine {

enum class EncodingType : uint8_t {
  kRaw,         // Unencoded typed storage
  kDelta,       // Delta encoding (timestamps)
  kDictionary,  // Dictionary encoding (strings)
  kPackedBool,  // Packed bitfield (bools)
};

struct ColumnDescriptor {
  FieldId field_id;
  PrimitiveType logical_type;
  std::string field_path;  // e.g., "position.x"
};

class TypedColumnBuffer {
 public:
  explicit TypedColumnBuffer(ColumnDescriptor descriptor);

  [[nodiscard]] const ColumnDescriptor& descriptor() const noexcept;
  [[nodiscard]] std::size_t row_count() const noexcept;
  [[nodiscard]] bool has_nulls() const noexcept;

  // Append typed values
  void append_float32(float value);
  void append_float64(double value);
  void append_int8(int8_t value);
  void append_int16(int16_t value);
  void append_int32(int32_t value);
  void append_int64(int64_t value);
  void append_uint8(uint8_t value);
  void append_uint16(uint16_t value);
  void append_uint32(uint32_t value);
  void append_uint64(uint64_t value);
  void append_bool(bool value);
  void append_string(std::string_view value);
  void append_null();

  // Read typed values (raw unencoded access)
  [[nodiscard]] float read_float32(std::size_t row) const;
  [[nodiscard]] double read_float64(std::size_t row) const;
  [[nodiscard]] int8_t read_int8(std::size_t row) const;
  [[nodiscard]] int16_t read_int16(std::size_t row) const;
  [[nodiscard]] int32_t read_int32(std::size_t row) const;
  [[nodiscard]] int64_t read_int64(std::size_t row) const;
  [[nodiscard]] uint8_t read_uint8(std::size_t row) const;
  [[nodiscard]] uint16_t read_uint16(std::size_t row) const;
  [[nodiscard]] uint32_t read_uint32(std::size_t row) const;
  [[nodiscard]] uint64_t read_uint64(std::size_t row) const;
  [[nodiscard]] bool read_bool(std::size_t row) const;
  [[nodiscard]] std::string_view read_string(std::size_t row) const;
  [[nodiscard]] bool is_null(std::size_t row) const;

  // Read any numeric column as double (for stats, display).
  // For string columns, returns NaN.
  [[nodiscard]] double read_as_double(std::size_t row) const;

  // Access underlying buffers (for encoding at seal time)
  [[nodiscard]] const RawBuffer& value_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& validity_buffer() const noexcept;
  [[nodiscard]] const RawBuffer& offsets_buffer() const noexcept;  // strings only

 private:
  ColumnDescriptor descriptor_;
  RawBuffer values_;
  RawBuffer validity_;
  RawBuffer offsets_;  // For string: offset array (uint32_t per entry + 1 sentinel)
  std::size_t row_count_ = 0;
  std::size_t null_count_ = 0;
  bool validity_initialized_ = false;

  void ensure_validity_initialized();

  template <typename T>
  void append_fixed(T value);

  template <typename T>
  [[nodiscard]] T read_fixed(std::size_t row) const;
};

}  // namespace pj::engine
