// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

// Regression tests for bugs found during code review.
//
// Each test FAILS against the current (buggy) code and PASSES once the
// corresponding fix is applied. Run with ./build.sh --debug && ./test.sh
// (ASAN is required for Bug #2).

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "pj_base/type_tree.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {
namespace {

// ---------------------------------------------------------------------------
// Helper: build and seal a test chunk with given time range.
// Reuses the same pattern as topic_storage_test.cpp::make_test_chunk.
// ---------------------------------------------------------------------------

TopicChunk makeChunkWithRange(TopicId tid, Timestamp t_start, Timestamp t_end, uint32_t num_rows) {
  std::vector<ColumnDescriptor> cols = {{0, PrimitiveType::kFloat32, "v"}};
  TopicChunkBuilder b(tid, /*schema_id=*/1, cols, num_rows);
  Timestamp step = (num_rows > 1) ? (t_end - t_start) / static_cast<Timestamp>(num_rows - 1) : 0;
  for (uint32_t i = 0; i < num_rows; ++i) {
    b.beginRow(t_start + static_cast<Timestamp>(i) * step);
    b.set(0, static_cast<float>(i));
    b.finishRow();
  }
  return b.seal();
}

// ===========================================================================
// Bug #1 — flush() with an in-progress row corrupts chunk stats
//
// writer.cpp:512  flush() checks rowCount() > 0 but not isRowInProgress().
// chunk.cpp:77-78 beginRow() immediately updates stats_.t_min / stats_.t_max.
//
// When flush() is called after beginRow(200) but before finishRow(), the
// sealed chunk has stats_.t_max = 200 even though only the row at t = 100
// was ever committed to timestamps_.
// ===========================================================================

TEST(RegressionTest, Bug1_FlushWithRowInProgress_CorruptsChunkStats) {
  DataEngine engine;
  auto ds = *engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  DataWriter writer = engine.createWriter();

  auto schema = makePrimitive("v", PrimitiveType::kFloat64);
  auto sid = *writer.registerSchema("s", schema);
  auto tid = *writer.registerTopic(ds, TopicDescriptor{.name = "t", .schema_id = sid});

  // Commit one complete row at t=100.
  ASSERT_TRUE(writer.beginRow(tid, 100).has_value());
  writer.set(tid, 0, 1.0);
  ASSERT_TRUE(writer.finishRow(tid).has_value());

  // Start a second row at t=200 but do NOT call finishRow().
  ASSERT_TRUE(writer.beginRow(tid, 200).has_value());
  writer.set(tid, 0, 2.0);

  // flush() should only seal the one committed row.
  auto chunks = writer.flush(tid);

  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].stats.row_count, 1u);
  // BUG: currently returns 200 — the in-progress row's timestamp leaked into stats_.
  EXPECT_EQ(chunks[0].stats.t_max, 100);
  EXPECT_EQ(chunks[0].timestamps.size(), 1u);
}

// ===========================================================================
// Bug #2 — finishBulkAppend() underflows when a column has fewer rows
//          than bulk_pending_rows_
//
// chunk.cpp:249
//   const std::size_t first_row = columns_[col].rowCount() - count;
//
// If the caller appends N timestamps but only N-1 values to a column,
// rowCount() - count wraps to SIZE_MAX. The statistics loop then reads
// buf[SIZE_MAX], which ASAN catches as an out-of-bounds access.
//
// The test uses EXPECT_DEATH; it passes both before the fix (ASAN OOB) and
// after the fix (PJ_ASSERT). Its purpose is to document that this scenario
// must always fail rather than silently corrupting stats.
// ===========================================================================

TEST(RegressionTest, Bug2_FinishBulkAppend_ColumnRowCountMismatch_TriggersUB) {
  // Declare test data outside the EXPECT_DEATH block: the C preprocessor does
  // not track {} when splitting macro arguments, so vector-initializer commas
  // inside the block would be misinterpreted as extra macro arguments.
  std::vector<ColumnDescriptor> cols = {{1, PrimitiveType::kFloat32, "x"}};
  std::vector<Timestamp> ts;
  ts.push_back(100);
  ts.push_back(200);
  ts.push_back(300);
  std::vector<float> vals;
  vals.push_back(1.0f);
  vals.push_back(2.0f);  // one short: 2 values for 3 timestamps

  // PJ_ASSERT behaviour differs by build configuration:
  //   Debug/ASAN  (PJ_ASSERT_THROWS=ON, no NDEBUG): throws std::runtime_error
  //   RelWithDebInfo (no PJ_ASSERT_THROWS, NDEBUG):  assert() compiled away → silent UB
  // The assertion is only verifiable in debug builds.
#ifdef PJ_ASSERT_THROWS
  EXPECT_THROW(
      {
        TopicChunkBuilder builder(/*topic_id=*/1, /*schema_id=*/1, cols, /*max_rows=*/100);
        builder.appendTimestamps(ts);
        builder.appendColumn<float>(0, vals);  // 2 rows appended, pending=3
        builder.finishBulkAppend();            // PJ_ASSERT fires → throws
      },
      std::exception);
#else
  // RelWithDebInfo: NDEBUG disables assert(), so the check cannot be observed at
  // this build level.  Debug builds provide the authoritative verification.
  GTEST_SKIP() << "Bug #2 assertion not verifiable in Release (NDEBUG disables assert())";
#endif
}

