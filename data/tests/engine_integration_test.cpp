#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "PJ/base/dataset.hpp"
#include "PJ/base/type_tree.hpp"
#include "PJ/base/types.hpp"
#include "PJ/engine/chunk.hpp"
#include "PJ/engine/column_buffer.hpp"
#include "PJ/engine/engine.hpp"
#include "PJ/engine/query.hpp"
#include "PJ/engine/reader.hpp"
#include "PJ/engine/topic_storage.hpp"
#include "PJ/engine/type_registry.hpp"
#include "PJ/engine/writer.hpp"

namespace PJ::engine {
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
  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test_source", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // Create writer, register scalar series (float64)
  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(dataset_id, "temperature", NumericType::kFloat64);
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();
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
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = handle.topic_id, .t_min = 0, .t_max = static_cast<Timestamp>(kRowCount - 1) * 1000});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  cursor_or->for_each([&count](const SampleRow& row) {
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
  auto latest_or = reader.latest_at(QueryPoint{.topic_id = handle.topic_id, .t = midpoint_ts});
  ASSERT_TRUE(latest_or.has_value()) << latest_or.error();
  auto& latest = *latest_or;
  ASSERT_TRUE(latest.has_value());
  EXPECT_EQ(latest->timestamp, midpoint_ts);
  ASSERT_NE(latest->chunk, nullptr);
  double midpoint_value = latest->chunk->read_numeric_as_double(0, latest->row_index);
  EXPECT_DOUBLE_EQ(midpoint_value, 1250.0);

  // Also verify metadata
  auto metadata_opt = reader.get_metadata(handle.topic_id);
  ASSERT_TRUE(metadata_opt.has_value());
  EXPECT_EQ(metadata_opt->total_row_count, kRowCount);
  EXPECT_EQ(metadata_opt->time_range_min, 0);
  EXPECT_EQ(metadata_opt->time_range_max, static_cast<Timestamp>(kRowCount - 1) * 1000);
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
  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "robot", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // Register schema and create topic via writer
  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("robot_pose", robot_pose);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "pose";
  topic_desc.schema_id = schema_id;
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.has_value()) << topic_id_or.error();
  TopicId topic_id = *topic_id_or;

  // Bind to get field IDs (column indices)
  auto write_handle_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(write_handle_or.has_value()) << write_handle_or.error();
  TopicWriteHandle write_handle = *write_handle_or;
  ASSERT_EQ(write_handle.field_ids.size(), 4U);

  // Column layout after flatten: col 0=x, col 1=y, col 2=z, col 3=frame_name
  constexpr std::size_t kRows = 200;
  for (std::size_t i = 0; i < kRows; ++i) {
    Timestamp ts = static_cast<Timestamp>(i) * 1000000;  // 1ms apart
    ASSERT_TRUE(writer.begin_row(topic_id, ts).has_value());
    writer.set_float32(topic_id, 0, static_cast<float>(i) * 1.0F);
    writer.set_float32(topic_id, 1, static_cast<float>(i) * 2.0F);
    writer.set_float32(topic_id, 2, static_cast<float>(i) * 3.0F);
    // Alternate frame name for variety
    std::string_view frame_name = (i % 2 == 0) ? "base_link" : "odom";
    writer.set_string(topic_id, 3, frame_name);
    ASSERT_TRUE(writer.finish_row(topic_id).has_value());
  }

  // Flush + commit
  auto flushed = writer.flush_all();
  EXPECT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  // Read back via reader
  DataReader reader = engine.create_reader();

  // Range query: verify total count
  std::size_t count = 0;
  auto cursor_or = reader.range_query(
      QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = static_cast<Timestamp>(kRows - 1) * 1000000});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  // Collect first and last rows for verification
  SampleRow first_row{};
  SampleRow last_row{};
  cursor_or->for_each([&count, &first_row, &last_row](const SampleRow& row) {
    if (count == 0) {
      first_row = row;
    }
    last_row = row;
    ++count;
  });
  EXPECT_EQ(count, kRows);

  // Verify first row (i=0): x=0, y=0, z=0, frame_name="base_link"
  ASSERT_NE(first_row.chunk, nullptr);
  EXPECT_FLOAT_EQ(static_cast<float>(first_row.chunk->read_numeric_as_double(0, first_row.row_index)), 0.0F);
  EXPECT_FLOAT_EQ(static_cast<float>(first_row.chunk->read_numeric_as_double(1, first_row.row_index)), 0.0F);
  EXPECT_FLOAT_EQ(static_cast<float>(first_row.chunk->read_numeric_as_double(2, first_row.row_index)), 0.0F);
  EXPECT_EQ(first_row.chunk->read_string(3, first_row.row_index), "base_link");

  // Verify last row (i=199): x=199, y=398, z=597, frame_name="odom"
  ASSERT_NE(last_row.chunk, nullptr);
  EXPECT_FLOAT_EQ(static_cast<float>(last_row.chunk->read_numeric_as_double(0, last_row.row_index)), 199.0F);
  EXPECT_FLOAT_EQ(static_cast<float>(last_row.chunk->read_numeric_as_double(1, last_row.row_index)), 398.0F);
  EXPECT_FLOAT_EQ(static_cast<float>(last_row.chunk->read_numeric_as_double(2, last_row.row_index)), 597.0F);
  EXPECT_EQ(last_row.chunk->read_string(3, last_row.row_index), "odom");

  // Verify dictionary encoding on the string column (col 3) in sealed chunks
  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  ASSERT_NE(storage, nullptr);
  for (const auto& chunk : storage->sealed_chunks()) {
    ASSERT_GT(chunk.column_encodings.size(), 3U);
    EXPECT_EQ(chunk.column_encodings[3], EncodingType::kDictionary) << "String column should use dictionary encoding";
    const auto& dict = std::get<encoding::DictionaryEncoded>(chunk.encoding_data[3]);
    // At most 2 unique values: "base_link" and "odom"
    EXPECT_LE(dict.dictionary.size(), 2U);
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

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "retention_test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();

  // Use small max_chunk_rows to force many chunks for fine-grained eviction
  // We use scalar API but with a custom topic for control over chunk size
  auto x_tree = make_primitive("value", PrimitiveType::kFloat64);
  auto schema_id_or = writer.register_schema("scalar_retention", x_tree);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "sensor";
  topic_desc.schema_id = schema_id;
  topic_desc.max_chunk_rows = 100;  // small chunks for easier eviction testing
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.has_value()) << topic_id_or.error();
  TopicId topic_id = *topic_id_or;

  auto write_handle_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(write_handle_or.has_value()) << write_handle_or.error();

  // Write 3000 rows: timestamps 0 to 9999 (spacing ~3.333)
  // Simplify: timestamps from 0 to 2999, one per integer timestamp
  constexpr std::size_t kRowCount = 3000;
  for (std::size_t i = 0; i < kRowCount; ++i) {
    Timestamp ts = static_cast<Timestamp>(i);
    ASSERT_TRUE(writer.begin_row(topic_id, ts).has_value());
    writer.set_float64(topic_id, 0, static_cast<double>(i) * 0.1);
    ASSERT_TRUE(writer.finish_row(topic_id).has_value());
  }

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  // Verify all data present
  DataReader reader = engine.create_reader();
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 2999});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow&) { ++count; });
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
  EXPECT_LT(chunks_after, chunks_before) << "Some chunks should have been evicted";

  // Query old range [0, 999]: should return fewer or zero rows
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 999});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow&) { ++count; });
    // Old chunks (t_max < 1499) are fully evicted.
    // Chunks with rows in [0, 999] and t_max < 1499 are gone.
    // At chunk size 100: chunks [0..99], [100..199], ..., [900..999] all have
    // t_max < 1499, so they should be evicted.
    EXPECT_EQ(count, 0U) << "Old data should be fully evicted";
  }

  // Query recent range [1500, 2999]: should return all data in that range
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 1500, .t_max = 2999});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow&) { ++count; });
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

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "evolution", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // Schema v1: struct { float32 x, y, z }
  auto x = make_primitive("x", PrimitiveType::kFloat32);
  auto y = make_primitive("y", PrimitiveType::kFloat32);
  auto z = make_primitive("z", PrimitiveType::kFloat32);
  auto schema_v1 = make_struct("pose", {x, y, z});

  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("pose", schema_v1);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();
  SchemaId schema_id = *schema_id_or;

  TopicDescriptor topic_desc;
  topic_desc.name = "position";
  topic_desc.schema_id = schema_id;
  topic_desc.max_chunk_rows = 200;  // Ensure v1 data fits in one chunk
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.has_value()) << topic_id_or.error();
  TopicId topic_id = *topic_id_or;

  auto wh_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(wh_or.has_value()) << wh_or.error();
  EXPECT_EQ(wh_or->field_ids.size(), 3U);

  // Write 100 rows with v1 schema (3 columns)
  for (std::size_t i = 0; i < 100; ++i) {
    Timestamp ts = static_cast<Timestamp>(i) * 1000;
    ASSERT_TRUE(writer.begin_row(topic_id, ts).has_value());
    writer.set_float32(topic_id, 0, static_cast<float>(i) * 1.0F);
    writer.set_float32(topic_id, 1, static_cast<float>(i) * 2.0F);
    writer.set_float32(topic_id, 2, static_cast<float>(i) * 3.0F);
    ASSERT_TRUE(writer.finish_row(topic_id).has_value());
  }

  // Flush v1 data and commit
  auto flushed_v1 = writer.flush_all();
  EXPECT_FALSE(flushed_v1.empty());
  engine.commit_chunks(std::move(flushed_v1));

  // Evolve schema: add float32 w
  auto w = make_primitive("w", PrimitiveType::kFloat32);
  auto schema_v2 = make_struct("pose", {x, y, z, w});
  auto evolve_status = engine.type_registry().evolve_schema(schema_id, schema_v2);
  ASSERT_TRUE(evolve_status.has_value()) << evolve_status.error();

  // Create a new writer so it picks up the evolved schema's column layout
  DataWriter writer2 = engine.create_writer();
  auto wh2_or = writer2.bind_topic_writer(topic_id);
  ASSERT_TRUE(wh2_or.has_value()) << wh2_or.error();
  EXPECT_EQ(wh2_or->field_ids.size(), 4U);

  // Write 100 more rows with v2 schema (4 columns)
  for (std::size_t i = 0; i < 100; ++i) {
    Timestamp ts = static_cast<Timestamp>(100 + i) * 1000;
    ASSERT_TRUE(writer2.begin_row(topic_id, ts).has_value());
    writer2.set_float32(topic_id, 0, static_cast<float>(100 + i) * 1.0F);
    writer2.set_float32(topic_id, 1, static_cast<float>(100 + i) * 2.0F);
    writer2.set_float32(topic_id, 2, static_cast<float>(100 + i) * 3.0F);
    writer2.set_float32(topic_id, 3, static_cast<float>(100 + i) * 4.0F);
    ASSERT_TRUE(writer2.finish_row(topic_id).has_value());
  }

  auto flushed_v2 = writer2.flush_all();
  EXPECT_FALSE(flushed_v2.empty());
  engine.commit_chunks(std::move(flushed_v2));

  // Read back data spanning both versions
  DataReader reader = engine.create_reader();

  // Query old rows [0, 99000]: should return 100 rows with 3 columns
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 99000});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow& row) {
      ASSERT_NE(row.chunk, nullptr);
      // Old chunks should have 3 column descriptors
      EXPECT_EQ(row.chunk->column_descriptors.size(), 3U);
      if (count == 0) {
        // Verify first old row: x=0, y=0, z=0
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(0, row.row_index)), 0.0F);
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(1, row.row_index)), 0.0F);
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(2, row.row_index)), 0.0F);
      }
      ++count;
    });
    EXPECT_EQ(count, 100U);
  }

  // Query new rows [100000, 199000]: should return 100 rows with 4 columns
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 100000, .t_max = 199000});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow& row) {
      ASSERT_NE(row.chunk, nullptr);
      // New chunks should have 4 column descriptors
      EXPECT_EQ(row.chunk->column_descriptors.size(), 4U);
      if (count == 0) {
        // Verify first new row (i=100): x=100, y=200, z=300, w=400
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(0, row.row_index)), 100.0F);
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(1, row.row_index)), 200.0F);
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(2, row.row_index)), 300.0F);
        EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(3, row.row_index)), 400.0F);
      }
      ++count;
    });
    EXPECT_EQ(count, 100U);
  }

  // Verify full range returns all 200 rows
  {
    std::size_t count = 0;
    auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 199000});
    ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
    cursor_or->for_each([&count](const SampleRow&) { ++count; });
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
  ASSERT_TRUE(td1_or.has_value()) << td1_or.error();
  TimeDomainId td1_id = *td1_or;

  auto td2_or = engine.create_time_domain("sim_time");
  ASSERT_TRUE(td2_or.has_value()) << td2_or.error();
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
  auto ds1_or = engine.create_dataset(DatasetDescriptor{.source_name = "robot1", .time_domain_id = td1_id});
  ASSERT_TRUE(ds1_or.has_value()) << ds1_or.error();
  DatasetId ds1_id = *ds1_or;

  auto ds2_or = engine.create_dataset(DatasetDescriptor{.source_name = "simulator", .time_domain_id = td2_id});
  ASSERT_TRUE(ds2_or.has_value()) << ds2_or.error();
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

  EXPECT_EQ(display_time_1, 9000000000);  // 10s - 1s = 9s
  EXPECT_EQ(display_time_2, 5000000000);  // 10s - 5s = 5s

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
  auto bad_ds = engine.create_dataset(DatasetDescriptor{.source_name = "bad", .time_domain_id = 999});
  EXPECT_FALSE(bad_ds.has_value());
}

