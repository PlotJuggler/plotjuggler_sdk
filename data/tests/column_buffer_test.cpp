#include "PJ/engine/column_buffer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace PJ::engine {
namespace {

// Helper: create a ColumnDescriptor for a given PrimitiveType.
ColumnDescriptor make_descriptor(PrimitiveType type, std::string path = "test_field") {
  return ColumnDescriptor{.field_id = 0, .logical_type = type, .field_path = std::move(path)};
}

// -----------------------------------------------------------------------
// 1. Float32 append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Float32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.append_float32(1.5f);
  buf.append_float32(-3.14f);
  buf.append_float32(0.0f);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_FLOAT_EQ(buf.read_float32(0), 1.5f);
  EXPECT_FLOAT_EQ(buf.read_float32(1), -3.14f);
  EXPECT_FLOAT_EQ(buf.read_float32(2), 0.0f);
}

// -----------------------------------------------------------------------
// 2. Float64 append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Float64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  buf.append_float64(2.718281828459045);
  buf.append_float64(-1e308);

  EXPECT_EQ(buf.row_count(), 2u);
  EXPECT_DOUBLE_EQ(buf.read_float64(0), 2.718281828459045);
  EXPECT_DOUBLE_EQ(buf.read_float64(1), -1e308);
}

// -----------------------------------------------------------------------
// 3. Int32 append/read (including negative)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Int32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  buf.append_int32(42);
  buf.append_int32(-100);
  buf.append_int32(0);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_int32(0), 42);
  EXPECT_EQ(buf.read_int32(1), -100);
  EXPECT_EQ(buf.read_int32(2), 0);
}

// -----------------------------------------------------------------------
// 4. Int64 append/read (large values)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Int64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt64));
  buf.append_int64(INT64_MAX);
  buf.append_int64(INT64_MIN);
  buf.append_int64(0);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_int64(0), INT64_MAX);
  EXPECT_EQ(buf.read_int64(1), INT64_MIN);
  EXPECT_EQ(buf.read_int64(2), 0);
}

// -----------------------------------------------------------------------
// 5. Uint64 append/read (uint8 logical type widens to uint64 storage)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Uint64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kUint64));
  buf.append_uint64(255);
  buf.append_uint64(0);
  buf.append_uint64(18000000000000000000ULL);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_uint64(0), 255U);
  EXPECT_EQ(buf.read_uint64(1), 0U);
  EXPECT_EQ(buf.read_uint64(2), 18000000000000000000ULL);
}

// -----------------------------------------------------------------------
// 6. Bool append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BoolAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kBool));
  buf.append_bool(true);
  buf.append_bool(false);
  buf.append_bool(true);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_TRUE(buf.read_bool(0));
  EXPECT_FALSE(buf.read_bool(1));
  EXPECT_TRUE(buf.read_bool(2));
}

// -----------------------------------------------------------------------
// 7. String append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, StringAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.append_string("hello");
  buf.append_string("world");
  buf.append_string("test");

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_string(0), "hello");
  EXPECT_EQ(buf.read_string(1), "world");
  EXPECT_EQ(buf.read_string(2), "test");
}

// -----------------------------------------------------------------------
// 8. Null handling
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, NullHandling) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.append_float32(1.0f);
  buf.append_null();
  buf.append_float32(3.0f);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_FALSE(buf.is_null(0));
  EXPECT_TRUE(buf.is_null(1));
  EXPECT_FALSE(buf.is_null(2));

  // Non-null values are still readable.
  EXPECT_FLOAT_EQ(buf.read_float32(0), 1.0f);
  EXPECT_FLOAT_EQ(buf.read_float32(2), 3.0f);
}

// -----------------------------------------------------------------------
// 9. has_nulls
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, HasNulls) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  EXPECT_FALSE(buf.has_nulls());

  buf.append_int32(10);
  EXPECT_FALSE(buf.has_nulls());

  buf.append_null();
  EXPECT_TRUE(buf.has_nulls());
}

// -----------------------------------------------------------------------
// 10. row_count increments correctly
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, RowCountIncrements) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  EXPECT_EQ(buf.row_count(), 0u);

  buf.append_float64(1.0);
  EXPECT_EQ(buf.row_count(), 1u);

  buf.append_float64(2.0);
  EXPECT_EQ(buf.row_count(), 2u);

  buf.append_null();
  EXPECT_EQ(buf.row_count(), 3u);

  buf.append_float64(4.0);
  EXPECT_EQ(buf.row_count(), 4u);
}

// -----------------------------------------------------------------------
// 11. read_as_double for float32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleFloat32) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.append_float32(1.5f);

  EXPECT_DOUBLE_EQ(buf.read_as_double(0), 1.5);
}

