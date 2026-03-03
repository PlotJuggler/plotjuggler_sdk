#include "PJ/engine/chunk.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace PJ::engine {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a vector of ColumnDescriptors
// ---------------------------------------------------------------------------

ColumnDescriptor make_col(FieldId id, PrimitiveType type, std::string path) {
  return ColumnDescriptor{id, type, std::move(path)};
}

// ===========================================================================
// Test 1: Build and seal float32 chunk
// ===========================================================================

TEST(ChunkTest, BuildAndSealFloat32Chunk) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
      make_col(2, PrimitiveType::kFloat32, "y"),
      make_col(3, PrimitiveType::kFloat32, "z"),
  };
  TopicChunkBuilder builder(/*topic_id=*/10, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  // Add 5 rows
  for (uint32_t i = 0; i < 5; ++i) {
    Timestamp ts = 1000 + static_cast<Timestamp>(i) * 100;
    builder.begin_row(ts);
    builder.set_float32(0, static_cast<float>(i) * 1.0F);
    builder.set_float32(1, static_cast<float>(i) * 2.0F);
    builder.set_float32(2, static_cast<float>(i) * 3.0F);
    builder.finish_row();
  }

  EXPECT_EQ(builder.row_count(), 5U);
  EXPECT_FALSE(builder.is_full());

  const auto& stats = builder.stats();
  EXPECT_EQ(stats.t_min, 1000);
  EXPECT_EQ(stats.t_max, 1400);
  EXPECT_EQ(stats.row_count, 5U);

  // Column 0 (x): values 0, 1, 2, 3, 4
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].max_value, 4.0);

  // Column 1 (y): values 0, 2, 4, 6, 8
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].max_value, 8.0);

  // Column 2 (z): values 0, 3, 6, 9, 12
  EXPECT_DOUBLE_EQ(*stats.column_stats[2].min_value, 0.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[2].max_value, 12.0);

  TopicChunk chunk = builder.seal();
  EXPECT_NE(chunk.id, 0U);
  EXPECT_EQ(chunk.topic_id, 10U);
  EXPECT_EQ(chunk.schema_version, 1U);
  EXPECT_EQ(chunk.stats.row_count, 5U);
  EXPECT_EQ(chunk.column_encodings.size(), 3U);
  for (std::size_t c = 0; c < 3; ++c) {
    EXPECT_EQ(chunk.column_encodings[c], EncodingType::kRaw);
  }
}

// ===========================================================================
// Test 2: Read back sealed values
// ===========================================================================

TEST(ChunkTest, ReadBackSealedValues) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
      make_col(2, PrimitiveType::kFloat64, "y"),
      make_col(3, PrimitiveType::kInt32, "z"),
  };
  TopicChunkBuilder builder(/*topic_id=*/20, /*schema_id=*/2, std::move(cols), /*max_rows=*/100);

  Timestamp timestamps[] = {1000, 1100, 1200, 1300, 1400};
  float x_vals[] = {1.5F, 2.5F, 3.5F, 4.5F, 5.5F};
  double y_vals[] = {10.0, 20.0, 30.0, 40.0, 50.0};
  int32_t z_vals[] = {-1, 0, 1, 2, 3};

  for (int i = 0; i < 5; ++i) {
    builder.begin_row(timestamps[i]);
    builder.set_float32(0, x_vals[i]);
    builder.set_float64(1, y_vals[i]);
    builder.set_int32(2, z_vals[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  // Read back timestamps
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(chunk.read_timestamp(i), timestamps[i]) << "row " << i;
  }

  // Read back float32 column as double
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, i)), x_vals[i]) << "row " << i;
  }

  // Read back float64 column
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(1, i), y_vals[i]) << "row " << i;
  }

  // Read back int32 column as double (may be FOR-encoded since range is small)
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(2, i), static_cast<double>(z_vals[i])) << "row " << i;
  }
}

