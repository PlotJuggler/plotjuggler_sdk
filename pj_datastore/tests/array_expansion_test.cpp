#include <gtest/gtest.h>

#include "pj_base/dataset.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/reader.hpp"
#include "pj_datastore/writer.hpp"

using namespace PJ;

namespace {

// ─────────────────────────────────────────────────────────────────
// Task 1: flatten_columns_impl for fixed-size and variable arrays
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, FixedSizeArray_Primitive_ProducesNColumns) {
  // Schema: struct msg { float32[3] accel }
  // Expected: 3 columns named accel[0], accel[1], accel[2]
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto accel = PJ::makeArray("accel", PJ::makePrimitive("", PJ::PrimitiveType::kFloat32), 3u);
  auto root = PJ::makeStruct("msg", {accel});
  auto sid = *writer.registerSchema("msg_fixed", root);

  TopicDescriptor desc;
  desc.name = "imu";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 3u);

  EXPECT_EQ(*writer.resolveField(topic_id, "accel[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "accel[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "accel[2]"), 2u);
}

TEST(ArrayExpansionTest, FixedSizeArray_StructElement_ProducesNxMColumns) {
  // Schema: struct msg { Pose[2] poses } where Pose = struct { float32 x, y, z }
  // Expected: 6 columns: poses[0].x, poses[0].y, poses[0].z, poses[1].x, poses[1].y, poses[1].z
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  // Element type: anonymous struct with x, y, z
  auto pose_elem = PJ::makeStruct(
      "", {
              PJ::makePrimitive("x", PJ::PrimitiveType::kFloat32),
              PJ::makePrimitive("y", PJ::PrimitiveType::kFloat32),
              PJ::makePrimitive("z", PJ::PrimitiveType::kFloat32),
          });
  auto poses = PJ::makeArray("poses", pose_elem, 2u);
  auto root = PJ::makeStruct("msg", {poses});
  auto sid = *writer.registerSchema("msg_struct_arr", root);

  TopicDescriptor desc;
  desc.name = "poses_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 6u);

  EXPECT_EQ(*writer.resolveField(topic_id, "poses[0].x"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[0].y"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[0].z"), 2u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[1].x"), 3u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[1].y"), 4u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[1].z"), 5u);
}

TEST(ArrayExpansionTest, VarLenArray_InitiallyZeroColumns) {
  // Schema: struct msg { float64[] data }
  // Variable-length: no fixed_size → 0 columns until expandArray() is called
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("msg_varlen", root);

  TopicDescriptor desc;
  desc.name = "varlen_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto handle = *writer.bindTopicWriter(topic_id);
  EXPECT_EQ(handle.field_ids.size(), 0u);  // 0 columns initially
}

TEST(ArrayExpansionTest, FixedSizeArray_WriteAndRead) {
  // Schema: struct msg { float32[3] accel }
  // Write 4 rows. Read back all values via range_query.
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto accel = PJ::makeArray("accel", PJ::makePrimitive("", PJ::PrimitiveType::kFloat32), 3u);
  auto root = PJ::makeStruct("msg", {accel});
  auto sid = *writer.registerSchema("msg_wr", root);

  TopicDescriptor desc;
  desc.name = "imu_wr";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(writer.beginRow(topic_id, PJ::Timestamp(i) * 1000).has_value());
    writer.set(topic_id, 0, static_cast<float>(i) * 1.0f);
    writer.set(topic_id, 1, static_cast<float>(i) * 2.0f);
    writer.set(topic_id, 2, static_cast<float>(i) * 3.0f);
    ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  }
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 4000});
  std::size_t count = 0;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), count * 1.0, 1e-4);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), count * 2.0, 1e-4);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), count * 3.0, 1e-4);
    ++count;
  });
  EXPECT_EQ(count, 4u);
}

// ─────────────────────────────────────────────────────────────────
// Task 2: DataWriter::expand_array
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, VarLenArray_ExpandArray_AddsColumns) {
  // expandArray("data", 3) on a float64[] topic → 3 columns: data[0], data[1], data[2]
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("exp_test", root);

  TopicDescriptor desc;
  desc.name = "exp_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Initially 0 columns
  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 0u);

  // Expand to 3
  auto result = writer.expandArray(topic_id, "data", 3u);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 3u);

  // Now 3 columns
  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 3u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[2]"), 2u);
}

TEST(ArrayExpansionTest, VarLenArray_ExpandIsIdempotent_ShrinkIsNoop) {
  // expand(3) then expand(2) → no-op; still 3 columns
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("idem_test", root);
  TopicDescriptor desc;
  desc.name = "idem_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 3u).has_value());

  // Try to shrink: must be no-op, return current count (3)
  auto result = writer.expandArray(topic_id, "data", 2u);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 3u);

  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 3u);
}