// ===========================================================================
// Test 6: create_topic rejects non-existent schema_id
//
// Before fix: create_topic accepted any schema_id, leading to empty columns
// and UB when setting values on the non-existent columns.
// ===========================================================================

TEST(EngineIntegrationTest, CreateTopicRejectsInvalidSchemaId) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // schema_id=999 doesn't exist — should fail
  TopicDescriptor desc;
  desc.name = "bad_topic";
  desc.schema_id = 999;
  auto result = engine.create_topic(dataset_id, desc);
  EXPECT_FALSE(result.has_value());

  // schema_id=0 (inline columns) should still succeed
  TopicDescriptor scalar_desc;
  scalar_desc.name = "scalar_topic";
  scalar_desc.schema_id = 0;
  auto scalar_result = engine.create_topic(dataset_id, scalar_desc);
  EXPECT_TRUE(scalar_result.has_value()) << scalar_result.error();
}

// ===========================================================================
// Test 7: begin_row rejects non-existent topic_id
//
// Before fix: begin_row called get_or_create_builder which hit an assert
// (UB in Release builds) when the topic didn't exist.
// ===========================================================================

TEST(EngineIntegrationTest, BeginRowRejectsInvalidTopicId) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();

  DataWriter writer = engine.create_writer();
  auto status = writer.begin_row(/*topic_id=*/999, /*t=*/1000);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// Test 8: Partial row auto-fills missing columns with null
