#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "pj/engine/buffer.hpp"

namespace pj::engine {
class TypedColumnBuffer;  // forward declare
}

namespace pj::engine::encoding {

// Delta encoding: stores first value + deltas as int64_t
struct DeltaEncoded {
  int64_t base_value = 0;
  RawBuffer deltas;    // int64_t deltas (count-1 values)
  std::size_t count = 0;
};

[[nodiscard]] DeltaEncoded delta_encode(const int64_t* values, std::size_t count);
void delta_decode(const DeltaEncoded& encoded, int64_t* output, std::size_t count);

// Dictionary encoding for strings
struct DictionaryEncoded {
  std::vector<std::string> dictionary;  // unique values in insertion order
  RawBuffer indices;                     // uint32_t indices into dictionary
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

// Packed bitfield for bools (1 bit per value, LSB first like Arrow validity bitmaps)
struct PackedBools {
  RawBuffer bits;
  std::size_t count = 0;
};

// Pack bool values (stored as uint8_t 0/1) into a bitfield
[[nodiscard]] PackedBools pack_bools(const uint8_t* values, std::size_t count);
[[nodiscard]] bool unpack_bool(const PackedBools& packed, std::size_t index);

}  // namespace pj::engine::encoding
