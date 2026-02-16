#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string_view>

#include "pj/engine/column_buffer.hpp"

namespace pj::engine {
namespace {

// Helper: create a ColumnDescriptor for a given PrimitiveType.
ColumnDescriptor make_descriptor(PrimitiveType type,
                                 std::string path = "test_field") {
  return ColumnDescriptor{.field_id = 0,
                          .logical_type = type,
                          .field_path = std::move(path)};
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
// 5. Uint8 append/read
// -----------------------------------------------------------------------
TEST(TypedColumnBufferTest, Uint8AppendRead) {
  TypedColumnBuffer buf(make_descriptor(PrimitiveType::kUint8));
  buf.append_uint8(255);
  buf.append_uint8(0);
  buf.append_uint8(128);

  EXPECT_EQ(buf.row_count(), 3u);
  EXPECT_EQ(buf.read_uint8(0), 255);
  EXPECT_EQ(buf.read_uint8(1), 0);
  EXPECT_EQ(buf.read_uint8(2), 128);
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

}  // namespace
}  // namespace pj::engine