//
// Before fix: finish_row incremented row_count but left column buffers
// with divergent lengths, causing later reads to go out-of-bounds.
// ===========================================================================

TEST(EngineIntegrationTest, PartialRowAutoFillsNulls) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // Create a 3-column schema: float32 x, y, z
  auto x = make_primitive("x", PrimitiveType::kFloat32);
  auto y = make_primitive("y", PrimitiveType::kFloat32);
  auto z = make_primitive("z", PrimitiveType::kFloat32);
  auto schema_tree = make_struct("point", {x, y, z});

  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("point", schema_tree);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();

  TopicDescriptor topic_desc;
  topic_desc.name = "partial";
  topic_desc.schema_id = *schema_id_or;
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.has_value()) << topic_id_or.error();
  TopicId topic_id = *topic_id_or;

  auto wh_or = writer.bind_topic_writer(topic_id);
  ASSERT_TRUE(wh_or.has_value()) << wh_or.error();

  // Row 1: set only x (columns y and z should be auto-null-filled)
  ASSERT_TRUE(writer.begin_row(topic_id, 1000).has_value());
  writer.set_float32(topic_id, 0, 1.0F);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  // Row 2: set all 3 columns (no nulls)
  ASSERT_TRUE(writer.begin_row(topic_id, 2000).has_value());
  writer.set_float32(topic_id, 0, 2.0F);
  writer.set_float32(topic_id, 1, 3.0F);
  writer.set_float32(topic_id, 2, 4.0F);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  auto flushed = writer.flush_all();
  ASSERT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  // Read back and verify
  DataReader reader = engine.create_reader();
  auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 3000});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();

  std::size_t count = 0;
  cursor_or->for_each([&count](const SampleRow& row) {
    ASSERT_NE(row.chunk, nullptr);
    if (count == 0) {
      // Row 1: x=1.0, y=null, z=null
      EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(0, row.row_index)), 1.0F);
      EXPECT_TRUE(row.chunk->is_null(1, row.row_index));
      EXPECT_TRUE(row.chunk->is_null(2, row.row_index));
    } else if (count == 1) {
      // Row 2: x=2.0, y=3.0, z=4.0 (no nulls)
      EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(0, row.row_index)), 2.0F);
      EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(1, row.row_index)), 3.0F);
      EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->read_numeric_as_double(2, row.row_index)), 4.0F);
      EXPECT_FALSE(row.chunk->is_null(0, row.row_index));
      EXPECT_FALSE(row.chunk->is_null(1, row.row_index));
      EXPECT_FALSE(row.chunk->is_null(2, row.row_index));
    }
    ++count;
  });
  EXPECT_EQ(count, 2U);
}

