#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/dataset.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/reader.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/type_registry.hpp"
#include "pj/engine/type_tree.hpp"
#include "pj/engine/types.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine {
namespace {

// ===========================================================================
// Test 1: End-to-end scalar write + read
//
// Creates an engine, registers a float64 scalar series, appends 5000 values
// (spanning multiple chunks at default max_chunk_rows=1024), flushes,
// commits, then verifies range_query returns all 5000 rows and latest_at
// returns the correct value at the midpoint.
// ===========================================================================

TEST(EngineIntegrationTest, EndToEndScalarWriteRead) {
  DataEngine engine;

  // Create dataset with default time domain id=0
  auto dataset_id_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "test_source", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.ok()) << dataset_id_or.status();
  DatasetId dataset_id = *dataset_id_or;

  // Create writer, register scalar series (float64)
  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(
      dataset_id, "temperature", NumericType::kFloat64);
  ASSERT_TRUE(handle_or.ok()) << handle_or.status();
  ScalarSeriesHandle handle = *handle_or;

  // Append 5000 scalar values: timestamps 0, 1000, 2000, ...
  // Values: i * 0.5
  constexpr std::size_t kRowCount = 5000;
  for (std::size_t i = 0; i < kRowCount; ++i) {
    Timestamp ts = static_cast<Timestamp>(i) * 1000;
    double value = static_cast<double>(i) * 0.5;
    writer.append_scalar(handle, ts, value);
  }

  // Flush all pending chunks and commit to engine
  auto flushed = writer.flush_all();
  EXPECT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  // Verify via reader: range_query full range
  DataReader reader = engine.create_reader();

  std::size_t count = 0;
  auto cursor = reader.range_query(
      QueryRange{.topic_id = handle.topic_id,
                 .t_min = 0,
                 .t_max = static_cast<Timestamp>(kRowCount - 1) * 1000});
  cursor.for_each([&count](const SampleRow& row) {
    (void)row;
    ++count;
  });
  EXPECT_EQ(count, kRowCount);

  // Verify multiple chunks were created (5000 rows / 1024 max = 5 chunks)
  const TopicStorage* storage = engine.get_topic_storage(handle.topic_id);
  ASSERT_NE(storage, nullptr);
  EXPECT_GE(storage->sealed_chunks().size(), 2U);

  // latest_at at midpoint: t = 2500 * 1000 = 2500000
  // Expected value at i=2500: 2500 * 0.5 = 1250.0
  Timestamp midpoint_ts = static_cast<Timestamp>(2500) * 1000;
  auto latest = reader.latest_at(
      QueryPoint{.topic_id = handle.topic_id, .t = midpoint_ts});
  ASSERT_TRUE(latest.found);
  EXPECT_EQ(latest.timestamp, midpoint_ts);
  ASSERT_NE(latest.chunk, nullptr);
  double midpoint_value =
      latest.chunk->read_numeric_as_double(0, latest.row_index);
  EXPECT_DOUBLE_EQ(midpoint_value, 1250.0);

  // Also verify metadata
  auto metadata_opt = reader.get_metadata(handle.topic_id);
  ASSERT_TRUE(metadata_opt.has_value());
  EXPECT_EQ(metadata_opt->total_row_count, kRowCount);
  EXPECT_EQ(metadata_opt->time_range_min, 0);
  EXPECT_EQ(metadata_opt->time_range_max,
            static_cast<Timestamp>(kRowCount - 1) * 1000);
}

// ===========================================================================
// Test 2: End-to-end structured write + read
//
// Registers a schema for a robot_pose struct (float32 x, y, z + string
// frame_name), creates a topic bound to that schema, writes 200 rows,
// flushes, commits, then verifies field values round-trip via range_query
// and checks that the string column uses dictionary encoding.
// ===========================================================================