// ===========================================================================
// Test 3: is_full
// ===========================================================================

TEST(ChunkTest, IsFull) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/30, /*schema_id=*/1, std::move(cols), /*max_rows=*/3);

  EXPECT_FALSE(builder.is_full());
  EXPECT_EQ(builder.row_count(), 0U);

  for (uint32_t i = 0; i < 3; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float32(0, static_cast<float>(i));
    builder.finish_row();
  }

  EXPECT_TRUE(builder.is_full());
  EXPECT_EQ(builder.row_count(), 3U);
}

// ===========================================================================
// Test 4: String column
// ===========================================================================

TEST(ChunkTest, StringColumn) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kString, "label"),
  };
  TopicChunkBuilder builder(/*topic_id=*/40, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  std::string_view strings[] = {"hello", "world", "hello", "world"};
  for (int i = 0; i < 4; ++i) {
    builder.begin_row(static_cast<Timestamp>(i * 100));
    builder.set_string(0, strings[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kDictionary);
  const auto& dict = std::get<encoding::DictionaryEncoded>(chunk.encoding_data[0]);
  // 2 unique strings: "hello" and "world"
  EXPECT_EQ(dict.dictionary.size(), 2U);

  // Read back all strings
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(chunk.read_string(0, i), strings[i]) << "row " << i;
  }
}

// ===========================================================================
// Test 5: Bool column
// ===========================================================================

TEST(ChunkTest, BoolColumn) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kBool, "flag"),
  };
  TopicChunkBuilder builder(/*topic_id=*/50, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  bool bools[] = {true, false, true, true, false};
  for (int i = 0; i < 5; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_bool(0, bools[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kPackedBool);

  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(chunk.read_bool(0, i), bools[i]) << "row " << i;
  }
}

// ===========================================================================
// Test 6: Null handling
// ===========================================================================

TEST(ChunkTest, NullHandling) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/60, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  // Row 0: 10.0, Row 1: null, Row 2: 30.0, Row 3: null, Row 4: 50.0
  builder.begin_row(100);
  builder.set_float64(0, 10.0);
  builder.finish_row();

  builder.begin_row(200);
  builder.set_null(0);
  builder.finish_row();

  builder.begin_row(300);
  builder.set_float64(0, 30.0);
  builder.finish_row();

  builder.begin_row(400);
  builder.set_null(0);
  builder.finish_row();

  builder.begin_row(500);
  builder.set_float64(0, 50.0);
  builder.finish_row();

  const auto& stats = builder.stats();
  EXPECT_EQ(stats.column_stats[0].null_count, 2U);

  TopicChunk chunk = builder.seal();

  EXPECT_FALSE(chunk.is_null(0, 0));
  EXPECT_TRUE(chunk.is_null(0, 1));
  EXPECT_FALSE(chunk.is_null(0, 2));
  EXPECT_TRUE(chunk.is_null(0, 3));
  EXPECT_FALSE(chunk.is_null(0, 4));

  // Non-null values should read back correctly
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 0), 10.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 2), 30.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 4), 50.0);
}

// ===========================================================================
// Test 7: Mixed types
// ===========================================================================