// ===========================================================================
// Test 9: Retention works with negative timestamps
//
// Before fix: enforce_retention checked `t_max > 0` to skip empty topics,
// which also skipped topics with legitimate non-positive timestamps.
// ===========================================================================

TEST(EngineIntegrationTest, RetentionWorksWithNegativeTimestamps) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(dataset_id, "negative_ts", NumericType::kFloat64);
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();
  ScalarSeriesHandle handle = *handle_or;

  // Write data with negative timestamps: -1000, -900, ..., -100, 0
  // Use small chunk size topic instead of scalar API to control chunk size
  // Actually, scalar API uses default chunk size 1024, which means all 11
  // values fit in one chunk. That's fine for testing retention logic.
  for (int i = -1000; i <= 0; i += 100) {
    writer.append_scalar(handle, static_cast<Timestamp>(i), static_cast<double>(i));
  }

  auto flushed = writer.flush_all();
  ASSERT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  const TopicStorage* storage = engine.get_topic_storage(handle.topic_id);
  ASSERT_NE(storage, nullptr);
  EXPECT_FALSE(storage->empty());
  EXPECT_EQ(storage->time_max(), 0);

  // Enforce retention with window of 500: evict_before(0 - 500 = -500)
  // Chunks with t_max < -500 should be evicted.
  // Our single chunk spans [-1000, 0], so t_max=0 > -500 → not evicted.
  engine.enforce_retention(500);

  // Data should still be present (the chunk wasn't evicted)
  EXPECT_FALSE(storage->empty());

  DataReader reader = engine.create_reader();
  auto cursor_or = reader.range_query(QueryRange{.topic_id = handle.topic_id, .t_min = -1000, .t_max = 0});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  std::size_t count = 0;
  cursor_or->for_each([&count](const SampleRow&) { ++count; });
  EXPECT_EQ(count, 11U);
}

