#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "pj/engine/encoding.hpp"
#include "pj/base/type_tree.hpp"  // PrimitiveType

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

// ==========================================================================
// Constant encoding tests
// ==========================================================================

TEST(ConstantEncoding, Float64) {
  constexpr std::size_t count = 100;
  std::vector<uint8_t> buf(count * sizeof(double));
  for (std::size_t i = 0; i < count; ++i) {
    double val = 3.14;
    std::memcpy(buf.data() + i * sizeof(double), &val, sizeof(double));
  }

  auto enc = constant_encode(buf.data(), StorageKind::kFloat64, count);
  EXPECT_EQ(enc.count, count);
  EXPECT_EQ(enc.value_kind, StorageKind::kFloat64);
  EXPECT_EQ(enc.value_size, sizeof(double));

  EXPECT_DOUBLE_EQ(constant_decode_as_double(enc), 3.14);
}

TEST(ConstantEncoding, Int32) {
  constexpr std::size_t count = 100;
  std::vector<uint8_t> buf(count * sizeof(int32_t));
  for (std::size_t i = 0; i < count; ++i) {
    int32_t val = -42;
    std::memcpy(buf.data() + i * sizeof(int32_t), &val, sizeof(int32_t));
  }

  auto enc = constant_encode(buf.data(), StorageKind::kInt32, count);
  EXPECT_EQ(enc.count, count);
  EXPECT_EQ(enc.value_kind, StorageKind::kInt32);

  EXPECT_DOUBLE_EQ(constant_decode_as_double(enc), -42.0);
}

// ==========================================================================
// Frame of Reference encoding tests
// ==========================================================================

TEST(FOREncoding, NarrowRange_Int32) {
  // Values [1000..1100]: range=100, fits in uint8
  constexpr std::size_t count = 101;
  std::vector<uint8_t> buf(count * sizeof(int32_t));
  for (std::size_t i = 0; i < count; ++i) {
    auto val = static_cast<int32_t>(1000 + i);
    std::memcpy(buf.data() + i * sizeof(int32_t), &val, sizeof(int32_t));
  }

  auto enc = for_encode(buf.data(), StorageKind::kInt32, count, 1000, 1100);
  EXPECT_EQ(enc.offset_bytes, 1);
  EXPECT_EQ(enc.reference, 1000);
  EXPECT_EQ(enc.count, count);
  EXPECT_EQ(enc.offsets.size(), count * 1);  // 1 byte per offset

  // Verify round-trip all values
  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_DOUBLE_EQ(for_decode_one_as_double(enc, i),
                     1000.0 + static_cast<double>(i)) << "row " << i;
  }
}

TEST(FOREncoding, MediumRange_Int32) {
  // Values [0..50000]: range=50000, fits in uint16
  constexpr std::size_t count = 501;
  std::vector<uint8_t> buf(count * sizeof(int32_t));
  for (std::size_t i = 0; i < count; ++i) {
    auto val = static_cast<int32_t>(i * 100);
    std::memcpy(buf.data() + i * sizeof(int32_t), &val, sizeof(int32_t));
  }

  auto enc = for_encode(buf.data(), StorageKind::kInt32, count, 0, 50000);
  EXPECT_EQ(enc.offset_bytes, 2);
  EXPECT_EQ(enc.reference, 0);

  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_DOUBLE_EQ(for_decode_one_as_double(enc, i),
                     static_cast<double>(i * 100)) << "row " << i;
  }
}

TEST(FOREncoding, NegativeRange_Int64) {
  // Values [-100..100]: range=200, fits in uint8 (int64 storage)
  constexpr std::size_t count = 201;
  std::vector<uint8_t> buf(count * sizeof(int64_t));
  for (std::size_t i = 0; i < count; ++i) {
    auto val = static_cast<int64_t>(-100 + static_cast<int64_t>(i));
    std::memcpy(buf.data() + i * sizeof(int64_t), &val, sizeof(int64_t));
  }

  auto enc = for_encode(buf.data(), StorageKind::kInt64, count, -100, 100);
  EXPECT_EQ(enc.offset_bytes, 1);
  EXPECT_EQ(enc.reference, -100);

  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_DOUBLE_EQ(for_decode_one_as_double(enc, i),
                     -100.0 + static_cast<double>(i)) << "row " << i;
  }
}

