#include "PJ/base/types.hpp"

#include <gtest/gtest.h>

namespace PJ {
namespace {

// ---------- numeric_type_size ----------

TEST(NumericTypeSize, Float32) {
  EXPECT_EQ(numeric_type_size(NumericType::kFloat32), 4);
}

TEST(NumericTypeSize, Float64) {
  EXPECT_EQ(numeric_type_size(NumericType::kFloat64), 8);
}

TEST(NumericTypeSize, Int8) {
  EXPECT_EQ(numeric_type_size(NumericType::kInt8), 1);
}

TEST(NumericTypeSize, Int16) {
  EXPECT_EQ(numeric_type_size(NumericType::kInt16), 2);
}

TEST(NumericTypeSize, Int32) {
  EXPECT_EQ(numeric_type_size(NumericType::kInt32), 4);
}

TEST(NumericTypeSize, Int64) {
  EXPECT_EQ(numeric_type_size(NumericType::kInt64), 8);
}

TEST(NumericTypeSize, Uint8) {
  EXPECT_EQ(numeric_type_size(NumericType::kUint8), 1);
}

TEST(NumericTypeSize, Uint16) {
  EXPECT_EQ(numeric_type_size(NumericType::kUint16), 2);
}

TEST(NumericTypeSize, Uint32) {
  EXPECT_EQ(numeric_type_size(NumericType::kUint32), 4);
}

TEST(NumericTypeSize, Uint64) {
  EXPECT_EQ(numeric_type_size(NumericType::kUint64), 8);
}

// ---------- numeric_value_type ----------

TEST(NumericValueType, Float32) {
  const NumericValue v = 1.5f;
  EXPECT_EQ(numeric_value_type(v), NumericType::kFloat32);
}

TEST(NumericValueType, Float64) {
  const NumericValue v = 2.5;
  EXPECT_EQ(numeric_value_type(v), NumericType::kFloat64);
}

TEST(NumericValueType, Int8) {
  const NumericValue v = static_cast<int8_t>(-1);
  EXPECT_EQ(numeric_value_type(v), NumericType::kInt8);
}

TEST(NumericValueType, Int16) {
  const NumericValue v = static_cast<int16_t>(1000);
  EXPECT_EQ(numeric_value_type(v), NumericType::kInt16);
}

TEST(NumericValueType, Int32) {
  const NumericValue v = static_cast<int32_t>(100000);
  EXPECT_EQ(numeric_value_type(v), NumericType::kInt32);
}

TEST(NumericValueType, Int64) {
  const NumericValue v = static_cast<int64_t>(1LL << 40);
  EXPECT_EQ(numeric_value_type(v), NumericType::kInt64);
}

TEST(NumericValueType, Uint8) {
  const NumericValue v = static_cast<uint8_t>(255);
  EXPECT_EQ(numeric_value_type(v), NumericType::kUint8);
}

TEST(NumericValueType, Uint16) {
  const NumericValue v = static_cast<uint16_t>(65535);
  EXPECT_EQ(numeric_value_type(v), NumericType::kUint16);
}

TEST(NumericValueType, Uint32) {
  const NumericValue v = static_cast<uint32_t>(4000000000u);
  EXPECT_EQ(numeric_value_type(v), NumericType::kUint32);
}

TEST(NumericValueType, Uint64) {
  const NumericValue v = static_cast<uint64_t>(1ULL << 50);
  EXPECT_EQ(numeric_value_type(v), NumericType::kUint64);
}

// ---------- numeric_value_to_double ----------

TEST(NumericValueToDouble, Float32) {
  const NumericValue v = 1.5f;
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 1.5);
}

TEST(NumericValueToDouble, Float64) {
  const NumericValue v = 2.5;
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 2.5);
}

TEST(NumericValueToDouble, Int8) {
  const NumericValue v = static_cast<int8_t>(-1);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), -1.0);
}

TEST(NumericValueToDouble, Int16) {
  const NumericValue v = static_cast<int16_t>(1000);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 1000.0);
}

TEST(NumericValueToDouble, Int32) {
  const NumericValue v = static_cast<int32_t>(100000);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 100000.0);
}

TEST(NumericValueToDouble, Int64) {
  const NumericValue v = static_cast<int64_t>(1LL << 40);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), static_cast<double>(1LL << 40));
}

TEST(NumericValueToDouble, Uint8) {
  const NumericValue v = static_cast<uint8_t>(255);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 255.0);
}

TEST(NumericValueToDouble, Uint16) {
  const NumericValue v = static_cast<uint16_t>(65535);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 65535.0);
}

TEST(NumericValueToDouble, Uint32) {
  const NumericValue v = static_cast<uint32_t>(4000000000u);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), 4000000000.0);
}

TEST(NumericValueToDouble, Uint64) {
  const NumericValue v = static_cast<uint64_t>(1ULL << 50);
  EXPECT_DOUBLE_EQ(numeric_value_to_double(v), static_cast<double>(1ULL << 50));
}

// ---------- kInvalidChunkId ----------

TEST(Constants, InvalidChunkIdIsZero) {
  EXPECT_EQ(kInvalidChunkId, 0);
}

}  // namespace
}  // namespace PJ