TEST(ChunkTest, MixedTypes) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "position"),
      make_col(2, PrimitiveType::kString, "label"),
      make_col(3, PrimitiveType::kBool, "active"),
  };
  TopicChunkBuilder builder(/*topic_id=*/70, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(1000);
  builder.set_float32(0, 1.5F);
  builder.set_string(1, "alpha");
  builder.set_bool(2, true);
  builder.finish_row();

  builder.begin_row(2000);
  builder.set_float32(0, 2.5F);
  builder.set_string(1, "beta");
  builder.set_bool(2, false);
  builder.finish_row();

  builder.begin_row(3000);
  builder.set_float32(0, 3.5F);
  builder.set_string(1, "alpha");
  builder.set_bool(2, true);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  // Check encodings
  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kRaw);
  EXPECT_EQ(chunk.column_encodings[1], EncodingType::kDictionary);
  EXPECT_EQ(chunk.column_encodings[2], EncodingType::kPackedBool);

  // Read back all values
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 0)), 1.5F);
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 1)), 2.5F);
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 2)), 3.5F);

  EXPECT_EQ(chunk.read_string(1, 0), "alpha");
  EXPECT_EQ(chunk.read_string(1, 1), "beta");
  EXPECT_EQ(chunk.read_string(1, 2), "alpha");

  EXPECT_TRUE(chunk.read_bool(2, 0));
  EXPECT_FALSE(chunk.read_bool(2, 1));
  EXPECT_TRUE(chunk.read_bool(2, 2));

  // Timestamps
  EXPECT_EQ(chunk.read_timestamp(0), 1000);
  EXPECT_EQ(chunk.read_timestamp(1), 2000);
  EXPECT_EQ(chunk.read_timestamp(2), 3000);
}

// ===========================================================================
// Test 8: Column stats (min/max, is_constant, run_count)
// ===========================================================================

TEST(ChunkTest, ColumnStatsNumeric) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "varying"),
      make_col(2, PrimitiveType::kFloat64, "constant"),
  };
  TopicChunkBuilder builder(/*topic_id=*/80, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  // varying: -5, 0, 10, 3, 10
  // constant: 42, 42, 42, 42, 42
  double varying[] = {-5.0, 0.0, 10.0, 3.0, 10.0};
  for (int i = 0; i < 5; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float64(0, varying[i]);
    builder.set_float64(1, 42.0);
    builder.finish_row();
  }

  const auto& stats = builder.stats();

  // Varying column
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].min_value, -5.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[0].max_value, 10.0);
  EXPECT_FALSE(stats.column_stats[0].is_constant);
  // run_count: -5->0 (change), 0->10 (change), 10->3 (change), 3->10 (change) = 1 + 4 = 5
  EXPECT_EQ(stats.column_stats[0].run_count, 5U);

  // Constant column
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].min_value, 42.0);
  EXPECT_DOUBLE_EQ(*stats.column_stats[1].max_value, 42.0);
  EXPECT_TRUE(stats.column_stats[1].is_constant);
  EXPECT_EQ(stats.column_stats[1].run_count, 1U);
}

// ===========================================================================
// Test: Unique chunk IDs
// ===========================================================================

TEST(ChunkTest, UniqueChunkIds) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };

  TopicChunkBuilder builder1(1, 1, cols, 10);
  builder1.begin_row(100);
  builder1.set_float32(0, 1.0F);
  builder1.finish_row();
  TopicChunk c1 = builder1.seal();

  TopicChunkBuilder builder2(1, 1, cols, 10);
  builder2.begin_row(200);
  builder2.set_float32(0, 2.0F);
  builder2.finish_row();
  TopicChunk c2 = builder2.seal();

  EXPECT_NE(c1.id, c2.id);
  EXPECT_NE(c1.id, kInvalidChunkId);
  EXPECT_NE(c2.id, kInvalidChunkId);
}

// ===========================================================================
// Test: Integer types round-trip
// ===========================================================================