TEST(ArrayExpansionTest, VarLenArray_WriteAndRead_BasicValues) {
  // expand(3), write 4 rows with all 3 elements, read back values
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("varlen_wr", root);
  TopicDescriptor desc;
  desc.name = "varlen_wr";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 3u).has_value());

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(writer.beginRow(topic_id, PJ::Timestamp(i) * 1000).has_value());
    writer.set(topic_id, 0, i * 1.0);
    writer.set(topic_id, 1, i * 2.0);
    writer.set(topic_id, 2, i * 3.0);
    ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  }
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 4000});
  std::size_t count = 0;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), count * 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), count * 2.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), count * 3.0, 1e-9);
    ++count;
  });
  EXPECT_EQ(count, 4u);
}

TEST(ArrayExpansionTest, VarLenArray_ExpandUnknownTopic_ReturnsError) {
  DataEngine engine;
  DataWriter writer = engine.createWriter();
  auto result = writer.expandArray(/*topic_id=*/999u, "data", 3u);
  EXPECT_FALSE(result.has_value());
}

TEST(ArrayExpansionTest, VarLenArray_ExpandNonArrayField_ReturnsError) {
  // Schema: struct { float64 value } — "value" is not an array
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto root = PJ::makeStruct("msg", {PJ::makePrimitive("value", PJ::PrimitiveType::kFloat64)});
  auto sid = *writer.registerSchema("non_arr", root);
  TopicDescriptor desc;
  desc.name = "non_arr_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto result = writer.expandArray(topic_id, "value", 3u);
  EXPECT_FALSE(result.has_value());
}

// ─────────────────────────────────────────────────────────────────
// Task 3: array_expansion_limit and metadata tracking
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, ExpansionLimit_ClampsColumns) {
  // array_expansion_limit = 4; expand to 10 → actual = 4
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("limit_test", root);

  TopicDescriptor desc;
  desc.name = "limited";
  desc.schema_id = sid;
  desc.array_expansion_limit = 4;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto result = writer.expandArray(topic_id, "data", 10u);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 4u);  // clamped to limit

  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 4u);
}

TEST(ArrayExpansionTest, MaxObservedArrayLength_TrackedInMetadata) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("obs_test", root);
  TopicDescriptor desc;
  desc.name = "observed";
  desc.schema_id = sid;
  desc.array_expansion_limit = 10;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 3u).has_value());

  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  ASSERT_NE(storage, nullptr);
  EXPECT_EQ(storage->maxObservedArrayLength(), 3u);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 5u).has_value());
  EXPECT_EQ(storage->maxObservedArrayLength(), 5u);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 8u).has_value());
  EXPECT_EQ(storage->maxObservedArrayLength(), 8u);
}

TEST(ArrayExpansionTest, TruncatedSampleCount_TrackedOnClamping) {
  // array_expansion_limit = 3. expand(10) → 1 truncation. expand(2) → no truncation.
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("trunc_test", root);
  TopicDescriptor desc;
  desc.name = "truncated";
  desc.schema_id = sid;
  desc.array_expansion_limit = 3;
  auto topic_id = *writer.registerTopic(ds, desc);

  const TopicStorage* storage = engine.getTopicStorage(topic_id);

  // expand to 10 exceeds limit=3 → 1 truncation
  ASSERT_TRUE(writer.expandArray(topic_id, "data", 10u).has_value());
  EXPECT_EQ(storage->truncatedSampleCount(), 1u);

  // expand to 2 — no-op (current=3 >= 2); no new truncation
  ASSERT_TRUE(writer.expandArray(topic_id, "data", 2u).has_value());
  EXPECT_EQ(storage->truncatedSampleCount(), 1u);

  // expand to 20 — actual=3 (same as current), but still truncated (20 > limit)
  ASSERT_TRUE(writer.expandArray(topic_id, "data", 20u).has_value());
  EXPECT_EQ(storage->truncatedSampleCount(), 2u);
}

TEST(ArrayExpansionTest, Metadata_ExposedViaTopicMetadata) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("meta_test", root);
  TopicDescriptor desc;
  desc.name = "meta_topic";
  desc.schema_id = sid;
  desc.array_expansion_limit = 4;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 10u).has_value());  // clamped to 4; 1 truncation; max_observed=10

  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  writer.set(topic_id, 2, 3.0);
  writer.set(topic_id, 3, 4.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto meta = reader.getMetadata(topic_id);
  ASSERT_TRUE(meta.has_value());
  EXPECT_EQ(meta->max_observed_array_length, 10u);
  EXPECT_EQ(meta->truncated_sample_count, 1u);
}