// ===========================================================================
// Test 10: range_query / latest_at return NotFound for non-existent topics
//
// Before fix: these returned empty results indistinguishable from
// "topic exists but has no data."
// ===========================================================================

TEST(EngineIntegrationTest, QueryReturnsErrorForMissingTopic) {
  DataEngine engine;

  DataReader reader = engine.create_reader();

  // range_query with non-existent topic
  auto cursor_or = reader.range_query(QueryRange{.topic_id = 999, .t_min = 0, .t_max = 1000});
  EXPECT_FALSE(cursor_or.has_value());

  // latest_at with non-existent topic
  auto latest_or = reader.latest_at(QueryPoint{.topic_id = 999, .t = 500});
  EXPECT_FALSE(latest_or.has_value());
}

// ===========================================================================
// Test 11: begin_row rejects out-of-order timestamp
// ===========================================================================

TEST(EngineIntegrationTest, BeginRowRejectsOutOfOrderTimestamp) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(dataset_id, "ordered", NumericType::kFloat64);
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();
  TopicId topic_id = handle_or->topic_id;

  // First row at t=200 succeeds
  ASSERT_TRUE(writer.begin_row(topic_id, 200).has_value());
  writer.set_float64(topic_id, 0, 1.0);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  // Second row at t=100 (out of order) should fail
  auto status = writer.begin_row(topic_id, 100);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// Test 12: Equal timestamps are allowed (non-decreasing)