TEST(EngineIntegrationTest, EndToEndStructuredWriteRead) {
  DataEngine engine;

  // Build type tree: struct robot_pose { float32 x, y, z; string frame_name }
  auto x = make_primitive("x", PrimitiveType::kFloat32);
  auto y = make_primitive("y", PrimitiveType::kFloat32);
  auto z = make_primitive("z", PrimitiveType::kFloat32);
  auto frame = make_primitive("frame_name", PrimitiveType::kString);
  auto robot_pose = make_struct("robot_pose", {x, y, z, frame});

  // Create dataset
  auto dataset_id_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "robot", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.ok()) << dataset_id_or.status();
  DatasetId dataset_id = *dataset_id_or;

  // Register schema and create topic via writer
  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("robot_pose", robot_pose);
  ASSERT_TRUE(schema_id_or.ok()) << schema_id_or.status();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "pose";
  topic_desc.schema_id = schema_id;
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.ok()) << topic_id_or.status();
  TopicId topic_id = *topic_id_or;

  // Bind to get field IDs (column indices)
  auto write_handle_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(write_handle_or.ok()) << write_handle_or.status();
  TopicWriteHandle write_handle = *write_handle_or;
  ASSERT_EQ(write_handle.field_ids.size(), 4U);

  // Column layout after flatten: col 0=x, col 1=y, col 2=z, col 3=frame_name
  constexpr std::size_t kRows = 200;
  for (std::size_t i = 0; i < kRows; ++i) {
    Timestamp ts = static_cast<Timestamp>(i) * 1000000;  // 1ms apart
    writer.begin_row(topic_id, ts);
    writer.set_float32(topic_id, 0, static_cast<float>(i) * 1.0F);
    writer.set_float32(topic_id, 1, static_cast<float>(i) * 2.0F);
    writer.set_float32(topic_id, 2, static_cast<float>(i) * 3.0F);
    // Alternate frame name for variety
    std::string_view frame_name = (i % 2 == 0) ? "base_link" : "odom";
    writer.set_string(topic_id, 3, frame_name);
    writer.finish_row(topic_id);
  }

  // Flush + commit
  auto flushed = writer.flush_all();
  EXPECT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  // Read back via reader
  DataReader reader = engine.create_reader();

  // Range query: verify total count
  std::size_t count = 0;
  auto cursor = reader.range_query(
      QueryRange{.topic_id = topic_id,
                 .t_min = 0,
                 .t_max = static_cast<Timestamp>(kRows - 1) * 1000000});
  // Collect first and last rows for verification
  SampleRow first_row{};
  SampleRow last_row{};
  cursor.for_each([&count, &first_row, &last_row](const SampleRow& row) {
    if (count == 0) {
      first_row = row;
    }
    last_row = row;
    ++count;
  });
  EXPECT_EQ(count, kRows);

  // Verify first row (i=0): x=0, y=0, z=0, frame_name="base_link"
  ASSERT_NE(first_row.chunk, nullptr);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          first_row.chunk->read_numeric_as_double(0, first_row.row_index)),
      0.0F);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          first_row.chunk->read_numeric_as_double(1, first_row.row_index)),
      0.0F);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          first_row.chunk->read_numeric_as_double(2, first_row.row_index)),
      0.0F);
  EXPECT_EQ(first_row.chunk->read_string(3, first_row.row_index),
            "base_link");

  // Verify last row (i=199): x=199, y=398, z=597, frame_name="odom"
  ASSERT_NE(last_row.chunk, nullptr);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          last_row.chunk->read_numeric_as_double(0, last_row.row_index)),
      199.0F);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          last_row.chunk->read_numeric_as_double(1, last_row.row_index)),
      398.0F);
  EXPECT_FLOAT_EQ(
      static_cast<float>(
          last_row.chunk->read_numeric_as_double(2, last_row.row_index)),
      597.0F);
  EXPECT_EQ(last_row.chunk->read_string(3, last_row.row_index), "odom");

  // Verify dictionary encoding on the string column (col 3) in sealed chunks
  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  ASSERT_NE(storage, nullptr);
  for (const auto& chunk : storage->sealed_chunks()) {
    ASSERT_GT(chunk.column_encodings.size(), 3U);
    EXPECT_EQ(chunk.column_encodings[3], EncodingType::kDictionary)
        << "String column should use dictionary encoding";
    ASSERT_TRUE(chunk.dictionary_data[3].has_value());
    // At most 2 unique values: "base_link" and "odom"
    EXPECT_LE(chunk.dictionary_data[3]->dictionary.size(), 2U);
  }

  // Verify type tree is retrievable via reader
  const TypeTreeNode* tree = reader.get_type_tree(topic_id);
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->name, "robot_pose");
  EXPECT_EQ(tree->kind, TypeKind::kStruct);
  EXPECT_EQ(tree->children.size(), 4U);
}