TEST(ChunkTest, IntegerTypesRoundTrip) {
  // int8/int16 logical types widen to int64 storage; int32 has its own storage;
  // uint8/uint16/uint32 widen to uint64 storage.
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt8, "i8"),    make_col(2, PrimitiveType::kInt16, "i16"),
      make_col(3, PrimitiveType::kInt32, "i32"),  make_col(4, PrimitiveType::kInt64, "i64"),
      make_col(5, PrimitiveType::kUint8, "u8"),   make_col(6, PrimitiveType::kUint16, "u16"),
      make_col(7, PrimitiveType::kUint32, "u32"), make_col(8, PrimitiveType::kUint64, "u64"),
  };
  TopicChunkBuilder builder(/*topic_id=*/90, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(1000);
  builder.set_int64(0, -42);      // int8 → int64 storage
  builder.set_int64(1, -1000);    // int16 → int64 storage
  builder.set_int32(2, -999999);  // int32 → int32 storage
  builder.set_int64(3, 123456789012345LL);
  builder.set_uint64(4, 255);          // uint8 → uint64 storage
  builder.set_uint64(5, 65535);        // uint16 → uint64 storage
  builder.set_uint64(6, 4000000000U);  // uint32 → uint64 storage
  builder.set_uint64(7, 18000000000000000000ULL);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  // Single-row chunks will be constant-encoded, but readback should be the same
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 0), -42.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(1, 0), -1000.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(2, 0), -999999.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(3, 0), 123456789012345.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(4, 0), 255.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(5, 0), 65535.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(6, 0), 4000000000.0);
  // uint64 large values may lose precision in double, so just check close
  EXPECT_NEAR(chunk.read_numeric_as_double(7, 0), 1.8e19, 1e4);
}

// ===========================================================================
// Test: No nulls means is_null always returns false
// ===========================================================================

TEST(ChunkTest, NoNullsIsNullReturnsFalse) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/100, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  for (int i = 0; i < 3; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float32(0, static_cast<float>(i));
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(chunk.is_null(0, i)) << "row " << i;
  }
}

// ===========================================================================
// Test: String column is_constant and run_count
// ===========================================================================

TEST(ChunkTest, StringColumnStats) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kString, "tag"),
  };
  TopicChunkBuilder builder(/*topic_id=*/110, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  // All same string -> is_constant = true, run_count = 1
  for (int i = 0; i < 4; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_string(0, "same");
    builder.finish_row();
  }

  const auto& stats = builder.stats();
  EXPECT_TRUE(stats.column_stats[0].is_constant);
  EXPECT_EQ(stats.column_stats[0].run_count, 1U);
  // String columns should not have numeric min/max
  EXPECT_FALSE(stats.column_stats[0].min_value.has_value());
  EXPECT_FALSE(stats.column_stats[0].max_value.has_value());
}

// ===========================================================================
// Test: Empty chunk (0 rows)
// ===========================================================================

TEST(ChunkTest, EmptyChunk) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/120, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  EXPECT_EQ(builder.row_count(), 0U);
  EXPECT_FALSE(builder.is_full());

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.stats.row_count, 0U);
  EXPECT_TRUE(chunk.timestamps.empty());
}

// ===========================================================================
// Test: Bulk read column as doubles (float32)
// ===========================================================================

TEST(ChunkTest, BulkReadFloat32) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
  };
  TopicChunkBuilder builder(/*topic_id=*/130, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  constexpr int kRows = 10;
  for (int i = 0; i < kRows; ++i) {
    builder.begin_row(static_cast<Timestamp>(i * 100));
    builder.set_float32(0, static_cast<float>(i) * 1.5F);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  // Read all rows
  std::vector<double> out(kRows);
  chunk.read_column_as_doubles(0, Span<double>(out), 0);
  for (int i = 0; i < kRows; ++i) {
    EXPECT_FLOAT_EQ(static_cast<float>(out[static_cast<std::size_t>(i)]), static_cast<float>(i) * 1.5F) << "row " << i;
  }

  // Read a sub-range [3, 7)
  std::vector<double> sub(4);
  chunk.read_column_as_doubles(0, Span<double>(sub), 3);
  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(static_cast<float>(sub[static_cast<std::size_t>(i)]), static_cast<float>(i + 3) * 1.5F)
        << "sub row " << i;
  }
}

// ===========================================================================
// Test: Bulk read column as doubles (int64)
// ===========================================================================