// ─────────────────────────────────────────────────────────────────
// Task 4: Cross-chunk expansion and null auto-fill
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, CrossChunkExpansion_OldChunksHaveFewerColumns) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("cross_chunk", root);
  TopicDescriptor desc;
  desc.name = "cc";
  desc.schema_id = sid;
  desc.max_chunk_rows = 8;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Phase 1: expand to 2, write 10 rows (8 auto-sealed + 2 in builder)
  ASSERT_TRUE(writer.expandArray(topic_id, "data", 2u).has_value());
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(writer.beginRow(topic_id, PJ::Timestamp(i) * 1000).has_value());
    writer.set(topic_id, 0, i * 1.0);
    writer.set(topic_id, 1, i * 2.0);
    ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  }

  // Phase 2: expand to 4 — seals the in-progress builder (rows 8–9), creates new 4-column layout
  ASSERT_TRUE(writer.expandArray(topic_id, "data", 4u).has_value());

  // Write 5 more rows with all 4 elements
  for (int i = 10; i < 15; ++i) {
    ASSERT_TRUE(writer.beginRow(topic_id, PJ::Timestamp(i) * 1000).has_value());
    writer.set(topic_id, 0, i * 1.0);
    writer.set(topic_id, 1, i * 2.0);
    writer.set(topic_id, 2, i * 3.0);
    writer.set(topic_id, 3, i * 4.0);
    ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  }
  engine.commitChunks(writer.flushAll());

  // Verify chunk column counts
  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  ASSERT_NE(storage, nullptr);
  for (const auto& chunk : storage->sealedChunks()) {
    if (chunk.stats.t_max < PJ::Timestamp(10) * 1000) {
      EXPECT_EQ(chunk.columns.size(), 2u) << "pre-expansion chunk must have 2 columns";
    } else {
      EXPECT_EQ(chunk.columns.size(), 4u) << "post-expansion chunk must have 4 columns";
    }
  }

  // Range query: all 15 rows accessible
  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 15000});
  std::size_t count = 0;
  cursor.forEach([&](const SampleRow& row) {
    int i = static_cast<int>(count);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), i * 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), i * 2.0, 1e-9);
    if (i >= 10) {
      EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), i * 3.0, 1e-9);
      EXPECT_NEAR(row.chunk->readNumericAsDouble(3, row.row_index), i * 4.0, 1e-9);
    }
    ++count;
  });
  EXPECT_EQ(count, 15u);
}

TEST(ArrayExpansionTest, UnsetElementsAutoNullFilled) {
  // expand(4), write rows setting only elements [0] and [1].
  // Elements [2] and [3] must be null (auto-filled by finish_row).
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("partial_set", root);
  TopicDescriptor desc;
  desc.name = "partial";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.expandArray(topic_id, "data", 4u).has_value());

  // Write row with only first 2 elements; cols 2 and 3 are unset
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  // col 2 and col 3 intentionally not set
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 2.0, 1e-9);
    EXPECT_TRUE(row.chunk->isNull(2, row.row_index));
    EXPECT_TRUE(row.chunk->isNull(3, row.row_index));
    visited = true;
  });
  EXPECT_TRUE(visited);
}

TEST(ArrayExpansionTest, VarLenStructArray_ExpandAndWrite) {
  // Schema: struct msg { Pose[] poses } where Pose = struct { float32 x, y }
  // expandArray("poses", 2) → 4 columns: poses[0].x, poses[0].y, poses[1].x, poses[1].y
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto pose_elem = PJ::makeStruct(
      "", {
              PJ::makePrimitive("x", PJ::PrimitiveType::kFloat32),
              PJ::makePrimitive("y", PJ::PrimitiveType::kFloat32),
          });
  auto poses_arr = PJ::makeArray("poses", pose_elem, std::nullopt);
  auto root = PJ::makeStruct("msg", {poses_arr});
  auto sid = *writer.registerSchema("struct_arr_var", root);

  TopicDescriptor desc;
  desc.name = "poses";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Initially 0 columns
  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 0u);

  // Expand to 2 Pose elements → 4 columns
  auto result = writer.expandArray(topic_id, "poses", 2u);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 2u);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 4u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[0].x"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[0].y"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[1].x"), 2u);
  EXPECT_EQ(*writer.resolveField(topic_id, "poses[1].y"), 3u);

  // Write and read back
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0f);
  writer.set(topic_id, 1, 2.0f);
  writer.set(topic_id, 2, 3.0f);
  writer.set(topic_id, 3, 4.0f);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 1.0, 1e-4);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 2.0, 1e-4);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), 3.0, 1e-4);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(3, row.row_index), 4.0, 1e-4);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