// ===========================================================================
// Bug #3 — commitChunks() includes the rejected topic in the returned
//          'changed' set even when appendSealedChunk fails
//
// engine.cpp:141-145
//   PJ_ASSERT(status.has_value(), ...);   // no-op in Release
//   (void)status;
//   changed.push_back(topic_id);          // unconditional!
//
// When an out-of-order chunk is rejected by appendSealedChunk, the chunk is
// discarded but the topic is still added to 'changed'. DerivedEngine then
// marks dependent nodes dirty and schedules a spurious incremental run.
// ===========================================================================

TEST(RegressionTest, Bug3_CommitChunks_ReportsChangedTopicOnRejectedChunk) {
  DataEngine engine;
  auto ds = *engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  DataWriter writer = engine.createWriter();

  auto schema = makePrimitive("v", PrimitiveType::kFloat32);
  auto sid = *writer.registerSchema("s", schema);
  auto tid = *writer.registerTopic(ds, TopicDescriptor{.name = "t", .schema_id = sid});

  // First commit: chunk at t=[100, 200]. Accepted.
  std::vector<std::pair<TopicId, TopicChunk>> batch1;
  batch1.emplace_back(tid, makeChunkWithRange(tid, 100, 200, 2));
  auto changed1 = engine.commitChunks(std::move(batch1));
  ASSERT_EQ(changed1.size(), 1u);

  // Second commit: chunk at t=[50, 150] — out of order (t_min=50 < last t_min=100).
  // appendSealedChunk rejects it. commitChunks should return an empty changed list
  // without throwing.
  //   - Debug (PJ_ASSERT_THROWS): currently throws std::runtime_error.
  //   - Release: currently returns {tid} (ASSERT is a no-op; push_back is unconditional).
  // Both behaviours are bugs. The correct behaviour is: return empty, no throw.
  std::vector<std::pair<TopicId, TopicChunk>> batch2;
  batch2.emplace_back(tid, makeChunkWithRange(tid, 50, 150, 2));
  std::vector<TopicId> changed2;
  ASSERT_NO_THROW(changed2 = engine.commitChunks(std::move(batch2)));
  EXPECT_TRUE(changed2.empty());
}

// ===========================================================================
// Bug #4 — appendSealedChunk accepts overlapping time ranges
//
// topic_storage.cpp:15
//   if (!sealed_chunks_.empty() && chunk.stats.t_min < sealed_chunks_.back().stats.t_min)
//
// The guard only checks new.t_min < last.t_min. A chunk whose t_min falls
// inside the previous chunk's [t_min, t_max] passes the check even though
// it creates a temporal overlap. This violates the non-overlapping invariant
// assumed by latestAt() and RangeCursor.
//
// Example: Chunk1=[100,500], Chunk2=[400,600]. 400 >= 100 → accepted (BUG).
// ===========================================================================

TEST(RegressionTest, Bug4_AppendSealedChunk_AcceptsOverlappingTimeRange) {
  TopicDescriptor desc;
  desc.name = "t";
  desc.schema_id = 1;
  desc.dataset_id = 1;
  TopicStorage storage(/*topic_id=*/1, std::move(desc));

  // Chunk1: t=[100, 500].
  ASSERT_TRUE(storage.appendSealedChunk(makeChunkWithRange(1, 100, 500, 5)).has_value());

  // Chunk2: t=[400, 600] — overlaps Chunk1 in [400, 500]. Should be rejected.
  auto result = storage.appendSealedChunk(makeChunkWithRange(1, 400, 600, 3));
  // BUG: currently has_value() == true (overlap silently accepted).
  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace PJ