TEST(ChunkTest, BulkReadInt64) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt64, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/140, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  constexpr int kRows = 5;
  int64_t values[] = {-100, 0, 42, 999, -1};
  for (int i = 0; i < kRows; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_int64(0, values[i]);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  std::vector<double> out(kRows);
  chunk.read_column_as_doubles(0, Span<double>(out), 0);
  for (int i = 0; i < kRows; ++i) {
    EXPECT_DOUBLE_EQ(out[static_cast<std::size_t>(i)], static_cast<double>(values[i])) << "row " << i;
  }
}

// ===========================================================================
// Test: Bulk read bool/string columns returns NaN
// ===========================================================================

TEST(ChunkTest, BulkReadBoolStringReturnsNaN) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kBool, "flag"),
      make_col(2, PrimitiveType::kString, "label"),
  };
  TopicChunkBuilder builder(/*topic_id=*/150, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(100);
  builder.set_bool(0, true);
  builder.set_string(1, "hello");
  builder.finish_row();

  builder.begin_row(200);
  builder.set_bool(0, false);
  builder.set_string(1, "world");
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  std::vector<double> out(2);

  chunk.read_column_as_doubles(0, Span<double>(out.data(), 2), 0);
  EXPECT_TRUE(std::isnan(out[0]));
  EXPECT_TRUE(std::isnan(out[1]));

  chunk.read_column_as_doubles(1, Span<double>(out.data(), 2), 0);
  EXPECT_TRUE(std::isnan(out[0]));
  EXPECT_TRUE(std::isnan(out[1]));
}

// ===========================================================================
// Test: Bulk read zero rows (boundary)
// ===========================================================================

TEST(ChunkTest, BulkReadZeroRows) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "x"),
  };
  TopicChunkBuilder builder(/*topic_id=*/160, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(100);
  builder.set_float64(0, 1.0);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  // Should not crash when reading 0 rows
  double dummy = 0.0;
  chunk.read_column_as_doubles(0, Span<double>(&dummy, 0), 0);
  EXPECT_DOUBLE_EQ(dummy, 0.0);  // untouched
}

// ===========================================================================
// NEW: Constant int column gets constant encoding
// ===========================================================================

TEST(ChunkTest, ConstantIntColumnGetsConstantEncoding) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt32, "const_val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/200, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  for (int i = 0; i < 100; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_int32(0, 42);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kConstant);

  // Read back every row
  for (std::size_t i = 0; i < 100; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, i), 42.0) << "row " << i;
  }

  // Bulk read
  std::vector<double> out(100);
  chunk.read_column_as_doubles(0, Span<double>(out), 0);
  for (std::size_t i = 0; i < 100; ++i) {
    EXPECT_DOUBLE_EQ(out[i], 42.0) << "bulk row " << i;
  }
}

// ===========================================================================
// NEW: Constant float column gets constant encoding
// ===========================================================================

TEST(ChunkTest, ConstantFloatColumnGetsConstantEncoding) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "const_f64"),
  };
  TopicChunkBuilder builder(/*topic_id=*/201, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  for (int i = 0; i < 50; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float64(0, 3.14);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kConstant);

  for (std::size_t i = 0; i < 50; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, i), 3.14) << "row " << i;
  }
}

// ===========================================================================
// NEW: Narrow range int column gets FOR encoding
// ===========================================================================

TEST(ChunkTest, NarrowRangeIntColumnGetsFOR) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt32, "narrow"),
  };
  TopicChunkBuilder builder(/*topic_id=*/202, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  // Values in [1000, 1100] — range=100, fits in uint8 (1 byte vs 4 native)
  for (int i = 0; i < 101; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_int32(0, 1000 + static_cast<int32_t>(i));
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kFrameOfReference);
  const auto& for_enc = std::get<encoding::FrameOfReferenceEncoded>(chunk.encoding_data[0]);
  EXPECT_EQ(for_enc.offset_bytes, 1);
  EXPECT_EQ(for_enc.reference, 1000);

  // Per-row read
  for (std::size_t i = 0; i < 101; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, i), 1000.0 + static_cast<double>(i)) << "row " << i;
  }

  // Bulk read
  std::vector<double> out(101);
  chunk.read_column_as_doubles(0, Span<double>(out), 0);
  for (std::size_t i = 0; i < 101; ++i) {
    EXPECT_DOUBLE_EQ(out[i], 1000.0 + static_cast<double>(i)) << "bulk row " << i;
  }
}

