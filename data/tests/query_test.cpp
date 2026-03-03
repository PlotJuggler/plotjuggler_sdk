#include "PJ/engine/query.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <vector>

#include "PJ/base/types.hpp"
#include "PJ/engine/chunk.hpp"

namespace PJ::engine {
namespace {

// Helper: build a test chunk with sequential timestamps.
TopicChunk make_test_chunk(Timestamp t_start, uint32_t num_rows, Timestamp step) {
  std::vector<ColumnDescriptor> cols = {{0, PrimitiveType::kFloat32, "value"}};
  TopicChunkBuilder builder(1, 1, cols, num_rows);
  for (uint32_t i = 0; i < num_rows; ++i) {
    Timestamp t = t_start + static_cast<Timestamp>(i) * step;
    builder.begin_row(t);
    builder.set_float32(0, static_cast<float>(i) * 1.0f);
    builder.finish_row();
  }
  return builder.seal();
}

// Build the standard 5-chunk test fixture:
//   Chunk 0: t=[0,   90],  step=10
//   Chunk 1: t=[100, 190], step=10
//   Chunk 2: t=[200, 290], step=10
//   Chunk 3: t=[300, 390], step=10
//   Chunk 4: t=[400, 490], step=10
std::deque<TopicChunk> make_standard_chunks() {
  std::deque<TopicChunk> chunks;
  for (int i = 0; i < 5; ++i) {
    chunks.push_back(make_test_chunk(static_cast<Timestamp>(i) * 100, 10, 10));
  }
  return chunks;
}

// =========================================================================
// Range query tests
// =========================================================================

TEST(QueryTest, RangeQuerySpanningTwoChunks) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 150, 250);

  std::vector<Timestamp> timestamps;
  while (cursor.valid()) {
    timestamps.push_back(cursor.current().timestamp);
    cursor.advance();
  }

  // Expected: 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250
  ASSERT_EQ(timestamps.size(), 11u);
  EXPECT_EQ(timestamps.front(), 150);
  EXPECT_EQ(timestamps.back(), 250);
  for (std::size_t i = 1; i < timestamps.size(); ++i) {
    EXPECT_EQ(timestamps[i] - timestamps[i - 1], 10);
  }
}

TEST(QueryTest, RangeQueryWithinSingleChunk) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 100, 190);

  std::size_t count = 0;
  while (cursor.valid()) {
    count++;
    cursor.advance();
  }
  EXPECT_EQ(count, 10u);
}

TEST(QueryTest, RangeQueryHittingNoChunks) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 500, 600);
  EXPECT_FALSE(cursor.valid());
}

TEST(QueryTest, RangeQueryAllData) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 0, 490);

  std::size_t count = 0;
  while (cursor.valid()) {
    count++;
    cursor.advance();
  }
  EXPECT_EQ(count, 50u);
}

TEST(QueryTest, RangeQueryExactChunkBoundary) {
  auto chunks = make_standard_chunks();
  // query [100, 199] should return only samples from chunk 1: 100..190
  auto cursor = range_query(chunks, 100, 199);

  std::vector<Timestamp> timestamps;
  while (cursor.valid()) {
    timestamps.push_back(cursor.current().timestamp);
    cursor.advance();
  }

  // Chunk 1 has t = 100, 110, ..., 190. All 10 are in [100, 199].
  ASSERT_EQ(timestamps.size(), 10u);
  EXPECT_EQ(timestamps.front(), 100);
  EXPECT_EQ(timestamps.back(), 190);
}

TEST(QueryTest, ForEachCallback) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 200, 390);

  std::size_t count = 0;
  cursor.for_each([&count](const SampleRow& /*row*/) { ++count; });
  EXPECT_EQ(count, 20u);  // chunks 2 and 3, 10 rows each
}

// =========================================================================
// latest_at tests
// =========================================================================

TEST(QueryTest, LatestAtInMiddleOfChunk) {
  auto chunks = make_standard_chunks();
  auto result = latest_at(chunks, 155);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->timestamp, 150);
}

TEST(QueryTest, LatestAtExactTimestamp) {
  auto chunks = make_standard_chunks();
  auto result = latest_at(chunks, 200);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->timestamp, 200);
}

TEST(QueryTest, LatestAtBeforeAllData) {
  auto chunks = make_standard_chunks();
  auto result = latest_at(chunks, -10);
  EXPECT_FALSE(result.has_value());
}

TEST(QueryTest, LatestAtAfterAllData) {
  auto chunks = make_standard_chunks();
  auto result = latest_at(chunks, 1000);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->timestamp, 490);
}

TEST(QueryTest, LatestAtBetweenChunks) {
  auto chunks = make_standard_chunks();
  // t=95 is between chunk 0 (t_max=90) and chunk 1 (t_min=100)
  auto result = latest_at(chunks, 95);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->timestamp, 90);
}

// =========================================================================
// Empty deque tests
// =========================================================================

TEST(QueryTest, EmptyDequeRangeQuery) {
  std::deque<TopicChunk> empty;
  auto cursor = range_query(empty, 0, 100);
  EXPECT_FALSE(cursor.valid());
}

TEST(QueryTest, EmptyDequeLatestAt) {
  std::deque<TopicChunk> empty;
  auto result = latest_at(empty, 50);
  EXPECT_FALSE(result.has_value());
}

// =========================================================================
// for_each_chunk tests
// =========================================================================

TEST(QueryTest, ForEachChunkMatchesForEach) {
  auto chunks = make_standard_chunks();

  // Collect per-row results via for_each
  auto cursor1 = range_query(chunks, 150, 350);
  std::vector<Timestamp> per_row_ts;
  cursor1.for_each([&](const SampleRow& row) { per_row_ts.push_back(row.timestamp); });

  // Collect per-chunk results via for_each_chunk
  auto cursor2 = range_query(chunks, 150, 350);
  std::vector<Timestamp> chunk_ts;
  cursor2.for_each_chunk([&](const ChunkRowRange& range) {
    for (std::size_t r = range.row_start; r < range.row_end; ++r) {
      chunk_ts.push_back(range.chunk->read_timestamp(r));
    }
  });

  ASSERT_EQ(per_row_ts.size(), chunk_ts.size());
  for (std::size_t i = 0; i < per_row_ts.size(); ++i) {
    EXPECT_EQ(per_row_ts[i], chunk_ts[i]) << "mismatch at index " << i;
  }
}

TEST(QueryTest, ForEachChunkAllData) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 0, 490);

  std::size_t total_rows = 0;
  std::size_t chunk_count = 0;
  cursor.for_each_chunk([&](const ChunkRowRange& range) {
    ++chunk_count;
    total_rows += range.row_end - range.row_start;
  });

  EXPECT_EQ(total_rows, 50u);
  EXPECT_EQ(chunk_count, 5u);
}

TEST(QueryTest, ForEachChunkNoResults) {
  auto chunks = make_standard_chunks();
  auto cursor = range_query(chunks, 500, 600);

  std::size_t count = 0;
  cursor.for_each_chunk([&](const ChunkRowRange& /*range*/) { ++count; });
  EXPECT_EQ(count, 0u);
}

}  // namespace
}  // namespace PJ::engine
