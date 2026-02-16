#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "pj/engine/chunk.hpp"
#include "pj/engine/topic_storage.hpp"

namespace pj::engine {
namespace {

// ---------------------------------------------------------------------------
// Helper: build and seal a test chunk with given time range
// ---------------------------------------------------------------------------

TopicChunk make_test_chunk(TopicId topic_id, Timestamp t_start, Timestamp t_end,
                           uint32_t num_rows) {
  std::vector<ColumnDescriptor> cols = {
      {0, PrimitiveType::kFloat32, "value"}};
  TopicChunkBuilder builder(topic_id, /*schema_id=*/1, cols, num_rows);
  Timestamp step =
      (num_rows > 1) ? (t_end - t_start) / static_cast<Timestamp>(num_rows - 1)
                     : 0;
  for (uint32_t i = 0; i < num_rows; ++i) {
    builder.begin_row(t_start + static_cast<Timestamp>(i) * step);
    builder.set_float32(0, static_cast<float>(i));
    builder.finish_row();
  }
  return builder.seal();
}

// ===========================================================================
// Test 1: Append chunks
// ===========================================================================

TEST(TopicStorageTest, AppendChunks) {
  TopicDescriptor desc;
  desc.name = "test_topic";
  desc.schema_id = 1;
  desc.dataset_id = 10;

  TopicStorage storage(/*topic_id=*/1, std::move(desc));

  storage.append_sealed_chunk(make_test_chunk(1, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(1, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(1, 3000, 3900, 10));

  EXPECT_EQ(storage.sealed_chunks().size(), 3U);
}

// ===========================================================================
// Test 2: time_min / time_max
// ===========================================================================

TEST(TopicStorageTest, TimeMinMax) {
  TopicDescriptor desc;
  desc.name = "time_range_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/2, std::move(desc));

  // Empty storage returns 0
  EXPECT_EQ(storage.time_min(), 0);
  EXPECT_EQ(storage.time_max(), 0);

  storage.append_sealed_chunk(make_test_chunk(2, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(2, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(2, 3000, 3900, 10));

  EXPECT_EQ(storage.time_min(), 1000);
  EXPECT_EQ(storage.time_max(), 3900);
}

// ===========================================================================
// Test 3: Evict none
// ===========================================================================

TEST(TopicStorageTest, EvictNone) {
  TopicDescriptor desc;
  desc.name = "evict_none_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/3, std::move(desc));

  storage.append_sealed_chunk(make_test_chunk(3, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(3, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(3, 3000, 3900, 10));

  // Evict before the first chunk's t_min -- nothing should be removed
  storage.evict_before(500);
  EXPECT_EQ(storage.sealed_chunks().size(), 3U);

  // Evict at exactly the first chunk's t_min -- still nothing removed
  // because t_max (1900) is not < 1000
  storage.evict_before(1000);
  EXPECT_EQ(storage.sealed_chunks().size(), 3U);
}

// ===========================================================================
// Test 4: Evict some
// ===========================================================================

TEST(TopicStorageTest, EvictSome) {
  TopicDescriptor desc;
  desc.name = "evict_some_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/4, std::move(desc));

  storage.append_sealed_chunk(make_test_chunk(4, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(4, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(4, 3000, 3900, 10));

  // Evict chunks whose t_max < 2500
  // Chunk 1 (t_max=1900 < 2500) -> evicted
  // Chunk 2 (t_max=2900 >= 2500) -> kept
  // Chunk 3 (t_max=3900 >= 2500) -> kept
  storage.evict_before(2500);
  EXPECT_EQ(storage.sealed_chunks().size(), 2U);
  EXPECT_EQ(storage.time_min(), 2000);
  EXPECT_EQ(storage.time_max(), 3900);
}

// ===========================================================================
// Test 5: Evict all
// ===========================================================================

TEST(TopicStorageTest, EvictAll) {
  TopicDescriptor desc;
  desc.name = "evict_all_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/5, std::move(desc));

  storage.append_sealed_chunk(make_test_chunk(5, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(5, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(5, 3000, 3900, 10));

  // Evict with t_keep_min beyond all chunks
  storage.evict_before(5000);
  EXPECT_TRUE(storage.empty());
  EXPECT_EQ(storage.sealed_chunks().size(), 0U);
  EXPECT_EQ(storage.time_min(), 0);
  EXPECT_EQ(storage.time_max(), 0);
}

// ===========================================================================
// Test 6: Metadata
// ===========================================================================

TEST(TopicStorageTest, Metadata) {
  TopicDescriptor desc;
  desc.name = "metadata_topic";
  desc.schema_id = 42;
  desc.dataset_id = 7;

  TopicStorage storage(/*topic_id=*/6, std::move(desc));

  storage.append_sealed_chunk(make_test_chunk(6, 1000, 1900, 10));
  storage.append_sealed_chunk(make_test_chunk(6, 2000, 2900, 10));
  storage.append_sealed_chunk(make_test_chunk(6, 3000, 3900, 10));

  TopicMetadata meta = storage.metadata();

  EXPECT_EQ(meta.topic_id, 6U);
  EXPECT_EQ(meta.name, "metadata_topic");
  EXPECT_EQ(meta.current_schema, 42U);
  EXPECT_EQ(meta.dataset_id, 7U);
  EXPECT_EQ(meta.time_range_min, 1000);
  EXPECT_EQ(meta.time_range_max, 3900);
  EXPECT_EQ(meta.total_row_count, 30U);
  EXPECT_GT(meta.total_byte_size, 0U);
}

// ===========================================================================
// Test 7: Empty
// ===========================================================================

TEST(TopicStorageTest, Empty) {
  TopicDescriptor desc;
  desc.name = "empty_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/7, std::move(desc));

  // Initially empty
  EXPECT_TRUE(storage.empty());

  // After appending, not empty
  storage.append_sealed_chunk(make_test_chunk(7, 1000, 1900, 10));
  EXPECT_FALSE(storage.empty());

  // After evicting all, empty again
  storage.evict_before(5000);
  EXPECT_TRUE(storage.empty());
}

// ===========================================================================
// Test 8: Update schema
// ===========================================================================

TEST(TopicStorageTest, UpdateSchema) {
  TopicDescriptor desc;
  desc.name = "schema_topic";
  desc.schema_id = 1;

  TopicStorage storage(/*topic_id=*/8, std::move(desc));

  EXPECT_EQ(storage.descriptor().schema_id, 1U);

  storage.update_schema(42);
  EXPECT_EQ(storage.descriptor().schema_id, 42U);

  // Metadata should reflect the updated schema
  TopicMetadata meta = storage.metadata();
  EXPECT_EQ(meta.current_schema, 42U);
}

}  // namespace
}  // namespace pj::engine
