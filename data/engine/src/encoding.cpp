#include "pj/engine/encoding.hpp"

#include <cstring>

#include "absl/container/flat_hash_map.h"

namespace pj::engine::encoding {

namespace {

// Write an index in the given byte width
void write_index(RawBuffer& buf, uint32_t index, uint8_t bytes) {
  switch (bytes) {
    case 1: {
      auto v = static_cast<uint8_t>(index);
      buf.append(&v, sizeof(v));
      break;
    }
    case 2: {
      auto v = static_cast<uint16_t>(index);
      buf.append(&v, sizeof(v));
      break;
    }
    default: {
      buf.append(&index, sizeof(index));
      break;
    }
  }
}

// Read an index at the given byte width
[[nodiscard]] uint32_t read_index(const uint8_t* data, std::size_t row,
                                   uint8_t bytes) {
  switch (bytes) {
    case 1: {
      uint8_t v = 0;
      std::memcpy(&v, data + row, sizeof(v));
      return v;
    }
    case 2: {
      uint16_t v = 0;
      std::memcpy(&v, data + row * 2, sizeof(v));
      return v;
    }
    default: {
      uint32_t v = 0;
      std::memcpy(&v, data + row * 4, sizeof(v));
      return v;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Constant encoding
// ---------------------------------------------------------------------------

ConstantEncoded constant_encode(const uint8_t* data, StorageKind kind,
                                std::size_t count) {
  ConstantEncoded result;
  result.value_kind = kind;
  result.count = count;

  const std::size_t esize = storage_kind_size(kind);
  result.value_size = static_cast<uint8_t>(esize);
  if (esize > 0 && count > 0) {
    std::memcpy(result.value_bytes.data(), data, esize);
  }
  return result;
}

double constant_decode_as_double(const ConstantEncoded& enc) {
  const uint8_t* ptr = enc.value_bytes.data();

  auto load = [&]<typename T>(const T* /*tag*/) -> double {
    T v{};
    std::memcpy(&v, ptr, sizeof(v));
    return static_cast<double>(v);
  };

  switch (enc.value_kind) {
    case StorageKind::kFloat32: return load(static_cast<const float*>(nullptr));
    case StorageKind::kFloat64: return load(static_cast<const double*>(nullptr));
    case StorageKind::kInt32:   return load(static_cast<const int32_t*>(nullptr));
    case StorageKind::kInt64:   return load(static_cast<const int64_t*>(nullptr));
    case StorageKind::kUint64:  return load(static_cast<const uint64_t*>(nullptr));
    case StorageKind::kBool:
    case StorageKind::kString:
      break;
  }
  return 0.0;
}

// ---------------------------------------------------------------------------
// Frame of Reference encoding
// Data must be int64_t values.
// ---------------------------------------------------------------------------

FrameOfReferenceEncoded for_encode(const uint8_t* data, StorageKind kind,
                                   std::size_t count,
                                   int64_t min_val, int64_t max_val) {
  FrameOfReferenceEncoded result;
  result.reference = min_val;
  result.count = count;

  const auto range = static_cast<uint64_t>(max_val - min_val);
  result.offset_bytes = offset_bytes_for(range);

  const std::size_t esize = storage_kind_size(kind);
  result.offsets.reserve(count * result.offset_bytes);

  for (std::size_t i = 0; i < count; ++i) {
    int64_t val{};
    if (kind == StorageKind::kInt32) {
      int32_t tmp{};
      std::memcpy(&tmp, data + i * esize, sizeof(tmp));
      val = tmp;  // sign-extend
    } else {
      std::memcpy(&val, data + i * esize, sizeof(val));
    }
    const auto offset = static_cast<uint64_t>(val - min_val);

    switch (result.offset_bytes) {
      case 1: {
        auto v = static_cast<uint8_t>(offset);
        result.offsets.append(&v, sizeof(v));
        break;
      }
      case 2: {
        auto v = static_cast<uint16_t>(offset);
        result.offsets.append(&v, sizeof(v));
        break;
      }
      default: {
        auto v = static_cast<uint32_t>(offset);
        result.offsets.append(&v, sizeof(v));
        break;
      }
    }
  }

  return result;
}

double for_decode_one_as_double(const FrameOfReferenceEncoded& enc,
                                std::size_t row) {
  const uint8_t* data = enc.offsets.data();
  uint64_t offset = 0;

  switch (enc.offset_bytes) {
    case 1: {
      uint8_t v = 0;
      std::memcpy(&v, data + row, sizeof(v));
      offset = v;
      break;
    }
    case 2: {
      uint16_t v = 0;
      std::memcpy(&v, data + row * 2, sizeof(v));
      offset = v;
      break;
    }
    default: {
      uint32_t v = 0;
      std::memcpy(&v, data + row * 4, sizeof(v));
      offset = v;
      break;
    }
  }

  return static_cast<double>(enc.reference) + static_cast<double>(offset);
}

void for_decode_range_as_doubles(const FrameOfReferenceEncoded& enc,
                                 double* out, std::size_t row_start,
                                 std::size_t count) {
  const double ref = static_cast<double>(enc.reference);

  switch (enc.offset_bytes) {
    case 1: {
      const auto* src = reinterpret_cast<const uint8_t*>(
          enc.offsets.data()) + row_start;
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = ref + static_cast<double>(src[i]);
      }
      break;
    }
    case 2: {
      const auto* src = reinterpret_cast<const uint16_t*>(
          enc.offsets.data()) + row_start;
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = ref + static_cast<double>(src[i]);
      }
      break;
    }
    default: {
      const auto* src = reinterpret_cast<const uint32_t*>(
          enc.offsets.data()) + row_start;
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = ref + static_cast<double>(src[i]);
      }
      break;
    }
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
    result.index_bytes = 1;
    return result;
  }

  // First pass: build dictionary to determine size
  absl::flat_hash_map<std::string, uint32_t> lookup;
  std::vector<uint32_t> temp_indices;
  temp_indices.reserve(row_count);

  for (std::size_t row = 0; row < row_count; ++row) {
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
    temp_indices.push_back(index);
  }

  // Determine narrowed index width
  result.index_bytes = index_bytes_for(result.dictionary.size());
  result.indices.reserve(row_count * result.index_bytes);

  for (uint32_t idx : temp_indices) {
    write_index(result.indices, idx, result.index_bytes);
  }

  return result;
}

std::string_view dictionary_lookup(
    const DictionaryEncoded& encoded, std::size_t row) {
  uint32_t index = read_index(encoded.indices.data(), row, encoded.index_bytes);
  if (index >= encoded.dictionary.size()) {
    return {};
  }
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