// ===========================================================================
// Test 3: Retention eviction
//
// Writes data spanning a timestamp range, commits, then enforces retention
// to evict old chunks. Verifies that old data is gone and recent data
// remains intact.
// ===========================================================================

TEST(EngineIntegrationTest, RetentionEviction) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "retention_test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.ok()) << dataset_id_or.status();
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();

  // Use small max_chunk_rows to force many chunks for fine-grained eviction
  // We use scalar API but with a custom topic for control over chunk size
  auto x_tree = make_primitive("value", PrimitiveType::kFloat64);
  auto schema_id_or = writer.register_schema("scalar_retention", x_tree);
  ASSERT_TRUE(schema_id_or.ok()) << schema_id_or.status();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "sensor";
  topic_desc.schema_id = schema_id;
  topic_desc.max_chunk_rows = 100;  // small chunks for easier eviction testing
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.ok()) << topic_id_or.status();
  TopicId topic_id = *topic_id_or;

  auto write_handle_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(write_handle_or.ok()) << write_handle_or.status();

  // Write 3000 rows: timestamps 0 to 9999 (spacing ~3.333)
  // Simplify: timestamps from 0 to 2999, one per integer timestamp
  constexpr std::size_t kRowCount = 3000;
  for (std::size_t i = 0; i < kRowCount; ++i) {
    Timestamp ts = static_cast<Timestamp>(i);
    writer.begin_row(topic_id, ts);
    writer.set_float64(topic_id, 0, static_cast<double>(i) * 0.1);
    writer.finish_row(topic_id);
  }

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  // Verify all data present
  DataReader reader = engine.create_reader();
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2999});
    cursor.for_each([&count](const SampleRow&) { ++count; });
    EXPECT_EQ(count, kRowCount);
  }

  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  ASSERT_NE(storage, nullptr);
  std::size_t chunks_before = storage->sealed_chunks().size();
  EXPECT_GT(chunks_before, 1U);

  // Enforce retention window of 1500ns.
  // t_max = 2999, so evict_before(2999 - 1500 = 1499).
  // Chunks with t_max < 1499 are evicted.
  engine.enforce_retention(1500);

  std::size_t chunks_after = storage->sealed_chunks().size();
  EXPECT_LT(chunks_after, chunks_before)
      << "Some chunks should have been evicted";

  // Query old range [0, 999]: should return fewer or zero rows
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 999});
    cursor.for_each([&count](const SampleRow&) { ++count; });
    // Old chunks (t_max < 1499) are fully evicted.
    // Chunks with rows in [0, 999] and t_max < 1499 are gone.
    // At chunk size 100: chunks [0..99], [100..199], ..., [900..999] all have
    // t_max < 1499, so they should be evicted.
    EXPECT_EQ(count, 0U) << "Old data should be fully evicted";
  }

  // Query recent range [1500, 2999]: should return all data in that range
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 1500, .t_max = 2999});
    cursor.for_each([&count](const SampleRow&) { ++count; });
    EXPECT_EQ(count, 1500U) << "Recent data should be intact";
  }
}

// ===========================================================================
// Test 4: Schema evolution
//
// Registers a topic with schema v1 (x, y, z as float32), writes 100 rows,
// evolves the schema to v2 (adds w as float32), writes 100 more rows with
// the new column, then verifies that old rows have 3 columns and new rows
// have 4 columns accessible.
// ===========================================================================