// -----------------------------------------------------------------------
// 12. read_as_double for int32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleInt32) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  buf.append_int32(42);

  EXPECT_DOUBLE_EQ(buf.read_as_double(0), 42.0);
}

// -----------------------------------------------------------------------
// 13. Multiple strings of varying lengths
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, MultipleStringsVaryingLengths) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.append_string("a");
  buf.append_string("bb");
  buf.append_string("ccc");
  buf.append_string("dddd");
  buf.append_string("eeeee");

  EXPECT_EQ(buf.row_count(), 5u);
  EXPECT_EQ(buf.read_string(0), "a");
  EXPECT_EQ(buf.read_string(1), "bb");
  EXPECT_EQ(buf.read_string(2), "ccc");
  EXPECT_EQ(buf.read_string(3), "dddd");
  EXPECT_EQ(buf.read_string(4), "eeeee");
}

// -----------------------------------------------------------------------
// 14. Empty string
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, EmptyString) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.append_string("");

  EXPECT_EQ(buf.row_count(), 1u);
  EXPECT_EQ(buf.read_string(0), "");
  EXPECT_TRUE(buf.read_string(0).empty());
}

// -----------------------------------------------------------------------
// Additional: descriptor accessor
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, DescriptorAccessor) {
  auto desc = make_descriptor(PrimitiveType::kFloat64, "position.x");
  TypedColumnBuffer buf(desc);

  EXPECT_EQ(buf.descriptor().logical_type, PrimitiveType::kFloat64);
  EXPECT_EQ(buf.descriptor().field_path, "position.x");
}

// -----------------------------------------------------------------------
// Additional: read_as_double for bool
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleBool) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kBool));
  buf.append_bool(true);
  buf.append_bool(false);

  EXPECT_DOUBLE_EQ(buf.read_as_double(0), 1.0);
  EXPECT_DOUBLE_EQ(buf.read_as_double(1), 0.0);
}

// -----------------------------------------------------------------------
// Additional: read_as_double for string returns NaN
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleStringReturnsNaN) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.append_string("hello");

  EXPECT_TRUE(std::isnan(buf.read_as_double(0)));
}

// -----------------------------------------------------------------------
// Additional: null in string column
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, NullInStringColumn) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.append_string("hello");
  buf.append_null();
  buf.append_string("world");

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_FALSE(buf.is_null(0));
  EXPECT_TRUE(buf.is_null(1));
  EXPECT_FALSE(buf.is_null(2));
  EXPECT_EQ(buf.read_string(0), "hello");
  EXPECT_EQ(buf.read_string(1), "");  // null string reads as empty
  EXPECT_EQ(buf.read_string(2), "world");
}

// -----------------------------------------------------------------------
// Additional: buffer accessors are non-empty after appends
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ValueBufferNonEmpty) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  EXPECT_TRUE(buf.value_buffer().empty());

  buf.append_int32(10);
  EXPECT_FALSE(buf.value_buffer().empty());
  EXPECT_EQ(buf.value_buffer().size(), sizeof(int32_t));
}

TEST(TypedColumnBufferTest, OffsetsBufferForStrings) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  EXPECT_TRUE(buf.offsets_buffer().empty());

  buf.append_string("hi");
  // offsets: [0, 2] => 2 * sizeof(uint32_t) = 8
  EXPECT_EQ(buf.offsets_buffer().size(), 2 * sizeof(uint32_t));
}