TEST(FOREncoding, BulkDecode) {
  constexpr std::size_t count = 50;
  std::vector<uint8_t> buf(count * sizeof(int32_t));
  for (std::size_t i = 0; i < count; ++i) {
    auto val = static_cast<int32_t>(500 + i);
    std::memcpy(buf.data() + i * sizeof(int32_t), &val, sizeof(int32_t));
  }

  auto enc = for_encode(buf.data(), StorageKind::kInt32, count, 500, 549);

  std::vector<double> out(count);
  for_decode_range_as_doubles(enc, out.data(), 0, count);

  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_DOUBLE_EQ(out[i], 500.0 + static_cast<double>(i)) << "row " << i;
  }

  // Decode a sub-range
  std::vector<double> sub(10);
  for_decode_range_as_doubles(enc, sub.data(), 20, 10);
  for (std::size_t i = 0; i < 10; ++i) {
    EXPECT_DOUBLE_EQ(sub[i], 520.0 + static_cast<double>(i)) << "sub " << i;
  }
}

// ==========================================================================
// Dictionary encoding with narrowed indices
// ==========================================================================

TEST(DictionaryEncoding, NarrowIndices) {
  // 4 unique strings, 1000 rows → indices should be uint8 (1 byte each)
  std::vector<std::string> data;
  data.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    switch (i % 4) {
      case 0: data.push_back("alpha"); break;
      case 1: data.push_back("beta"); break;
      case 2: data.push_back("gamma"); break;
      case 3: data.push_back("delta"); break;
    }
  }
  auto col = make_string_column(data);

  auto encoded = dictionary_encode_strings(
      col.offsets.data(), col.offsets.size(),
      col.values.data(), col.values.size(), 1000);

  EXPECT_EQ(encoded.dictionary.size(), 4u);
  EXPECT_EQ(encoded.index_bytes, 1);
  // Indices buffer should be 1000 bytes (uint8), not 4000 (uint32)
  EXPECT_EQ(encoded.indices.size(), 1000u);

  // Verify lookups
  for (std::size_t i = 0; i < 1000; ++i) {
    EXPECT_EQ(dictionary_lookup(encoded, i), data[i]) << "row " << i;
  }
}

TEST(DictionaryEncoding, MediumIndices) {
  // 300 unique strings → indices should be uint16 (2 bytes each)
  std::vector<std::string> dict_values;
  dict_values.reserve(300);
  for (int i = 0; i < 300; ++i) {
    dict_values.push_back("str_" + std::to_string(i));
  }

  // Build data with 600 rows cycling through all 300
  std::vector<std::string> data;
  data.reserve(600);
  for (int i = 0; i < 600; ++i) {
    data.push_back(dict_values[static_cast<std::size_t>(i % 300)]);
  }
  auto col = make_string_column(data);

  auto encoded = dictionary_encode_strings(
      col.offsets.data(), col.offsets.size(),
      col.values.data(), col.values.size(), 600);

  EXPECT_EQ(encoded.dictionary.size(), 300u);
  EXPECT_EQ(encoded.index_bytes, 2);
  // Indices buffer should be 600*2 = 1200 bytes
  EXPECT_EQ(encoded.indices.size(), 1200u);

  for (std::size_t i = 0; i < 600; ++i) {
    EXPECT_EQ(dictionary_lookup(encoded, i), data[i]) << "row " << i;
  }
}

// ==========================================================================
// Byte-width helper tests
// ==========================================================================

TEST(ByteWidthHelpers, IndexBytesFor) {
  EXPECT_EQ(index_bytes_for(1), 1);
  EXPECT_EQ(index_bytes_for(256), 1);
  EXPECT_EQ(index_bytes_for(257), 2);
  EXPECT_EQ(index_bytes_for(65536), 2);
  EXPECT_EQ(index_bytes_for(65537), 4);
}

TEST(ByteWidthHelpers, OffsetBytesFor) {
  EXPECT_EQ(offset_bytes_for(0), 1);
  EXPECT_EQ(offset_bytes_for(255), 1);
  EXPECT_EQ(offset_bytes_for(256), 2);
  EXPECT_EQ(offset_bytes_for(65535), 2);
  EXPECT_EQ(offset_bytes_for(65536), 4);
  EXPECT_EQ(offset_bytes_for(uint64_t{1} << 32), 8);
}

}  // namespace
}  // namespace pj::engine::encoding