// ===========================================================================

TEST(EngineIntegrationTest, EqualTimestampsAllowed) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(dataset_id, "equal_ts", NumericType::kFloat64);
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();
  TopicId topic_id = handle_or->topic_id;

  // Two rows at t=100 should both succeed
  ASSERT_TRUE(writer.begin_row(topic_id, 100).has_value());
  writer.set_float64(topic_id, 0, 1.0);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  ASSERT_TRUE(writer.begin_row(topic_id, 100).has_value());
  writer.set_float64(topic_id, 0, 2.0);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  // Third row at higher timestamp also succeeds
  ASSERT_TRUE(writer.begin_row(topic_id, 200).has_value());
  writer.set_float64(topic_id, 0, 3.0);
  ASSERT_TRUE(writer.finish_row(topic_id).has_value());

  auto flushed = writer.flush_all();
  engine.commit_chunks(std::move(flushed));

  DataReader reader = engine.create_reader();
  std::size_t count = 0;
  auto cursor_or = reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = 300});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  cursor_or->for_each([&count](const SampleRow&) { ++count; });
  EXPECT_EQ(count, 3U);
}

// ===========================================================================
// Test 13: Bulk append_columns spanning multiple chunks
// ===========================================================================

TEST(EngineIntegrationTest, BulkAppendColumnsMultiChunk) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "bulk_test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value()) << dataset_id_or.error();
  DatasetId dataset_id = *dataset_id_or;

  // Create a 3-column schema: float32 x, y, z
  auto x = make_primitive("x", PrimitiveType::kFloat32);
  auto y = make_primitive("y", PrimitiveType::kFloat32);
  auto z = make_primitive("z", PrimitiveType::kFloat32);
  auto schema_tree = make_struct("imu", {x, y, z});

  DataWriter writer = engine.create_writer();
  auto schema_id_or = writer.register_schema("imu_schema", schema_tree);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();

  TopicDescriptor topic_desc;
  topic_desc.name = "imu_data";
  topic_desc.schema_id = *schema_id_or;
  topic_desc.max_chunk_rows = 256;  // small chunks to force splitting
  auto topic_id_or = writer.register_topic(dataset_id, topic_desc);
  ASSERT_TRUE(topic_id_or.has_value()) << topic_id_or.error();
  TopicId topic_id = *topic_id_or;

  // Prepare 1000 rows of bulk data
  constexpr std::size_t N = 1000;
  std::vector<Timestamp> timestamps(N);
  std::vector<float> x_vals(N), y_vals(N), z_vals(N);
  for (std::size_t i = 0; i < N; ++i) {
    timestamps[i] = static_cast<Timestamp>(i) * 1000;
    x_vals[i] = static_cast<float>(i) * 0.1F;
    y_vals[i] = static_cast<float>(i) * 0.2F;
    z_vals[i] = static_cast<float>(i) * 0.3F;
  }

  std::vector<ColumnData> columns = {
      ColumnData::Float32(0, x_vals),
      ColumnData::Float32(1, y_vals),
      ColumnData::Float32(2, z_vals),
  };
  auto status = writer.append_columns(topic_id, timestamps, columns);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flush_all();
  EXPECT_FALSE(flushed.empty());
  engine.commit_chunks(std::move(flushed));

  // Should have multiple chunks (1000 / 256 = 4 chunks)
  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  ASSERT_NE(storage, nullptr);
  EXPECT_GE(storage->sealed_chunks().size(), 3U);

  // Verify round-trip via range_query
  DataReader reader = engine.create_reader();
  std::size_t count = 0;
  auto cursor_or =
      reader.range_query(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = static_cast<Timestamp>(N - 1) * 1000});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  cursor_or->for_each([&count](const SampleRow& row) {
    ASSERT_NE(row.chunk, nullptr);
    ++count;
  });
  EXPECT_EQ(count, N);

  // Spot-check specific values via latest_at
  auto latest_or = reader.latest_at(QueryPoint{.topic_id = topic_id, .t = 500 * 1000});
  ASSERT_TRUE(latest_or.has_value()) << latest_or.error();
  ASSERT_TRUE(latest_or->has_value());
  EXPECT_EQ((*latest_or)->timestamp, 500 * 1000);
  EXPECT_FLOAT_EQ(
      static_cast<float>((*latest_or)->chunk->read_numeric_as_double(0, (*latest_or)->row_index)), 500.0F * 0.1F);
  EXPECT_FLOAT_EQ(
      static_cast<float>((*latest_or)->chunk->read_numeric_as_double(1, (*latest_or)->row_index)), 500.0F * 0.2F);
}

