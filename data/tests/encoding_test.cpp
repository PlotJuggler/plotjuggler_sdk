#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "pj/engine/encoding.hpp"

namespace pj::engine::encoding {
namespace {

// ==========================================================================
// Helper: build offsets + values buffers from a vector of strings, mimicking
// what TypedColumnBuffer would produce for a string column.
// ==========================================================================
struct StringColumnData {
  std::vector<uint8_t> offsets;
  std::vector<uint8_t> values;
};

StringColumnData make_string_column(const std::vector<std::string>& strings) {
  StringColumnData col;

  // offsets: (strings.size() + 1) uint32_t entries
  col.offsets.resize((strings.size() + 1) * sizeof(uint32_t));
  uint32_t running = 0;
  for (std::size_t i = 0; i < strings.size(); ++i) {
    std::memcpy(col.offsets.data() + i * sizeof(uint32_t), &running, sizeof(uint32_t));
    running += static_cast<uint32_t>(strings[i].size());
  }
  std::memcpy(col.offsets.data() + strings.size() * sizeof(uint32_t), &running,
               sizeof(uint32_t));

  // values: concatenated string bytes
  col.values.reserve(running);
  for (const auto& s : strings) {
    col.values.insert(col.values.end(), s.begin(), s.end());
  }

  return col;
}

// ==========================================================================
// Delta encoding tests
// ==========================================================================

TEST(DeltaEncoding, MonotonicTimestamps) {
  std::array<int64_t, 4> values = {1000, 1010, 1020, 1030};
  auto encoded = delta_encode(values.data(), values.size());

  EXPECT_EQ(encoded.count, 4u);
  EXPECT_EQ(encoded.base_value, 1000);
  EXPECT_EQ(encoded.deltas.size(), 3 * sizeof(int64_t));

  std::array<int64_t, 4> decoded{};
  delta_decode(encoded, decoded.data(), decoded.size());

  for (std::size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(decoded[i], values[i]) << "mismatch at index " << i;
  }
}

TEST(DeltaEncoding, NegativeDeltas) {
  std::array<int64_t, 4> values = {100, 90, 110, 80};
  auto encoded = delta_encode(values.data(), values.size());

  EXPECT_EQ(encoded.count, 4u);
  EXPECT_EQ(encoded.base_value, 100);

  std::array<int64_t, 4> decoded{};
  delta_decode(encoded, decoded.data(), decoded.size());

  for (std::size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(decoded[i], values[i]) << "mismatch at index " << i;
  }
}

TEST(DeltaEncoding, SingleValue) {
  int64_t value = 42;
  auto encoded = delta_encode(&value, 1);

  EXPECT_EQ(encoded.count, 1u);
  EXPECT_EQ(encoded.base_value, 42);
  EXPECT_TRUE(encoded.deltas.empty());

  int64_t decoded = 0;
  delta_decode(encoded, &decoded, 1);
  EXPECT_EQ(decoded, 42);
}

TEST(DeltaEncoding, Empty) {
  auto encoded = delta_encode(nullptr, 0);

  EXPECT_EQ(encoded.count, 0u);
  EXPECT_TRUE(encoded.deltas.empty());

  // Decoding with count=0 should be a no-op
  delta_decode(encoded, nullptr, 0);
}

// ==========================================================================
// Dictionary encoding tests
// ==========================================================================

TEST(DictionaryEncoding, RepeatedStrings) {
  auto col = make_string_column({"base", "world", "base", "base"});

  auto encoded = dictionary_encode_strings(
      col.offsets.data(), col.offsets.size(),
      col.values.data(), col.values.size(), 4);

  EXPECT_EQ(encoded.count, 4u);
  EXPECT_EQ(encoded.dictionary.size(), 2u);
  EXPECT_EQ(encoded.dictionary[0], "base");
  EXPECT_EQ(encoded.dictionary[1], "world");

  EXPECT_EQ(dictionary_lookup(encoded, 0), "base");
  EXPECT_EQ(dictionary_lookup(encoded, 1), "world");
  EXPECT_EQ(dictionary_lookup(encoded, 2), "base");
  EXPECT_EQ(dictionary_lookup(encoded, 3), "base");
}

TEST(DictionaryEncoding, AllUniqueStrings) {
  auto col = make_string_column({"alpha", "beta", "gamma", "delta"});

  auto encoded = dictionary_encode_strings(
      col.offsets.data(), col.offsets.size(),
      col.values.data(), col.values.size(), 4);

  EXPECT_EQ(encoded.count, 4u);
  EXPECT_EQ(encoded.dictionary.size(), 4u);
}

TEST(DictionaryEncoding, LookupCorrectness) {
  std::vector<std::string> strings = {"foo", "bar", "baz", "foo", "qux"};
  auto col = make_string_column(strings);

  auto encoded = dictionary_encode_strings(
      col.offsets.data(), col.offsets.size(),
      col.values.data(), col.values.size(), 5);

  EXPECT_EQ(encoded.count, 5u);

  for (std::size_t i = 0; i < strings.size(); ++i) {
    EXPECT_EQ(dictionary_lookup(encoded, i), strings[i])
        << "mismatch at row " << i;
  }
}

// ==========================================================================
// Packed bools tests
// ==========================================================================

TEST(PackedBools, SixteenValues) {
  //                    0  1  2  3  4  5  6  7   8  9  10 11 12 13 14 15
  std::array<uint8_t, 16> values = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};

  auto packed = pack_bools(values.data(), values.size());

  EXPECT_EQ(packed.count, 16u);
  EXPECT_EQ(packed.bits.size(), 2u);  // 16 bits = 2 bytes

  for (std::size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(unpack_bool(packed, i), values[i] != 0)
        << "mismatch at index " << i;
  }
}

TEST(PackedBools, ExactByteBoundary) {
  // Exactly 8 values
  std::array<uint8_t, 8> values = {1, 1, 0, 1, 0, 1, 1, 0};

  auto packed = pack_bools(values.data(), values.size());

  EXPECT_EQ(packed.count, 8u);
  EXPECT_EQ(packed.bits.size(), 1u);  // 8 bits = 1 byte

  for (std::size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(unpack_bool(packed, i), values[i] != 0)
        << "mismatch at index " << i;
  }
}

TEST(PackedBools, NineValues) {
  // One past a byte boundary
  std::array<uint8_t, 9> values = {1, 0, 1, 1, 0, 0, 1, 0, 1};

  auto packed = pack_bools(values.data(), values.size());

  EXPECT_EQ(packed.count, 9u);
  EXPECT_EQ(packed.bits.size(), 2u);  // ceil(9/8) = 2 bytes

  for (std::size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(unpack_bool(packed, i), values[i] != 0)
        << "mismatch at index " << i;
  }
}

TEST(PackedBools, Empty) {
  auto packed = pack_bools(nullptr, 0);

  EXPECT_EQ(packed.count, 0u);
  EXPECT_TRUE(packed.bits.empty());
}

}  // namespace
}  // namespace pj::engine::encoding