// ===========================================================================
// NEW: Wide range int column stays raw
// ===========================================================================

TEST(ChunkTest, WideRangeIntColumnStaysRaw) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt32, "wide"),
  };
  TopicChunkBuilder builder(/*topic_id=*/203, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  // Range that spans full int32 — FOR can't narrow below 4 bytes
  builder.begin_row(0);
  builder.set_int32(0, -2000000000);
  builder.finish_row();

  builder.begin_row(1);
  builder.set_int32(0, 2000000000);
  builder.finish_row();

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kRaw);

  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 0), -2000000000.0);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, 1), 2000000000.0);
}

// ===========================================================================
// NEW: Float column always stays raw (never gets FOR)
// ===========================================================================

TEST(ChunkTest, FloatColumnAlwaysStaysRaw) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "f32"),
      make_col(2, PrimitiveType::kFloat64, "f64"),
  };
  TopicChunkBuilder builder(/*topic_id=*/204, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  // Varying float values
  for (int i = 0; i < 10; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_float32(0, static_cast<float>(i) * 0.1F);
    builder.set_float64(1, static_cast<double>(i) * 0.1);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kRaw);
  EXPECT_EQ(chunk.column_encodings[1], EncodingType::kRaw);
}

// ===========================================================================
// NEW: Constant bool column gets constant encoding
// ===========================================================================

TEST(ChunkTest, ConstantBoolGetsConstantEncoding) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kBool, "always_true"),
  };
  TopicChunkBuilder builder(/*topic_id=*/205, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  for (int i = 0; i < 50; ++i) {
    builder.begin_row(static_cast<Timestamp>(i));
    builder.set_bool(0, true);
    builder.finish_row();
  }

  TopicChunk chunk = builder.seal();

  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kConstant);

  for (std::size_t i = 0; i < 50; ++i) {
    EXPECT_TRUE(chunk.read_bool(0, i)) << "row " << i;
  }
}

// ===========================================================================
// Bulk append: basic float32
// ===========================================================================

TEST(ChunkTest, BulkAppendFloat32) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
      make_col(2, PrimitiveType::kFloat32, "y"),
  };
  TopicChunkBuilder builder(/*topic_id=*/500, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  constexpr std::size_t N = 100;
  std::vector<Timestamp> ts(N);
  std::vector<float> x_vals(N), y_vals(N);
  for (std::size_t i = 0; i < N; ++i) {
    ts[i] = static_cast<Timestamp>(i) * 10;
    x_vals[i] = static_cast<float>(i) * 1.0F;
    y_vals[i] = static_cast<float>(i) * 2.0F;
  }

  builder.append_timestamps(ts);
  builder.append_column_float32(0, x_vals);
  builder.append_column_float32(1, y_vals);
  builder.finish_bulk_append();

  EXPECT_EQ(builder.row_count(), 100U);
  EXPECT_EQ(builder.last_timestamp(), 990);

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.stats.row_count, 100U);
  EXPECT_EQ(chunk.stats.t_min, 0);
  EXPECT_EQ(chunk.stats.t_max, 990);

  // Verify round-trip
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(chunk.read_timestamp(i), static_cast<Timestamp>(i) * 10);
    EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, i)), x_vals[i]);
    EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(1, i)), y_vals[i]);
  }
}

// ===========================================================================
// Bulk append: stats are correct
// ===========================================================================