// -----------------------------------------------------------------------
// Bulk append: Float32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkFloat32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  const float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  buf.append_float32_bulk(Span<const float>(data, 5));

  EXPECT_EQ(buf.row_count(), 5u);
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_FLOAT_EQ(buf.read_float32(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Float64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkFloat64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  const double data[] = {1.1, 2.2, 3.3};
  buf.append_float64_bulk(Span<const double>(data, 3));

  EXPECT_EQ(buf.row_count(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(buf.read_float64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Int32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkInt32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  const int32_t data[] = {-100, 0, 42, 999};
  buf.append_int32_bulk(Span<const int32_t>(data, 4));

  EXPECT_EQ(buf.row_count(), 4u);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(buf.read_int32(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Int64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkInt64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt64));
  const int64_t data[] = {INT64_MIN, 0, INT64_MAX};
  buf.append_int64_bulk(Span<const int64_t>(data, 3));

  EXPECT_EQ(buf.row_count(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(buf.read_int64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Uint64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkUint64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kUint64));
  const uint64_t data[] = {0, 255, 18000000000000000000ULL};
  buf.append_uint64_bulk(Span<const uint64_t>(data, 3));

  EXPECT_EQ(buf.row_count(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(buf.read_uint64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Bool
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkBoolAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kBool));
  const uint8_t data[] = {1, 0, 1, 1, 0};
  buf.append_bool_bulk(Span<const uint8_t>(data, 5));

  EXPECT_EQ(buf.row_count(), 5u);
  EXPECT_TRUE(buf.read_bool(0));
  EXPECT_FALSE(buf.read_bool(1));
  EXPECT_TRUE(buf.read_bool(2));
  EXPECT_TRUE(buf.read_bool(3));
  EXPECT_FALSE(buf.read_bool(4));
}

// -----------------------------------------------------------------------
// Bulk append: Strings
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkStringAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  // "hello" "world" "!"
  const char string_data[] = "helloworld!";
  const uint32_t offsets[] = {0, 5, 10, 11};
  buf.append_strings_bulk(Span<const uint32_t>(offsets, 4), Span<const char>(string_data, 11));

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_string(0), "hello");
  EXPECT_EQ(buf.read_string(1), "world");
  EXPECT_EQ(buf.read_string(2), "!");
}

// -----------------------------------------------------------------------
// Bulk append: Validity bitmap
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkValidityBitmap) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  const float data[] = {1.0f, 0.0f, 3.0f, 0.0f};
  buf.append_float32_bulk(Span<const float>(data, 4));

  // Validity bitmap: bits [1,0,1,0] = 0b0101 = 0x05
  const uint8_t bitmap[] = {0x05};
  buf.append_validity_bulk(BitSpan{Span<const uint8_t>(bitmap, 1), 0, 4});

  EXPECT_FALSE(buf.is_null(0));
  EXPECT_TRUE(buf.is_null(1));
  EXPECT_FALSE(buf.is_null(2));
  EXPECT_TRUE(buf.is_null(3));
  EXPECT_TRUE(buf.has_nulls());
}

// -----------------------------------------------------------------------
// Bulk append: Mixed single + bulk
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkAfterSingleAppend) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.append_float32(1.0f);
  buf.append_float32(2.0f);

  const float bulk[] = {3.0f, 4.0f, 5.0f};
  buf.append_float32_bulk(Span<const float>(bulk, 3));

  EXPECT_EQ(buf.row_count(), 5u);
  EXPECT_FLOAT_EQ(buf.read_float32(0), 1.0f);
  EXPECT_FLOAT_EQ(buf.read_float32(1), 2.0f);
  EXPECT_FLOAT_EQ(buf.read_float32(2), 3.0f);
  EXPECT_FLOAT_EQ(buf.read_float32(3), 4.0f);
  EXPECT_FLOAT_EQ(buf.read_float32(4), 5.0f);
}

// -----------------------------------------------------------------------
// Bulk append: Zero count is a no-op
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkZeroCount) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.append_float32_bulk(Span<const float>());
  EXPECT_EQ(buf.row_count(), 0u);
}

// -----------------------------------------------------------------------
// Bulk append: Strings with non-zero base offset
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkStringsNonZeroBaseOffset) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  // Simulates Arrow-style offsets that don't start at 0
  const char string_data[] = "XXXXXhelloworld";
  const uint32_t offsets[] = {5, 10, 15};  // 2 strings: "hello", "world"
  buf.append_strings_bulk(Span<const uint32_t>(offsets, 3), Span<const char>(string_data, 15));

  EXPECT_EQ(buf.row_count(), 2u);
  EXPECT_EQ(buf.read_string(0), "hello");
  EXPECT_EQ(buf.read_string(1), "world");
}

// -----------------------------------------------------------------------
// Bulk append: Validity with bit_offset
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkValidityWithBitOffset) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  const int32_t data[] = {10, 20, 30};
  buf.append_int32_bulk(Span<const int32_t>(data, 3));

  // bitmap = 0b01010000, bit_offset = 4, so bits 4,5,6 = 1,0,1
  // In Arrow's LSB-first layout: byte 0x50 = 0b01010000
  //   bit 4 = (0x50 >> 4) & 1 = 1 (valid)
  //   bit 5 = (0x50 >> 5) & 1 = 0 (null)
  //   bit 6 = (0x50 >> 6) & 1 = 1 (valid)
  const uint8_t bitmap[] = {0x50};
  buf.append_validity_bulk(BitSpan{Span<const uint8_t>(bitmap, 1), 4, 3});

  EXPECT_FALSE(buf.is_null(0));  // bit 4 = 1 -> valid
  EXPECT_TRUE(buf.is_null(1));   // bit 5 = 0 -> null
  EXPECT_FALSE(buf.is_null(2));  // bit 6 = 1 -> valid
}

}  // namespace
}  // namespace PJ::engine