// ─────────────────────────────────────────────────────────────────
// Bug 1: Second DataWriter for a typed variable-length array topic
// ignores the expanded layout stored in TopicStorage.
//
// Root cause: get_or_create_builder checks `type_tree != nullptr` first.
// For typed topics (schema_id != 0), it always rebuilds from the type tree
// via buildColumnDescriptors(), which yields 0 columns for variable-length
// arrays. The `else` branch that reads storage->columnDescriptors() is
// only reachable when schema_id == 0, so the expanded layout persisted by
// Writer A via setColumnDescriptors() is silently ignored by Writer B.
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, SecondWriter_PicksUpExpandedLayout) {
  // Writer A expands data[] to 3 elements and writes one row.
  // Writer B is created fresh for the same engine/topic.
  // Writer B must see 3 columns — not 0.
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});

  // Writer A: register schema + topic, expand and write
  DataWriter writerA = engine.createWriter();
  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writerA.registerSchema("second_writer_schema", root);

  TopicDescriptor desc;
  desc.name = "second_writer_topic";
  desc.schema_id = sid;
  auto topic_id = *writerA.registerTopic(ds, desc);

  auto expand_result = writerA.expandArray(topic_id, "data", 3u);
  ASSERT_TRUE(expand_result.has_value()) << expand_result.error();
  ASSERT_EQ(*expand_result, 3u);

  ASSERT_TRUE(writerA.beginRow(topic_id, 1000).has_value());
  writerA.set(topic_id, 0, 10.0);
  writerA.set(topic_id, 1, 20.0);
  writerA.set(topic_id, 2, 30.0);
  ASSERT_TRUE(writerA.finishRow(topic_id).has_value());
  engine.commitChunks(writerA.flushAll());

  // Writer B: a brand new DataWriter on the same engine/topic.
  // It must see the 3-column layout that Writer A established.
  DataWriter writerB = engine.createWriter();

  // Verify column count via bind_topic_writer
  auto handle = writerB.bindTopicWriter(topic_id);
  ASSERT_TRUE(handle.has_value()) << handle.error();
  // BUG: This will be 0 instead of 3 before the fix.
  EXPECT_EQ(handle->field_ids.size(), 3u) << "Writer B must inherit the 3-column layout expanded by Writer A";

  // Also verify field resolution works correctly
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[2]"), 2u);
}

// ─────────────────────────────────────────────────────────────────
// Bug 2: expandArray() called while a row is in progress (between
// begin_row and finish_row) originally silently discarded the
// in-progress row.
//
// Root cause: expand_array checked `builder.rowCount() > 0` before
// sealing. rowCount() counts only *completed* rows (incremented in
// finish_row). An active row started with beginRow() but not yet
// finished had rowCount() == 0, so the builder was erased without
// sealing. The incomplete row was permanently lost with no error.
//
// Fixed behavior: expand_array now returns an error when called with
// a row in progress, and the previously completed rows remain intact
// in the pending chunks.
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, ExpandArray_WhileRowInProgress_ReturnsError) {
  // Write two complete rows, begin a third row, then call expand_array.
  // The expand_array call must fail with an error (row in progress).
  // The two previously completed rows must still be accessible after flush.
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("in_progress_schema", root);

  TopicDescriptor desc;
  desc.name = "in_progress_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // First expand so we have 2 columns to write.
  auto init_expand = writer.expandArray(topic_id, "data", 2u);
  ASSERT_TRUE(init_expand.has_value()) << init_expand.error();

  // Row 1 (completed)
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());

  // Row 2 (completed)
  ASSERT_TRUE(writer.beginRow(topic_id, 2000).has_value());
  writer.set(topic_id, 0, 3.0);
  writer.set(topic_id, 1, 4.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());

  // Row 3: begin_row but do NOT call finish_row before expand_array.
  // expand_array must return an error — it must not silently drop the row.
  ASSERT_TRUE(writer.beginRow(topic_id, 3000).has_value());
  writer.set(topic_id, 0, 5.0);
  writer.set(topic_id, 1, 6.0);
  auto expand_result = writer.expandArray(topic_id, "data", 3u);
  EXPECT_FALSE(expand_result.has_value())
      << "expand_array must fail when a row is in progress; got success unexpectedly";

  // Finish the in-progress row, then flush
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  // All 3 rows must be readable — the failed expand did not corrupt data
  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 4000});
  std::size_t count = 0;
  cursor.forEach([&](const SampleRow&) { ++count; });
  EXPECT_EQ(count, 3u) << "All 3 rows must survive; the failed expand must not corrupt the builder";
}