TEST(EngineIntegrationTest, SchemaEvolution) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "evolution", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.ok()) << dataset_id_or.status();
  DatasetId dataset_id = *dataset_id_or;

  // Schema v1: struct { float32 x, y, z }
  auto x = make_primitive("x", PrimitiveType::kFloat32);
  auto y = make_primitive("y", PrimitiveType::kFloat32);
  auto z = make_primitive("z", PrimitiveType::kFloat32);
  auto schema_v1 = make_struct("pose", {x, y, z});

  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("pose", schema_v1);
  ASSERT_TRUE(schema_id_or.ok()) << schema_id_or.status();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "position";
  topic_desc.schema_id = schema_id;
  topic_desc.max_chunk_rows = 200;  // Ensure v1 data fits in one chunk
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.ok()) << topic_id_or.status();
  TopicId topic_id = *topic_id_or;

  auto wh_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(wh_or.ok()) << wh_or.status();
  EXPECT_EQ(wh_or->field_ids.size(), 3U);

  // Write 100 rows with v1 schema (3 columns)
  for (std::size_t i = 0; i < 100; ++i) {
    Timestamp ts = static_cast<Timestamp>(i) * 1000;
    writer.begin_row(topic_id, ts);
    writer.set_float32(topic_id, 0, static_cast<float>(i) * 1.0F);
    writer.set_float32(topic_id, 1, static_cast<float>(i) * 2.0F);
    writer.set_float32(topic_id, 2, static_cast<float>(i) * 3.0F);
    writer.finish_row(topic_id);
  }

  // Flush v1 data and commit
  auto flushed_v1 = writer.flush_all();
  EXPECT_FALSE(flushed_v1.empty());
  engine.commit_chunks(std::move(flushed_v1));

  // Evolve schema: add float32 w
  auto w = make_primitive("w", PrimitiveType::kFloat32);
  auto schema_v2 = make_struct("pose", {x, y, z, w});
  auto evolve_status = engine.type_registry().evolve_schema(schema_id, schema_v2);
  ASSERT_TRUE(evolve_status.ok()) << evolve_status;

  // Create a new writer so it picks up the evolved schema's column layout
  DataWriter writer2 = engine.create_writer();
  auto wh2_or = writer2.bind_topic_writer(topic_id);
  ASSERT_TRUE(wh2_or.ok()) << wh2_or.status();
  EXPECT_EQ(wh2_or->field_ids.size(), 4U);

  // Write 100 more rows with v2 schema (4 columns)
  for (std::size_t i = 0; i < 100; ++i) {
    Timestamp ts = static_cast<Timestamp>(100 + i) * 1000;
    writer2.begin_row(topic_id, ts);
    writer2.set_float32(topic_id, 0, static_cast<float>(100 + i) * 1.0F);
    writer2.set_float32(topic_id, 1, static_cast<float>(100 + i) * 2.0F);
    writer2.set_float32(topic_id, 2, static_cast<float>(100 + i) * 3.0F);
    writer2.set_float32(topic_id, 3, static_cast<float>(100 + i) * 4.0F);
    writer2.finish_row(topic_id);
  }

  auto flushed_v2 = writer2.flush_all();
  EXPECT_FALSE(flushed_v2.empty());
  engine.commit_chunks(std::move(flushed_v2));

  // Read back data spanning both versions
  DataReader reader = engine.create_reader();

  // Query old rows [0, 99000]: should return 100 rows with 3 columns
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 99000});
    cursor.for_each([&count](const SampleRow& row) {
      ASSERT_NE(row.chunk, nullptr);
      // Old chunks should have 3 column descriptors
      EXPECT_EQ(row.chunk->column_descriptors.size(), 3U);
      if (count == 0) {
        // Verify first old row: x=0, y=0, z=0
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(0, row.row_index)),
            0.0F);
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(1, row.row_index)),
            0.0F);
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(2, row.row_index)),
            0.0F);
      }
      ++count;
    });
    EXPECT_EQ(count, 100U);
  }

  // Query new rows [100000, 199000]: should return 100 rows with 4 columns
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 100000, .t_max = 199000});
    cursor.for_each([&count](const SampleRow& row) {
      ASSERT_NE(row.chunk, nullptr);
      // New chunks should have 4 column descriptors
      EXPECT_EQ(row.chunk->column_descriptors.size(), 4U);
      if (count == 0) {
        // Verify first new row (i=100): x=100, y=200, z=300, w=400
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(0, row.row_index)),
            100.0F);
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(1, row.row_index)),
            200.0F);
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(2, row.row_index)),
            300.0F);
        EXPECT_FLOAT_EQ(
            static_cast<float>(
                row.chunk->read_numeric_as_double(3, row.row_index)),
            400.0F);
      }
      ++count;
    });
    EXPECT_EQ(count, 100U);
  }

  // Verify full range returns all 200 rows
  {
    std::size_t count = 0;
    auto cursor = reader.range_query(
        QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 199000});
    cursor.for_each([&count](const SampleRow&) { ++count; });
    EXPECT_EQ(count, 200U);
  }
}

// ===========================================================================
// Test 5: Time domain offset
//
// Creates 2 time domains with different names and display_offsets, creates
// datasets bound to each, and verifies the offsets are stored and
// retrievable. Also verifies the display_time = raw_time - offset
// relationship.
// ===========================================================================