// ===========================================================================
// Test 14: Bulk append error handling
// ===========================================================================

TEST(EngineIntegrationTest, BulkAppendErrorHandling) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "err_test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value());
  DatasetId dataset_id = *dataset_id_or;

  DataWriter writer = engine.create_writer();

  // Non-existent topic
  {
    const Timestamp ts[] = {1};
    const float vals[] = {1.0F};
    std::vector<ColumnData> cols = {
        ColumnData::Float32(0, Span<const float>(vals, 1)),
    };
    auto status = writer.append_columns(999, Span<const Timestamp>(ts, 1), cols);
    EXPECT_FALSE(status.has_value());
  }

  // Mismatched column count
  {
    auto tree = make_primitive("val", PrimitiveType::kFloat32);
    auto sid = *writer.register_schema("s1", tree);
    TopicDescriptor desc;
    desc.name = "t1";
    desc.schema_id = sid;
    auto tid = *writer.register_topic(dataset_id, desc);

    const Timestamp ts[] = {1, 2, 3};
    const float vals[] = {1.0F, 2.0F};  // 2 values, 3 timestamps
    std::vector<ColumnData> cols = {
        ColumnData::Float32(0, Span<const float>(vals, 2)),
    };
    auto status = writer.append_columns(tid, Span<const Timestamp>(ts, 3), cols);
    EXPECT_FALSE(status.has_value());
  }
}

// ===========================================================================
// Test 15: Bulk append empty is a no-op
// ===========================================================================

TEST(EngineIntegrationTest, BulkAppendEmpty) {
  DataEngine engine;

  auto dataset_id_or = engine.create_dataset(DatasetDescriptor{.source_name = "empty_test", .time_domain_id = 0});
  ASSERT_TRUE(dataset_id_or.has_value());
  DatasetId dataset_id = *dataset_id_or;

  auto tree = make_primitive("val", PrimitiveType::kFloat64);
  DataWriter writer = engine.create_writer();
  auto sid = *writer.register_schema("s_empty", tree);
  TopicDescriptor desc;
  desc.name = "empty_topic";
  desc.schema_id = sid;
  auto tid = *writer.register_topic(dataset_id, desc);

  // Empty append should succeed and produce no data
  auto status = writer.append_columns(tid, Span<const Timestamp>(), Span<const ColumnData>());
  EXPECT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flush_all();
  EXPECT_TRUE(flushed.empty());
}

}  // namespace
}  // namespace PJ::engine