TEST(ChunkTest, BulkAppendStats) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/501, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  const double data[] = {3.0, 1.0, 4.0, 1.0, 5.0};
  const Timestamp ts[] = {10, 20, 30, 40, 50};

  builder.append_timestamps(Span<const Timestamp>(ts, 5));
  builder.append_column_float64(0, Span<const double>(data, 5));
  builder.finish_bulk_append();

  const auto& cs = builder.stats().column_stats[0];
  EXPECT_DOUBLE_EQ(*cs.min_value, 1.0);
  EXPECT_DOUBLE_EQ(*cs.max_value, 5.0);
  EXPECT_FALSE(cs.is_constant);
  EXPECT_GT(cs.run_count, 1U);
}

// ===========================================================================
// Bulk append: constant column
// ===========================================================================

TEST(ChunkTest, BulkAppendConstantColumn) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kInt32, "const"),
  };
  TopicChunkBuilder builder(/*topic_id=*/502, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  constexpr std::size_t N = 50;
  std::vector<Timestamp> ts(N);
  std::vector<int32_t> vals(N, 42);
  for (std::size_t i = 0; i < N; ++i) {
    ts[i] = static_cast<Timestamp>(i);
  }

  builder.append_timestamps(ts);
  builder.append_column_int32(0, vals);
  builder.finish_bulk_append();

  const auto& cs = builder.stats().column_stats[0];
  EXPECT_TRUE(cs.is_constant);
  EXPECT_DOUBLE_EQ(*cs.min_value, 42.0);
  EXPECT_DOUBLE_EQ(*cs.max_value, 42.0);

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.column_encodings[0], EncodingType::kConstant);
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(0, i), 42.0);
  }
}

// ===========================================================================
// Bulk append: string column
// ===========================================================================

TEST(ChunkTest, BulkAppendStrings) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kString, "name"),
  };
  TopicChunkBuilder builder(/*topic_id=*/503, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  const char string_data[] = "alphaBravoCharlie";
  const uint32_t offsets[] = {0, 5, 10, 17};
  const Timestamp ts[] = {10, 20, 30};

  builder.append_timestamps(Span<const Timestamp>(ts, 3));
  builder.append_column_strings(0, Span<const uint32_t>(offsets, 4), Span<const char>(string_data, 17));
  builder.finish_bulk_append();

  EXPECT_EQ(builder.row_count(), 3U);

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.read_string(0, 0), "alpha");
  EXPECT_EQ(chunk.read_string(0, 1), "Bravo");
  EXPECT_EQ(chunk.read_string(0, 2), "Charlie");
}

// ===========================================================================
// Bulk append: remaining_capacity
// ===========================================================================

TEST(ChunkTest, BulkRemainingCapacity) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "x"),
  };
  TopicChunkBuilder builder(/*topic_id=*/504, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  EXPECT_EQ(builder.remaining_capacity(), 100U);

  const Timestamp ts[] = {1, 2, 3};
  const float vals[] = {1.0F, 2.0F, 3.0F};
  builder.append_timestamps(Span<const Timestamp>(ts, 3));
  builder.append_column_float32(0, Span<const float>(vals, 3));
  builder.finish_bulk_append();

  EXPECT_EQ(builder.remaining_capacity(), 97U);
}

// ===========================================================================
// Bulk append: with validity bitmap
// ===========================================================================

TEST(ChunkTest, BulkAppendWithValidity) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat64, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/505, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  const double data[] = {1.0, 0.0, 3.0, 0.0};
  const Timestamp ts[] = {10, 20, 30, 40};

  builder.append_timestamps(Span<const Timestamp>(ts, 4));
  builder.append_column_float64(0, Span<const double>(data, 4));
  // validity: bits [1, 0, 1, 0] = 0b0101 = 0x05
  const uint8_t bitmap[] = {0x05};
  builder.append_column_validity(0, BitSpan{Span<const uint8_t>(bitmap, 1), 0, 4});
  builder.finish_bulk_append();

  TopicChunk chunk = builder.seal();
  EXPECT_FALSE(chunk.is_null(0, 0));
  EXPECT_TRUE(chunk.is_null(0, 1));
  EXPECT_FALSE(chunk.is_null(0, 2));
  EXPECT_TRUE(chunk.is_null(0, 3));
  EXPECT_EQ(chunk.stats.column_stats[0].null_count, 2U);
}