// ─────────────────────────────────────────────────────────────────
// Bug 3: Cross-writer re-expansion of the same array field can
// duplicate existing indices.
//
// Root cause: expandArray() tracks current expansion in
// DataWriter::expanded_arrays_, which is per-writer state. A fresh
// DataWriter sees the persisted expanded columns via TopicStorage, but
// its expanded_arrays_ entry starts at 0. Expanding "data" from 3 to 5
// can incorrectly append data[0], data[1], data[2] again, producing
// 8 columns instead of 5.
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, SecondWriter_ReExpandSameField_DoesNotDuplicateColumns) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});

  // Writer A creates topic and expands data[] to 3.
  DataWriter writerA = engine.createWriter();
  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writerA.registerSchema("reexpand_schema", root);

  TopicDescriptor desc;
  desc.name = "reexpand_topic";
  desc.schema_id = sid;
  auto topic_id = *writerA.registerTopic(ds, desc);

  auto r1 = writerA.expandArray(topic_id, "data", 3u);
  ASSERT_TRUE(r1.has_value()) << r1.error();
  ASSERT_EQ(*r1, 3u);

  // Writer B starts fresh, then expands the same field to 5.
  DataWriter writerB = engine.createWriter();
  auto before = writerB.bindTopicWriter(topic_id);
  ASSERT_TRUE(before.has_value()) << before.error();
  ASSERT_EQ(before->field_ids.size(), 3u);

  auto r2 = writerB.expandArray(topic_id, "data", 5u);
  ASSERT_TRUE(r2.has_value()) << r2.error();
  EXPECT_EQ(*r2, 5u);

  // Expected layout is exactly data[0..4] => 5 columns total.
  auto after = writerB.bindTopicWriter(topic_id);
  ASSERT_TRUE(after.has_value()) << after.error();
  EXPECT_EQ(after->field_ids.size(), 5u)
      << "Re-expanding in a new writer must append only new indices [3..4], not duplicate [0..2]";
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[2]"), 2u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[3]"), 3u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "data[4]"), 4u);
}

// ─────────────────────────────────────────────────────────────────
// Correctness lock-in: mixed-schema topic (scalar + variable-length
// array) — verify column ordering and field IDs after expansion.
//
// This was not tested in the original suite. It verifies that when a
// schema has pre-existing scalar columns, expand_array appends the new
// array element columns at the correct positions with the correct
// FieldIds, and that the scalar column is still readable after the
// layout change.
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, MixedSchema_ScalarPlusVarLenArray_CorrectColumnOrder) {
  // Schema: struct msg { float64 timestamp_sec; float64[] values }
  // After expandArray("values", 3):
  //   col 0 → timestamp_sec  (field_id 0)
  //   col 1 → values[0]      (field_id 1)
  //   col 2 → values[1]      (field_id 2)
  //   col 3 → values[2]      (field_id 3)
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto ts_field = PJ::makePrimitive("timestamp_sec", PJ::PrimitiveType::kFloat64);
  auto values_arr = PJ::makeArray("values", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {ts_field, values_arr});
  auto sid = *writer.registerSchema("mixed_schema", root);

  TopicDescriptor desc;
  desc.name = "mixed_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Before expansion: 1 scalar column only
  auto handle_before = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle_before.field_ids.size(), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "timestamp_sec"), 0u);

  // Expand values[] to 3 elements
  auto result = writer.expandArray(topic_id, "values", 3u);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 3u);

  // Now 4 columns in total
  auto handle_after = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle_after.field_ids.size(), 4u);

  // Verify field path resolution
  EXPECT_EQ(*writer.resolveField(topic_id, "timestamp_sec"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "values[0]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "values[1]"), 2u);
  EXPECT_EQ(*writer.resolveField(topic_id, "values[2]"), 3u);

  // Write and read back to confirm data integrity
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 100.0);  // timestamp_sec
  writer.set(topic_id, 1, 1.0);    // values[0]
  writer.set(topic_id, 2, 2.0);    // values[1]
  writer.set(topic_id, 3, 3.0);    // values[2]
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 100.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), 2.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(3, row.row_index), 3.0, 1e-9);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