TEST(EngineIntegrationTest, TimeDomainOffset) {
  DataEngine engine;

  // Create two time domains
  auto td1_or = engine.create_time_domain("ros_time");
  ASSERT_TRUE(td1_or.ok()) << td1_or.status();
  TimeDomainId td1_id = *td1_or;

  auto td2_or = engine.create_time_domain("sim_time");
  ASSERT_TRUE(td2_or.ok()) << td2_or.status();
  TimeDomainId td2_id = *td2_or;

  // Verify they have distinct IDs
  EXPECT_NE(td1_id, td2_id);

  // Set display offsets
  constexpr Timestamp kOffset1 = 1000000000;  // 1 second
  constexpr Timestamp kOffset2 = 5000000000;  // 5 seconds
  engine.set_display_offset(td1_id, kOffset1);
  engine.set_display_offset(td2_id, kOffset2);

  // Verify offsets stored and retrievable
  const TimeDomain* td1 = engine.get_time_domain(td1_id);
  ASSERT_NE(td1, nullptr);
  EXPECT_EQ(td1->name, "ros_time");
  EXPECT_EQ(td1->display_offset, kOffset1);

  const TimeDomain* td2 = engine.get_time_domain(td2_id);
  ASSERT_NE(td2, nullptr);
  EXPECT_EQ(td2->name, "sim_time");
  EXPECT_EQ(td2->display_offset, kOffset2);

  // Create datasets bound to each time domain
  auto ds1_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "robot1", .time_domain_id = td1_id});
  ASSERT_TRUE(ds1_or.ok()) << ds1_or.status();
  DatasetId ds1_id = *ds1_or;

  auto ds2_or = engine.create_dataset(
      DatasetDescriptor{.source_name = "simulator", .time_domain_id = td2_id});
  ASSERT_TRUE(ds2_or.ok()) << ds2_or.status();
  DatasetId ds2_id = *ds2_or;

  // Verify datasets have the correct time domains
  const DatasetInfo* ds1 = engine.get_dataset(ds1_id);
  ASSERT_NE(ds1, nullptr);
  EXPECT_EQ(ds1->time_domain.id, td1_id);
  EXPECT_EQ(ds1->time_domain.name, "ros_time");
  EXPECT_EQ(ds1->time_domain.display_offset, kOffset1);

  const DatasetInfo* ds2 = engine.get_dataset(ds2_id);
  ASSERT_NE(ds2, nullptr);
  EXPECT_EQ(ds2->time_domain.id, td2_id);
  EXPECT_EQ(ds2->time_domain.name, "sim_time");
  EXPECT_EQ(ds2->time_domain.display_offset, kOffset2);

  // Verify display_time = raw_time - offset relationship
  constexpr Timestamp kRawTime = 10000000000;  // 10 seconds
  Timestamp display_time_1 = kRawTime - td1->display_offset;
  Timestamp display_time_2 = kRawTime - td2->display_offset;

  EXPECT_EQ(display_time_1, 9000000000);   // 10s - 1s = 9s
  EXPECT_EQ(display_time_2, 5000000000);   // 10s - 5s = 5s

  // Verify that updating offset is reflected immediately
  constexpr Timestamp kNewOffset1 = 2000000000;  // 2 seconds
  engine.set_display_offset(td1_id, kNewOffset1);

  const TimeDomain* td1_updated = engine.get_time_domain(td1_id);
  ASSERT_NE(td1_updated, nullptr);
  EXPECT_EQ(td1_updated->display_offset, kNewOffset1);

  Timestamp display_time_1_updated = kRawTime - td1_updated->display_offset;
  EXPECT_EQ(display_time_1_updated, 8000000000);  // 10s - 2s = 8s

  // Verify listing datasets shows both
  auto datasets = engine.list_datasets();
  EXPECT_EQ(datasets.size(), 2U);

  // Verify non-existent time domain returns nullptr
  EXPECT_EQ(engine.get_time_domain(999), nullptr);

  // Verify creating dataset with non-existent time domain fails
  auto bad_ds = engine.create_dataset(
      DatasetDescriptor{.source_name = "bad", .time_domain_id = 999});
  EXPECT_FALSE(bad_ds.ok());
}

}  // namespace
}  // namespace pj::engine
