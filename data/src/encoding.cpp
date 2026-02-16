#include "pj/engine/encoding.hpp"

#include <cstring>

#include "absl/container/flat_hash_map.h"

namespace pj::engine::encoding {

// ---------------------------------------------------------------------------
// Delta encoding
// ---------------------------------------------------------------------------

DeltaEncoded delta_encode(const int64_t* values, std::size_t count) {
  DeltaEncoded result;
  if (count == 0) {
    return result;
  }

  result.base_value = values[0];
  result.count = count;

  if (count > 1) {
    result.deltas.reserve((count - 1) * sizeof(int64_t));
    for (std::size_t i = 1; i < count; ++i) {
      int64_t delta = values[i] - values[i - 1];
      result.deltas.append(&delta, sizeof(int64_t));
    }
  }

  return result;
}

void delta_decode(const DeltaEncoded& encoded, int64_t* output, std::size_t count) {
  if (count == 0) {
    return;
  }

  output[0] = encoded.base_value;

  const uint8_t* delta_data = encoded.deltas.data();
  for (std::size_t i = 1; i < count; ++i) {
    int64_t delta = 0;
    std::memcpy(&delta, delta_data + (i - 1) * sizeof(int64_t), sizeof(int64_t));
    output[i] = output[i - 1] + delta;
  }
}

// ---------------------------------------------------------------------------
// Dictionary encoding for strings
// ---------------------------------------------------------------------------

DictionaryEncoded dictionary_encode_strings(
    const uint8_t* offsets_data, [[maybe_unused]] std::size_t offsets_size,
    const uint8_t* values_data, [[maybe_unused]] std::size_t values_size,
    std::size_t row_count) {
  DictionaryEncoded result;
  result.count = row_count;

  if (row_count == 0) {
    return result;
  }

  // Map from string to its index in the dictionary vector.
  // We use std::string as the key (not string_view) because emplace_back on
  // the dictionary vector can reallocate, invalidating any string_view that
  // pointed into previously-stored std::string objects.
  absl::flat_hash_map<std::string, uint32_t> lookup;

  result.indices.reserve(row_count * sizeof(uint32_t));

  for (std::size_t row = 0; row < row_count; ++row) {
    // Read offsets[row] and offsets[row+1] as uint32_t
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    std::memcpy(&start_offset, offsets_data + row * sizeof(uint32_t), sizeof(uint32_t));
    std::memcpy(&end_offset, offsets_data + (row + 1) * sizeof(uint32_t), sizeof(uint32_t));

    std::string_view str_view(
        reinterpret_cast<const char*>(values_data + start_offset),
        end_offset - start_offset);

    auto it = lookup.find(str_view);
    uint32_t index = 0;
    if (it != lookup.end()) {
      index = it->second;
    } else {
      index = static_cast<uint32_t>(result.dictionary.size());
      std::string owned_str(str_view);
      result.dictionary.push_back(owned_str);
      lookup[std::move(owned_str)] = index;
    }

    result.indices.append(&index, sizeof(uint32_t));
  }

  return result;
}

std::string_view dictionary_lookup(
    const DictionaryEncoded& encoded, std::size_t row) {
  uint32_t index = 0;
  std::memcpy(&index, encoded.indices.data() + row * sizeof(uint32_t), sizeof(uint32_t));
  return encoded.dictionary[index];
}

// ---------------------------------------------------------------------------
// Packed bools
// ---------------------------------------------------------------------------

PackedBools pack_bools(const uint8_t* values, std::size_t count) {
  PackedBools result;
  result.count = count;

  if (count == 0) {
    return result;
  }

  std::size_t num_bytes = (count + 7) / 8;
  result.bits.resize(num_bytes);

  // Zero-initialize (resize should already give us zeroed memory from
  // std::vector, but be explicit)
  uint8_t* bit_data = result.bits.mutable_data();
  std::memset(bit_data, 0, num_bytes);

  for (std::size_t i = 0; i < count; ++i) {
    if (values[i] != 0) {
      std::size_t byte_idx = i / 8;
      std::size_t bit_idx = i % 8;
      bit_data[byte_idx] |= static_cast<uint8_t>(1u << bit_idx);
    }
  }

  return result;
}

bool unpack_bool(const PackedBools& packed, std::size_t index) {
  std::size_t byte_idx = index / 8;
  std::size_t bit_idx = index % 8;
  uint8_t byte_val = 0;
  std::memcpy(&byte_val, packed.bits.data() + byte_idx, sizeof(uint8_t));
  return (byte_val & (1u << bit_idx)) != 0;
}

}  // namespace pj::engine::encoding