// ─────────────────────────────────────────────────────────────────
// Correctness lock-in: two independent expansions of different fields
// on the same topic.
//
// Ensures that expand_array for a second distinct field appends
// correctly after the columns of the first expansion, producing the
// right field_id sequence and readable data.
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, TwoDistinctArrayFields_ExpandBoth_CorrectLayout) {
  // Schema: struct msg { float64[] positions; float64[] velocities }
  // Expand positions to 2, then velocities to 3.
  // Expected columns:
  //   positions[0] (id 0), positions[1] (id 1),
  //   velocities[0] (id 2), velocities[1] (id 3), velocities[2] (id 4)
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto pos_arr = PJ::makeArray("positions", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto vel_arr = PJ::makeArray("velocities", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {pos_arr, vel_arr});
  auto sid = *writer.registerSchema("two_arrays_schema", root);

  TopicDescriptor desc;
  desc.name = "two_arrays_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Initially 0 columns
  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 0u);

  // Expand positions to 2
  auto r1 = writer.expandArray(topic_id, "positions", 2u);
  ASSERT_TRUE(r1.has_value()) << r1.error();
  EXPECT_EQ(*r1, 2u);
  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 2u);

  // Expand velocities to 3
  auto r2 = writer.expandArray(topic_id, "velocities", 3u);
  ASSERT_TRUE(r2.has_value()) << r2.error();
  EXPECT_EQ(*r2, 3u);
  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 5u);

  // Verify field ordering
  EXPECT_EQ(*writer.resolveField(topic_id, "positions[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "positions[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "velocities[0]"), 2u);
  EXPECT_EQ(*writer.resolveField(topic_id, "velocities[1]"), 3u);
  EXPECT_EQ(*writer.resolveField(topic_id, "velocities[2]"), 4u);

  // Write a row and read it back to confirm no data corruption
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  writer.set(topic_id, 2, 10.0);
  writer.set(topic_id, 3, 20.0);
  writer.set(topic_id, 4, 30.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 2.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), 10.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(3, row.row_index), 20.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(4, row.row_index), 30.0, 1e-9);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

// ─────────────────────────────────────────────────────────────────
// Group A: schemaless ensure_column
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, Schemaless_EnsureColumn_AddsColumn) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ec_add";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto fid = writer.ensureColumn(topic_id, "value", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid.has_value()) << fid.error();
  EXPECT_EQ(*fid, 0u);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 1u);

  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 42.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 42.0, 1e-9);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

TEST(ArrayExpansionTest, Schemaless_EnsureColumn_Idempotent) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ec_idem";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto fid1 = writer.ensureColumn(topic_id, "x", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid1.has_value());
  EXPECT_EQ(*fid1, 0u);

  auto fid2 = writer.ensureColumn(topic_id, "x", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid2.has_value());
  EXPECT_EQ(*fid2, 0u);  // same field id — idempotent

  auto handle = *writer.bindTopicWriter(topic_id);
  EXPECT_EQ(handle.field_ids.size(), 1u);  // no duplicate
}

TEST(ArrayExpansionTest, Schemaless_EnsureColumn_MultipleColumns_WriteAndRead) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ec_multi";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  EXPECT_EQ(*writer.ensureColumn(topic_id, "x", PJ::PrimitiveType::kFloat64), 0u);
  EXPECT_EQ(*writer.ensureColumn(topic_id, "y", PJ::PrimitiveType::kFloat64), 1u);
  EXPECT_EQ(*writer.ensureColumn(topic_id, "z", PJ::PrimitiveType::kFloat32), 2u);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 3u);

  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  writer.set(topic_id, 2, 3.0f);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 2.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(2, row.row_index), 3.0, 1e-4);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

TEST(ArrayExpansionTest, Schemaless_EnsureColumn_SecondWriter_PicksUpLayout) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writerA = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ec_second_writer";
  desc.schema_id = 0;
  auto topic_id = *writerA.registerTopic(ds, desc);

  ASSERT_TRUE(writerA.ensureColumn(topic_id, "v", PJ::PrimitiveType::kFloat64).has_value());
  ASSERT_TRUE(writerA.beginRow(topic_id, 1000).has_value());
  writerA.set(topic_id, 0, 99.0);
  ASSERT_TRUE(writerA.finishRow(topic_id).has_value());
  engine.commitChunks(writerA.flushAll());

  DataWriter writerB = engine.createWriter();
  auto handle = writerB.bindTopicWriter(topic_id);
  ASSERT_TRUE(handle.has_value()) << handle.error();
  ASSERT_EQ(handle->field_ids.size(), 1u);
  EXPECT_EQ(*writerB.resolveField(topic_id, "v"), 0u);
}