// ===========================================================================
// Bulk append: mixed types
// ===========================================================================

TEST(ChunkTest, BulkAppendMixedTypes) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "f32"),
      make_col(2, PrimitiveType::kInt64, "i64"),
      make_col(3, PrimitiveType::kBool, "b"),
  };
  TopicChunkBuilder builder(/*topic_id=*/506, /*schema_id=*/1, std::move(cols), /*max_rows=*/1000);

  const Timestamp ts[] = {100, 200, 300};
  const float f32[] = {1.0F, 2.0F, 3.0F};
  const int64_t i64[] = {10, 20, 30};
  const uint8_t bools[] = {1, 0, 1};

  builder.append_timestamps(Span<const Timestamp>(ts, 3));
  builder.append_column_float32(0, Span<const float>(f32, 3));
  builder.append_column_int64(1, Span<const int64_t>(i64, 3));
  builder.append_column_bool(2, Span<const uint8_t>(bools, 3));
  builder.finish_bulk_append();

  TopicChunk chunk = builder.seal();
  EXPECT_EQ(chunk.stats.row_count, 3U);
  EXPECT_FLOAT_EQ(static_cast<float>(chunk.read_numeric_as_double(0, 1)), 2.0F);
  EXPECT_DOUBLE_EQ(chunk.read_numeric_as_double(1, 2), 30.0);
  EXPECT_TRUE(chunk.read_bool(2, 0));
  EXPECT_FALSE(chunk.read_bool(2, 1));
  EXPECT_TRUE(chunk.read_bool(2, 2));
}

// ===========================================================================
// Death tests: debug asserts catch misuse
// Active in debug builds (assert) or when PJ_ASSERT_THROWS is defined.
// ===========================================================================

#if !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)

#ifdef PJ_ASSERT_THROWS
#define PJ_EXPECT_ASSERT_FAIL(stmt, msg) EXPECT_THROW(stmt, std::runtime_error)
#else
#define PJ_EXPECT_ASSERT_FAIL(stmt, msg) ASSERT_DEATH(stmt, msg)
#endif

TEST(ChunkDeathTest, SetWithoutBeginRowAsserts) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/300, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  PJ_EXPECT_ASSERT_FAIL(builder.set_float32(0, 1.0F), "set_float32 called without begin_row");
}

TEST(ChunkDeathTest, FinishRowWithoutBeginRowAsserts) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/301, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  PJ_EXPECT_ASSERT_FAIL(builder.finish_row(), "finish_row called without begin_row");
}

TEST(ChunkDeathTest, BeginRowWhileRowInProgressAsserts) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/302, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(100);
  PJ_EXPECT_ASSERT_FAIL(builder.begin_row(200), "begin_row called while row already in progress");
}

TEST(ChunkDeathTest, OutOfBoundsColIndexAsserts) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/303, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(100);
  PJ_EXPECT_ASSERT_FAIL(builder.set_float32(5, 1.0F), "col_index out of bounds");
}

TEST(ChunkDeathTest, OutOfOrderTimestampAsserts) {
  std::vector<ColumnDescriptor> cols = {
      make_col(1, PrimitiveType::kFloat32, "val"),
  };
  TopicChunkBuilder builder(/*topic_id=*/304, /*schema_id=*/1, std::move(cols), /*max_rows=*/100);

  builder.begin_row(200);
  builder.set_float32(0, 1.0F);
  builder.finish_row();

  PJ_EXPECT_ASSERT_FAIL(builder.begin_row(100), "timestamps must be monotonically non-decreasing");
}

#endif  // !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)

}  // namespace
}  // namespace PJ::engine
