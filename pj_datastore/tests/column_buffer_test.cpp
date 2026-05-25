// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/column_buffer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace PJ {
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
  buf.appendFloat32(1.5f);
  buf.appendFloat32(-3.14f);
  buf.appendFloat32(0.0f);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_FLOAT_EQ(buf.readFloat32(0), 1.5f);
  EXPECT_FLOAT_EQ(buf.readFloat32(1), -3.14f);
  EXPECT_FLOAT_EQ(buf.readFloat32(2), 0.0f);
}

// -----------------------------------------------------------------------
// 2. Float64 append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Float64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  buf.appendFloat64(2.718281828459045);
  buf.appendFloat64(-1e308);

  EXPECT_EQ(buf.rowCount(), 2u);
  EXPECT_DOUBLE_EQ(buf.readFloat64(0), 2.718281828459045);
  EXPECT_DOUBLE_EQ(buf.readFloat64(1), -1e308);
}

// -----------------------------------------------------------------------
// 3. Int32 append/read (including negative)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Int32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  buf.appendInt32(42);
  buf.appendInt32(-100);
  buf.appendInt32(0);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_EQ(buf.readInt32(0), 42);
  EXPECT_EQ(buf.readInt32(1), -100);
  EXPECT_EQ(buf.readInt32(2), 0);
}

// -----------------------------------------------------------------------
// 4. Int64 append/read (large values)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Int64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt64));
  buf.appendInt64(INT64_MAX);
  buf.appendInt64(INT64_MIN);
  buf.appendInt64(0);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_EQ(buf.readInt64(0), INT64_MAX);
  EXPECT_EQ(buf.readInt64(1), INT64_MIN);
  EXPECT_EQ(buf.readInt64(2), 0);
}

// -----------------------------------------------------------------------
// 5. Uint64 append/read (uint8 logical type widens to uint64 storage)
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Uint64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kUint64));
  buf.appendUint64(255);
  buf.appendUint64(0);
  buf.appendUint64(18000000000000000000ULL);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_EQ(buf.readUint64(0), 255U);
  EXPECT_EQ(buf.readUint64(1), 0U);
  EXPECT_EQ(buf.readUint64(2), 18000000000000000000ULL);
}

// -----------------------------------------------------------------------
// 6. Bool append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BoolAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kBool));
  buf.appendBool(true);
  buf.appendBool(false);
  buf.appendBool(true);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_TRUE(buf.readBool(0));
  EXPECT_FALSE(buf.readBool(1));
  EXPECT_TRUE(buf.readBool(2));
}

// -----------------------------------------------------------------------
// 7. String append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, StringAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.appendString("hello");
  buf.appendString("world");
  buf.appendString("test");

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_EQ(buf.readString(0), "hello");
  EXPECT_EQ(buf.readString(1), "world");
  EXPECT_EQ(buf.readString(2), "test");
}

// -----------------------------------------------------------------------
// 8. Null handling
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, NullHandling) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.appendFloat32(1.0f);
  buf.appendNull();
  buf.appendFloat32(3.0f);

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_FALSE(buf.isNull(0));
  EXPECT_TRUE(buf.isNull(1));
  EXPECT_FALSE(buf.isNull(2));

  // Non-null values are still readable.
  EXPECT_FLOAT_EQ(buf.readFloat32(0), 1.0f);
  EXPECT_FLOAT_EQ(buf.readFloat32(2), 3.0f);
}

// -----------------------------------------------------------------------
// 9. has_nulls
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, HasNulls) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  EXPECT_FALSE(buf.hasNulls());

  buf.appendInt32(10);
  EXPECT_FALSE(buf.hasNulls());

  buf.appendNull();
  EXPECT_TRUE(buf.hasNulls());
}

// -----------------------------------------------------------------------
// 10. row_count increments correctly
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, RowCountIncrements) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  EXPECT_EQ(buf.rowCount(), 0u);

  buf.appendFloat64(1.0);
  EXPECT_EQ(buf.rowCount(), 1u);

  buf.appendFloat64(2.0);
  EXPECT_EQ(buf.rowCount(), 2u);

  buf.appendNull();
  EXPECT_EQ(buf.rowCount(), 3u);

  buf.appendFloat64(4.0);
  EXPECT_EQ(buf.rowCount(), 4u);
}

// -----------------------------------------------------------------------
// 11. read_as_double for float32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleFloat32) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.appendFloat32(1.5f);

  EXPECT_DOUBLE_EQ(buf.readAsDouble(0), 1.5);
}