TEST(ArrayExpansionTest, Schemaless_EnsureColumn_WhileRowInProgress_ReturnsError) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ec_row_progress";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  ASSERT_TRUE(writer.ensureColumn(topic_id, "x", PJ::PrimitiveType::kFloat64).has_value());
  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());

  auto result = writer.ensureColumn(topic_id, "y", PJ::PrimitiveType::kFloat64);
  EXPECT_FALSE(result.has_value()) << "ensure_column must fail while a row is in progress";

  ASSERT_TRUE(writer.finishRow(topic_id).has_value());

  // After finishing the row, ensure_column should auto-seal and succeed.
  auto result2 = writer.ensureColumn(topic_id, "y", PJ::PrimitiveType::kFloat64);
  EXPECT_TRUE(result2.has_value()) << "ensure_column should auto-seal and succeed after rows";
}

// ─────────────────────────────────────────────────────────────────
// Group B: schemaless expand_array
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, Schemaless_ExpandArray_AddsColumns) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ea_add";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto result = writer.expandArray(topic_id, "data", 3u, PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 3u);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 3u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[2]"), 2u);
}

TEST(ArrayExpansionTest, Schemaless_ExpandArray_IncrementalExpansion) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ea_incremental";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto r1 = writer.expandArray(topic_id, "data", 2u, PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(r1.has_value()) << r1.error();
  EXPECT_EQ(*r1, 2u);

  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  auto r2 = writer.expandArray(topic_id, "data", 5u, PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(r2.has_value()) << r2.error();
  EXPECT_EQ(*r2, 5u);

  ASSERT_TRUE(writer.beginRow(topic_id, 2000).has_value());
  writer.set(topic_id, 0, 10.0);
  writer.set(topic_id, 1, 20.0);
  writer.set(topic_id, 2, 30.0);
  writer.set(topic_id, 3, 40.0);
  writer.set(topic_id, 4, 50.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  ASSERT_NE(storage, nullptr);
  ASSERT_GE(storage->sealedChunks().size(), 2u);
  EXPECT_EQ(storage->sealedChunks().back().columns.size(), 5u);
}

TEST(ArrayExpansionTest, Schemaless_ExpandArray_Idempotent_ShrinkIsNoop) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ea_idem";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  EXPECT_EQ(*writer.expandArray(topic_id, "data", 3u, PJ::PrimitiveType::kFloat64), 3u);
  EXPECT_EQ(*writer.expandArray(topic_id, "data", 3u, PJ::PrimitiveType::kFloat64), 3u);
  EXPECT_EQ(*writer.expandArray(topic_id, "data", 1u, PJ::PrimitiveType::kFloat64), 3u);

  EXPECT_EQ(writer.bindTopicWriter(topic_id)->field_ids.size(), 3u);
}

TEST(ArrayExpansionTest, Schemaless_ExpandArray_DefaultType_IsFloat64) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "sl_ea_default_type";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  // No element_type arg — default is kFloat64
  auto result = writer.expandArray(topic_id, "data", 2u);
  ASSERT_TRUE(result.has_value()) << result.error();

  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  ASSERT_NE(storage, nullptr);
  const auto& cols = storage->columnDescriptors();
  ASSERT_EQ(cols.size(), 2u);
  EXPECT_EQ(cols[0].logical_type, PJ::PrimitiveType::kFloat64);
  EXPECT_EQ(cols[1].logical_type, PJ::PrimitiveType::kFloat64);
}

// ─────────────────────────────────────────────────────────────────
// Group C: mixed usage
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, Mixed_EnsureColumnThenExpandArray_NoConflict) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "mixed_ec_ea";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Pre-add data[0] via ensure_column
  auto fid0 = writer.ensureColumn(topic_id, "data[0]", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid0.has_value());
  EXPECT_EQ(*fid0, 0u);

  // expand_array sees data[0] already exists, adds data[1] and data[2]
  auto result = writer.expandArray(topic_id, "data", 3u, PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 3u);

  auto handle = *writer.bindTopicWriter(topic_id);
  EXPECT_EQ(handle.field_ids.size(), 3u);  // exactly 3, no duplicates
  EXPECT_EQ(*writer.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[2]"), 2u);
}

TEST(ArrayExpansionTest, Mixed_ExpandArrayThenEnsureColumn_NoConflict) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "mixed_ea_ec";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  auto r = writer.expandArray(topic_id, "data", 2u, PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(*r, 2u);

  // data[0] already exists — should be no-op, returns existing field id
  auto fid0 = writer.ensureColumn(topic_id, "data[0]", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid0.has_value());
  EXPECT_EQ(*fid0, 0u);

  // extra column not part of array
  auto fid_extra = writer.ensureColumn(topic_id, "extra", PJ::PrimitiveType::kFloat32);
  ASSERT_TRUE(fid_extra.has_value());
  EXPECT_EQ(*fid_extra, 2u);  // new column at index 2

  auto handle = *writer.bindTopicWriter(topic_id);
  EXPECT_EQ(handle.field_ids.size(), 3u);  // data[0], data[1], extra
}

