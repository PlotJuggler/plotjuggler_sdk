#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "pj/engine/buffer.hpp"
#include "pj/engine/column_buffer.hpp"  // StorageKind

namespace pj::engine::encoding {

// ---------------------------------------------------------------------------
// Constant encoding — stores a single repeated value
// ---------------------------------------------------------------------------
struct ConstantEncoded {
  std::array<uint8_t, 8> value_bytes{};  // Single value (max StorageKind size)
  StorageKind value_kind;
  uint8_t value_size = 0;
  std::size_t count = 0;
};

// ---------------------------------------------------------------------------
// Frame of Reference — subtract min, store narrowed offsets
// (Applied to kInt32 and kInt64 columns; kUint64 is excluded because large
//  unsigned values overflow the int64_t reference.)
// ---------------------------------------------------------------------------
struct FrameOfReferenceEncoded {
  int64_t reference = 0;     // min value
  uint8_t offset_bytes = 0;  // 1, 2, or 4
  RawBuffer offsets;          // packed as uint8/uint16/uint32
  std::size_t count = 0;
};

// ---------------------------------------------------------------------------
// Dictionary encoding for strings (with narrowed indices)
// ---------------------------------------------------------------------------
struct DictionaryEncoded {
  std::vector<std::string> dictionary;  // unique values in insertion order
  RawBuffer indices;                     // stored as uint8/uint16/uint32
  uint8_t index_bytes = 4;              // 1, 2, or 4
  std::size_t count = 0;
};

// Encode a string column into dictionary form.
// Takes raw string data (offsets buffer + value buffer from TypedColumnBuffer).
[[nodiscard]] DictionaryEncoded dictionary_encode_strings(
    const uint8_t* offsets_data, std::size_t offsets_size,
    const uint8_t* values_data, std::size_t values_size,
    std::size_t row_count);

[[nodiscard]] std::string_view dictionary_lookup(
    const DictionaryEncoded& encoded, std::size_t row);

// ---------------------------------------------------------------------------
// Packed bitfield for bools (1 bit per value, LSB first like Arrow validity bitmaps)
// ---------------------------------------------------------------------------
struct PackedBools {
  RawBuffer bits;
  std::size_t count = 0;
};

// Pack bool values (stored as uint8_t 0/1) into a bitfield
[[nodiscard]] PackedBools pack_bools(const uint8_t* values, std::size_t count);
[[nodiscard]] bool unpack_bool(const PackedBools& packed, std::size_t index);

// ---------------------------------------------------------------------------
// Unified per-column encoding data variant
// ---------------------------------------------------------------------------
using ColumnEncodingData = std::variant<
    std::monostate,            // kRaw — data in encoded_columns[i]
    ConstantEncoded,
    FrameOfReferenceEncoded,
    DictionaryEncoded,
    PackedBools>;

// ---------------------------------------------------------------------------
// Constant encoding functions
// ---------------------------------------------------------------------------
[[nodiscard]] ConstantEncoded constant_encode(
    const uint8_t* data, StorageKind kind, std::size_t count);
[[nodiscard]] double constant_decode_as_double(const ConstantEncoded& enc);

// ---------------------------------------------------------------------------
// Frame of Reference encoding functions
// Data must be kInt32 or kInt64 values.
// ---------------------------------------------------------------------------
[[nodiscard]] FrameOfReferenceEncoded for_encode(
    const uint8_t* data, StorageKind kind, std::size_t count,
    int64_t min_val, int64_t max_val);
[[nodiscard]] double for_decode_one_as_double(
    const FrameOfReferenceEncoded& enc, std::size_t row);
void for_decode_range_as_doubles(
    const FrameOfReferenceEncoded& enc,
    double* out, std::size_t row_start, std::size_t count);

// ---------------------------------------------------------------------------
// Byte-width helpers
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint8_t index_bytes_for(std::size_t dict_size) noexcept {
  if (dict_size <= 256) return 1;
  if (dict_size <= 65536) return 2;
  return 4;
}

[[nodiscard]] constexpr uint8_t offset_bytes_for(uint64_t range) noexcept {
  if (range < 256) return 1;
  if (range < 65536) return 2;
  if (range < uint64_t{1} << 32) return 4;
  return 8;  // signals: stay kRaw
}

}  // namespace pj::engine::encoding