// -----------------------------------------------------------------------
// 12. read_as_double for int32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleInt32) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  buf.appendInt32(42);

  EXPECT_DOUBLE_EQ(buf.readAsDouble(0), 42.0);
}

// -----------------------------------------------------------------------
// 13. Multiple strings of varying lengths
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, MultipleStringsVaryingLengths) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.appendString("a");
  buf.appendString("bb");
  buf.appendString("ccc");
  buf.appendString("dddd");
  buf.appendString("eeeee");

  EXPECT_EQ(buf.rowCount(), 5u);
  EXPECT_EQ(buf.readString(0), "a");
  EXPECT_EQ(buf.readString(1), "bb");
  EXPECT_EQ(buf.readString(2), "ccc");
  EXPECT_EQ(buf.readString(3), "dddd");
  EXPECT_EQ(buf.readString(4), "eeeee");
}

// -----------------------------------------------------------------------
// 14. Empty string
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, EmptyString) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.appendString("");

  EXPECT_EQ(buf.rowCount(), 1u);
  EXPECT_EQ(buf.readString(0), "");
  EXPECT_TRUE(buf.readString(0).empty());
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
  buf.appendBool(true);
  buf.appendBool(false);

  EXPECT_DOUBLE_EQ(buf.readAsDouble(0), 1.0);
  EXPECT_DOUBLE_EQ(buf.readAsDouble(1), 0.0);
}

// -----------------------------------------------------------------------
// Additional: read_as_double for string returns NaN
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ReadAsDoubleStringReturnsNaN) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.appendString("hello");

  EXPECT_TRUE(std::isnan(buf.readAsDouble(0)));
}

// -----------------------------------------------------------------------
// Additional: null in string column
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, NullInStringColumn) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  buf.appendString("hello");
  buf.appendNull();
  buf.appendString("world");

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_FALSE(buf.isNull(0));
  EXPECT_TRUE(buf.isNull(1));
  EXPECT_FALSE(buf.isNull(2));
  EXPECT_EQ(buf.readString(0), "hello");
  EXPECT_EQ(buf.readString(1), "");  // null string reads as empty
  EXPECT_EQ(buf.readString(2), "world");
}

// -----------------------------------------------------------------------
// Additional: buffer accessors are non-empty after appends
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, ValueBufferNonEmpty) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  EXPECT_TRUE(buf.valueBuffer().empty());

  buf.appendInt32(10);
  EXPECT_FALSE(buf.valueBuffer().empty());
  EXPECT_EQ(buf.valueBuffer().size(), sizeof(int32_t));
}

TEST(TypedColumnBufferTest, OffsetsBufferForStrings) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  EXPECT_TRUE(buf.offsetsBuffer().empty());

  buf.appendString("hi");
  // offsets: [0, 2] => 2 * sizeof(uint32_t) = 8
  EXPECT_EQ(buf.offsetsBuffer().size(), 2 * sizeof(uint32_t));
}

// -----------------------------------------------------------------------
// Bulk append: Float32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkFloat32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  const float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  buf.appendFloat32Bulk(Span<const float>(data, 5));

  EXPECT_EQ(buf.rowCount(), 5u);
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_FLOAT_EQ(buf.readFloat32(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Float64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkFloat64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat64));
  const double data[] = {1.1, 2.2, 3.3};
  buf.appendFloat64Bulk(Span<const double>(data, 3));

  EXPECT_EQ(buf.rowCount(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(buf.readFloat64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Int32
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkInt32AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  const int32_t data[] = {-100, 0, 42, 999};
  buf.appendInt32Bulk(Span<const int32_t>(data, 4));

  EXPECT_EQ(buf.rowCount(), 4u);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(buf.readInt32(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Int64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkInt64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt64));
  const int64_t data[] = {INT64_MIN, 0, INT64_MAX};
  buf.appendInt64Bulk(Span<const int64_t>(data, 3));

  EXPECT_EQ(buf.rowCount(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(buf.readInt64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Uint64
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkUint64AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kUint64));
  const uint64_t data[] = {0, 255, 18000000000000000000ULL};
  buf.appendUint64Bulk(Span<const uint64_t>(data, 3));

  EXPECT_EQ(buf.rowCount(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(buf.readUint64(i), data[i]);
  }
}

// -----------------------------------------------------------------------
// Bulk append: Bool
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkBoolAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kBool));
  const uint8_t data[] = {1, 0, 1, 1, 0};
  buf.appendBoolBulk(Span<const uint8_t>(data, 5));

  EXPECT_EQ(buf.rowCount(), 5u);
  EXPECT_TRUE(buf.readBool(0));
  EXPECT_FALSE(buf.readBool(1));
  EXPECT_TRUE(buf.readBool(2));
  EXPECT_TRUE(buf.readBool(3));
  EXPECT_FALSE(buf.readBool(4));
}

// -----------------------------------------------------------------------
// Bulk append: Strings
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkStringAppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  // "hello" "world" "!"
  const char string_data[] = "helloworld!";
  const uint32_t offsets[] = {0, 5, 10, 11};
  buf.appendStringsBulk(Span<const uint32_t>(offsets, 4), Span<const char>(string_data, 11));

  EXPECT_EQ(buf.rowCount(), 3u);
  EXPECT_EQ(buf.readString(0), "hello");
  EXPECT_EQ(buf.readString(1), "world");
  EXPECT_EQ(buf.readString(2), "!");
}