// ─────────────────────────────────────────────────────────────────
// Group C2: regression tests for review findings
// ─────────────────────────────────────────────────────────────────

// Review finding 1: typed expand_array duplicated element columns when
// ensure_column had already added an array-indexed path.
TEST(ArrayExpansionTest, Typed_EnsureColumnThenExpandArray_NoDuplicateColumns) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto data_arr = PJ::makeArray("data", PJ::makePrimitive("", PJ::PrimitiveType::kFloat64), std::nullopt);
  auto root = PJ::makeStruct("msg", {data_arr});
  auto sid = *writer.registerSchema("typed_ec_ea_dedup", root);

  TopicDescriptor desc;
  desc.name = "typed_ec_ea_dedup";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Manually add data[0] via ensureColumn (does not update array_expansion_count)
  auto fid0 = writer.ensureColumn(topic_id, "data[0]", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid0.has_value()) << fid0.error();
  EXPECT_EQ(*fid0, 0u);

  // expand_array for the same typed field: must NOT duplicate data[0]
  auto result = writer.expandArray(topic_id, "data", 3u);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(*result, 3u);

  auto handle = *writer.bindTopicWriter(topic_id);
  EXPECT_EQ(handle.field_ids.size(), 3u) << "Must have exactly 3 columns, no duplicates";
  EXPECT_EQ(*writer.resolveField(topic_id, "data[0]"), 0u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[1]"), 1u);
  EXPECT_EQ(*writer.resolveField(topic_id, "data[2]"), 2u);
}

// Review finding 2: ensure_column must return error when the path already exists
// with a different type instead of silently accepting the mismatch.
TEST(ArrayExpansionTest, EnsureColumn_TypeMismatch_ReturnsError) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  TopicDescriptor desc;
  desc.name = "type_mismatch";
  desc.schema_id = 0;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Register as float64
  auto fid = writer.ensureColumn(topic_id, "value", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid.has_value());
  EXPECT_EQ(*fid, 0u);

  // Re-register same path with float32: must fail
  auto mismatch = writer.ensureColumn(topic_id, "value", PJ::PrimitiveType::kFloat32);
  EXPECT_FALSE(mismatch.has_value()) << "Type mismatch must return error, not silent success";

  // Re-register same path with same type: must succeed (idempotent)
  auto same_type = writer.ensureColumn(topic_id, "value", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(same_type.has_value());
  EXPECT_EQ(*same_type, 0u);  // same field id
}

// ─────────────────────────────────────────────────────────────────
// Group D: typed topic with ensure_column
// ─────────────────────────────────────────────────────────────────

TEST(ArrayExpansionTest, Typed_EnsureColumn_AddsColumnOutsideSchema) {
  DataEngine engine;
  DatasetId ds = *engine.createDataset(PJ::DatasetDescriptor{.source_name = "t"});
  DataWriter writer = engine.createWriter();

  auto root = PJ::makeStruct("msg", {PJ::makePrimitive("x", PJ::PrimitiveType::kFloat64)});
  auto sid = *writer.registerSchema("typed_ensure", root);

  TopicDescriptor desc;
  desc.name = "typed_ensure_topic";
  desc.schema_id = sid;
  auto topic_id = *writer.registerTopic(ds, desc);

  // Schema gives field "x" at id=0
  EXPECT_EQ(*writer.resolveField(topic_id, "x"), 0u);

  // Add an extra column outside the schema
  auto fid = writer.ensureColumn(topic_id, "debug_extra", PJ::PrimitiveType::kFloat64);
  ASSERT_TRUE(fid.has_value()) << fid.error();
  EXPECT_EQ(*fid, 1u);

  auto handle = *writer.bindTopicWriter(topic_id);
  ASSERT_EQ(handle.field_ids.size(), 2u);

  ASSERT_TRUE(writer.beginRow(topic_id, 1000).has_value());
  writer.set(topic_id, 0, 1.0);
  writer.set(topic_id, 1, 2.0);
  ASSERT_TRUE(writer.finishRow(topic_id).has_value());
  engine.commitChunks(writer.flushAll());

  DataReader reader = engine.createReader();
  auto cursor = *reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2000});
  bool visited = false;
  cursor.forEach([&](const SampleRow& row) {
    EXPECT_NEAR(row.chunk->readNumericAsDouble(0, row.row_index), 1.0, 1e-9);
    EXPECT_NEAR(row.chunk->readNumericAsDouble(1, row.row_index), 2.0, 1e-9);
    visited = true;
  });
  EXPECT_TRUE(visited);
}

}  // namespace
