/**
 * @file materialized_series_view_test.cpp
 * @brief MaterializedSeriesView over hand-built Arrow structs: the null samples it reports.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {
namespace {

void releaseSchema(::ArrowSchema* schema) {
  std::memset(schema, 0, sizeof(*schema));
}

void releaseArray(::ArrowArray* array) {
  std::memset(array, 0, sizeof(*array));
}

/// A {timestamp: int64, value: float64} struct of four samples, as a host's readSeriesArrow hands
/// it out. `validity` is the value column's bitmap (nullptr: none) and `null_count` what the
/// producer declares (-1: not counted). The storage outlives every view built from it.
struct Series {
  std::array<int64_t, 4> stamps{10, 20, 30, 40};
  std::array<double, 4> values{1.5, 0.0, 3.5, 0.0};
  std::array<const void*, 2> stamp_buffers{nullptr, stamps.data()};
  std::array<const void*, 2> value_buffers{nullptr, values.data()};
  ::ArrowSchema stamp_schema{};
  ::ArrowSchema value_schema{};
  std::array<::ArrowSchema*, 2> schema_children{&stamp_schema, &value_schema};
  ::ArrowArray stamp_array{};
  ::ArrowArray value_array{};
  std::array<::ArrowArray*, 2> array_children{&stamp_array, &value_array};

  MaterializedSeriesView view(const uint8_t* validity, int64_t null_count) {
    stamp_schema.format = "l";
    value_schema.format = "g";
    stamp_array.length = value_array.length = 4;
    stamp_array.n_buffers = value_array.n_buffers = 2;
    stamp_array.buffers = stamp_buffers.data();
    value_buffers[0] = validity;
    value_array.buffers = value_buffers.data();
    value_array.null_count = null_count;

    ArrowSchemaHolder schema;
    ::ArrowSchema* raw_schema = schema.out();
    raw_schema->format = "+s";
    raw_schema->n_children = 2;
    raw_schema->children = schema_children.data();
    raw_schema->release = releaseSchema;
    ArrowArrayHolder array;
    ::ArrowArray* raw_array = array.out();
    raw_array->length = 4;
    raw_array->n_children = 2;
    raw_array->children = array_children.data();
    raw_array->release = releaseArray;
    return MaterializedSeriesView(std::move(schema), std::move(array));
  }
};

TEST(MaterializedSeriesViewTest, AColumnWithoutABitmapHasNoNulls) {
  Series series;
  const MaterializedSeriesView view = series.view(nullptr, 0);
  ASSERT_EQ(view.rowCount(), 4U);
  EXPECT_EQ(view.nullCount(), 0U);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_FALSE(view.isNull(row)) << row;
  }
}

// Rows 1 and 3 are null: their zeroed value slots are not samples of 0.
TEST(MaterializedSeriesViewTest, ReportsTheNullSamplesOfItsValueColumn) {
  Series series;
  const uint8_t validity = 0b0101;
  const MaterializedSeriesView view = series.view(&validity, 2);
  EXPECT_EQ(view.nullCount(), 2U);
  EXPECT_FALSE(view.isNull(0));
  EXPECT_TRUE(view.isNull(1));
  EXPECT_FALSE(view.isNull(2));
  EXPECT_TRUE(view.isNull(3));
  EXPECT_FALSE(view.isNull(4)) << "past the end";
  ASSERT_NE(view.valuesAsFloat64(), nullptr);
  EXPECT_EQ(view.valuesAsFloat64()[2], 3.5);
}

TEST(MaterializedSeriesViewTest, CountsNullsTheProducerDidNotCount) {
  Series series;
  const uint8_t validity = 0b0111;
  const MaterializedSeriesView view = series.view(&validity, -1);
  EXPECT_EQ(view.nullCount(), 1U);
  EXPECT_TRUE(view.isNull(3));
}

// A bitmap may ride along with a declared count of zero: nothing is null then.
TEST(MaterializedSeriesViewTest, ADeclaredCountOfZeroWinsOverTheBitmap) {
  Series series;
  const uint8_t validity = 0b0000;
  const MaterializedSeriesView view = series.view(&validity, 0);
  EXPECT_EQ(view.nullCount(), 0U);
  EXPECT_FALSE(view.isNull(1));
}

TEST(MaterializedSeriesViewTest, AnEmptyViewHasNoNulls) {
  const MaterializedSeriesView view;
  EXPECT_EQ(view.nullCount(), 0U);
  EXPECT_FALSE(view.isNull(0));
}

}  // namespace
}  // namespace PJ::sdk