// -----------------------------------------------------------------------
// Bulk append: Validity bitmap
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkValidityBitmap) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  const float data[] = {1.0f, 0.0f, 3.0f, 0.0f};
  buf.appendFloat32Bulk(Span<const float>(data, 4));

  // Validity bitmap: bits [1,0,1,0] = 0b0101 = 0x05
  const uint8_t bitmap[] = {0x05};
  buf.appendValidityBulk(BitSpan{Span<const uint8_t>(bitmap, 1), 0, 4});

  EXPECT_FALSE(buf.isNull(0));
  EXPECT_TRUE(buf.isNull(1));
  EXPECT_FALSE(buf.isNull(2));
  EXPECT_TRUE(buf.isNull(3));
  EXPECT_TRUE(buf.hasNulls());
}

// -----------------------------------------------------------------------
// Bulk append: Mixed single + bulk
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkAfterSingleAppend) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.appendFloat32(1.0f);
  buf.appendFloat32(2.0f);

  const float bulk[] = {3.0f, 4.0f, 5.0f};
  buf.appendFloat32Bulk(Span<const float>(bulk, 3));

  EXPECT_EQ(buf.rowCount(), 5u);
  EXPECT_FLOAT_EQ(buf.readFloat32(0), 1.0f);
  EXPECT_FLOAT_EQ(buf.readFloat32(1), 2.0f);
  EXPECT_FLOAT_EQ(buf.readFloat32(2), 3.0f);
  EXPECT_FLOAT_EQ(buf.readFloat32(3), 4.0f);
  EXPECT_FLOAT_EQ(buf.readFloat32(4), 5.0f);
}

// -----------------------------------------------------------------------
// Bulk append: Zero count is a no-op
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkZeroCount) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kFloat32));
  buf.appendFloat32Bulk(Span<const float>());
  EXPECT_EQ(buf.rowCount(), 0u);
}

// -----------------------------------------------------------------------
// Bulk append: Strings with non-zero base offset
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkStringsNonZeroBaseOffset) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kString));
  // Simulates Arrow-style offsets that don't start at 0
  const char string_data[] = "XXXXXhelloworld";
  const uint32_t offsets[] = {5, 10, 15};  // 2 strings: "hello", "world"
  buf.appendStringsBulk(Span<const uint32_t>(offsets, 3), Span<const char>(string_data, 15));

  EXPECT_EQ(buf.rowCount(), 2u);
  EXPECT_EQ(buf.readString(0), "hello");
  EXPECT_EQ(buf.readString(1), "world");
}

// -----------------------------------------------------------------------
// Bulk append: Validity with bit_offset
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, BulkValidityWithBitOffset) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kInt32));
  const int32_t data[] = {10, 20, 30};
  buf.appendInt32Bulk(Span<const int32_t>(data, 3));

  // bitmap = 0b01010000, bit_offset = 4, so bits 4,5,6 = 1,0,1
  // In Arrow's LSB-first layout: byte 0x50 = 0b01010000
  //   bit 4 = (0x50 >> 4) & 1 = 1 (valid)
  //   bit 5 = (0x50 >> 5) & 1 = 0 (null)
  //   bit 6 = (0x50 >> 6) & 1 = 1 (valid)
  const uint8_t bitmap[] = {0x50};
  buf.appendValidityBulk(BitSpan{Span<const uint8_t>(bitmap, 1), 4, 3});

  EXPECT_FALSE(buf.isNull(0));  // bit 4 = 1 -> valid
  EXPECT_TRUE(buf.isNull(1));   // bit 5 = 0 -> null
  EXPECT_FALSE(buf.isNull(2));  // bit 6 = 1 -> valid
}

}  // namespace
}  // namespace PJ
