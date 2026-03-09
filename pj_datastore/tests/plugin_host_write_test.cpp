#include "pj_datastore/plugin_host_write.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow.hpp"
#include "nanoarrow/nanoarrow_ipc.h"
#include "pj_base/dataset.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_host_read.hpp"
#include "pj_datastore/plugin_host_types.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/reader.hpp"
#include "pj_datastore/topic_storage.hpp"

namespace PJ {
namespace {

// ===========================================================================
// Helper: serialize Arrow schema + array to IPC bytes
// ===========================================================================

std::vector<uint8_t> serializeToIpc(ArrowSchema* schema, ArrowArray* array) {
  ArrowBuffer out_buf;
  ArrowBufferInit(&out_buf);

  ArrowIpcOutputStream out_stream;
  EXPECT_EQ(ArrowIpcOutputStreamInitBuffer(&out_stream, &out_buf), NANOARROW_OK);

  ArrowIpcWriter writer;
  EXPECT_EQ(ArrowIpcWriterInit(&writer, &out_stream), NANOARROW_OK);

  ArrowError error;
  EXPECT_EQ(ArrowIpcWriterWriteSchema(&writer, schema, &error), NANOARROW_OK) << error.message;

  nanoarrow::UniqueArrayView view;
  EXPECT_EQ(ArrowArrayViewInitFromSchema(view.get(), schema, nullptr), NANOARROW_OK);
  EXPECT_EQ(ArrowArrayViewSetArray(view.get(), array, nullptr), NANOARROW_OK);
  EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, view.get(), &error), NANOARROW_OK) << error.message;
  EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, nullptr, &error), NANOARROW_OK);

  ArrowIpcWriterReset(&writer);

  std::vector<uint8_t> result(static_cast<std::size_t>(out_buf.size_bytes));
  std::memcpy(result.data(), out_buf.data, result.size());
  ArrowBufferReset(&out_buf);
  return result;
}

// ===========================================================================
// 1. createDataSource — basic
// ===========================================================================

TEST(PluginHostWriteTest, CreateDataSourceBasic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto handle_or = host.createDataSource("sensor_hub");
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();

  const auto* ds = engine.getDataset(handle_or->id);
  ASSERT_NE(ds, nullptr);
  EXPECT_EQ(ds->source_name, "sensor_hub");
}

// ===========================================================================
// 2. createDataSource — two sources get distinct IDs
// ===========================================================================

TEST(PluginHostWriteTest, CreateDataSourceDistinctIds) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto a = host.createDataSource("a");
  auto b = host.createDataSource("b");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(a->id, b->id);
}

// ===========================================================================
// 3. ensureTopic — basic creation
// ===========================================================================

TEST(PluginHostWriteTest, EnsureTopicBasic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = host.ensureTopic(src, "gps");
  ASSERT_TRUE(topic.has_value()) << topic.error();
  EXPECT_NE(topic->id, 0U);

  const auto* storage = engine.getTopicStorage(topic->id);
  ASSERT_NE(storage, nullptr);
  EXPECT_EQ(storage->descriptor().name, "gps");
}

// ===========================================================================
// 4. ensureTopic — idempotent (same name returns same handle)
// ===========================================================================

TEST(PluginHostWriteTest, EnsureTopicIdempotent) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto t1 = *host.ensureTopic(src, "imu");
  auto t2 = *host.ensureTopic(src, "imu");
  EXPECT_EQ(t1.id, t2.id);
}

// ===========================================================================
// 5. ensureTopic — different names produce different handles
// ===========================================================================

TEST(PluginHostWriteTest, EnsureTopicDifferentNames) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto t1 = *host.ensureTopic(src, "gps");
  auto t2 = *host.ensureTopic(src, "imu");
  EXPECT_NE(t1.id, t2.id);
}

// ===========================================================================
// 6. ensureTopic — invalid data source handle
// ===========================================================================

TEST(PluginHostWriteTest, EnsureTopicInvalidSource) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto result = host.ensureTopic(DataSourceHandle{.id = 999}, "test");
  EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// 7. ensureField — basic creation for each FieldType
// ===========================================================================

TEST(PluginHostWriteTest, EnsureFieldAllTypes) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "data");

  auto f32 = host.ensureField(topic, "f32", FieldType::kFloat32);
  auto f64 = host.ensureField(topic, "f64", FieldType::kFloat64);
  auto i32 = host.ensureField(topic, "i32", FieldType::kInt32);
  auto i64 = host.ensureField(topic, "i64", FieldType::kInt64);
  auto u64 = host.ensureField(topic, "u64", FieldType::kUint64);
  auto bl = host.ensureField(topic, "bl", FieldType::kBool);
  auto str = host.ensureField(topic, "str", FieldType::kString);

  ASSERT_TRUE(f32.has_value()) << f32.error();
  ASSERT_TRUE(f64.has_value()) << f64.error();
  ASSERT_TRUE(i32.has_value()) << i32.error();
  ASSERT_TRUE(i64.has_value()) << i64.error();
  ASSERT_TRUE(u64.has_value()) << u64.error();
  ASSERT_TRUE(bl.has_value()) << bl.error();
  ASSERT_TRUE(str.has_value()) << str.error();

  // Field IDs should be sequential (0..6)
  EXPECT_EQ(f32->id, 0U);
  EXPECT_EQ(f64->id, 1U);
  EXPECT_EQ(i32->id, 2U);
  EXPECT_EQ(i64->id, 3U);
  EXPECT_EQ(u64->id, 4U);
  EXPECT_EQ(bl->id, 5U);
  EXPECT_EQ(str->id, 6U);

  // All handles carry the correct topic
  EXPECT_EQ(f32->topic, topic);
  EXPECT_EQ(str->topic, topic);
}

// ===========================================================================
// 8. ensureField — idempotent (same name+type returns same handle)
// ===========================================================================

TEST(PluginHostWriteTest, EnsureFieldIdempotent) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "data");

  auto f1 = *host.ensureField(topic, "val", FieldType::kFloat64);
  auto f2 = *host.ensureField(topic, "val", FieldType::kFloat64);
  EXPECT_EQ(f1.id, f2.id);
}

// ===========================================================================
// 9. ensureField — type mismatch error
// ===========================================================================

TEST(PluginHostWriteTest, EnsureFieldTypeMismatch) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "data");

  auto f1 = host.ensureField(topic, "val", FieldType::kFloat64);
  ASSERT_TRUE(f1.has_value());

  auto f2 = host.ensureField(topic, "val", FieldType::kInt32);
  EXPECT_FALSE(f2.has_value());
}

// ===========================================================================
// 10. ensureField — invalid topic handle
// ===========================================================================

TEST(PluginHostWriteTest, EnsureFieldInvalidTopic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto result = host.ensureField(TopicHandle{.id = 999}, "val", FieldType::kFloat64);
  EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// 11. appendRecord — single record with mixed types, roundtrip via read
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordMixedTypes) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "mixed");

  std::vector<NamedFieldValue> fields = {
      {.name = "x", .type = FieldType::kFloat32, .value = 1.5F},
      {.name = "y", .type = FieldType::kFloat64, .value = 2.5},
      {.name = "count", .type = FieldType::kInt32, .value = int32_t{42}},
      {.name = "big", .type = FieldType::kInt64, .value = int64_t{1000000}},
      {.name = "flags", .type = FieldType::kUint64, .value = uint64_t{0xFF}},
      {.name = "ok", .type = FieldType::kBool, .value = true},
      {.name = "label", .type = FieldType::kString, .value = std::string_view("hello")},
  };

  auto status = host.appendRecord(topic, 1000, fields);
  ASSERT_TRUE(status.has_value()) << status.error();

  host.flush();

  // Verify via DataReader
  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 2000});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();

  std::size_t count = 0;
  cursor_or->forEach([&](const SampleRow& row) {
    EXPECT_EQ(row.timestamp, 1000);
    EXPECT_FLOAT_EQ(static_cast<float>(row.chunk->readNumericAsDouble(0, row.row_index)), 1.5F);
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(1, row.row_index), 2.5);
    EXPECT_EQ(static_cast<int32_t>(row.chunk->readNumericAsDouble(2, row.row_index)), 42);
    EXPECT_EQ(static_cast<int64_t>(row.chunk->readNumericAsDouble(3, row.row_index)), 1000000);
    EXPECT_EQ(static_cast<uint64_t>(row.chunk->readNumericAsDouble(4, row.row_index)), 0xFF);
    EXPECT_TRUE(row.chunk->readBool(5, row.row_index));
    EXPECT_EQ(row.chunk->readString(6, row.row_index), "hello");
    ++count;
  });
  EXPECT_EQ(count, 1U);
}

// ===========================================================================
// 12. appendRecord — sparse record (only some fields)
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordSparse) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "sparse");

  // Create all 3 fields first
  host.ensureField(topic, "x", FieldType::kFloat64);
  host.ensureField(topic, "y", FieldType::kFloat64);
  host.ensureField(topic, "z", FieldType::kFloat64);

  // Only provide x — y and z should be auto-null
  std::vector<NamedFieldValue> fields = {
      {.name = "x", .type = FieldType::kFloat64, .value = 10.0},
  };
  ASSERT_TRUE(host.appendRecord(topic, 1000, fields).has_value());
  host.flush();

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 2000});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([](const SampleRow& row) {
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(0, row.row_index), 10.0);
    EXPECT_TRUE(row.chunk->isNull(1, row.row_index));
    EXPECT_TRUE(row.chunk->isNull(2, row.row_index));
  });
}

// ===========================================================================
// 13. appendRecord — auto-creates fields on first sight
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordAutoCreatesFields) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "auto");

  // No fields created yet — appendRecord should create them
  std::vector<NamedFieldValue> fields = {
      {.name = "temp", .type = FieldType::kFloat64, .value = 36.6},
      {.name = "unit", .type = FieldType::kString, .value = std::string_view("celsius")},
  };
  ASSERT_TRUE(host.appendRecord(topic, 100, fields).has_value());
  host.flush();

  // Verify fields were created by reading back
  const auto* storage = engine.getTopicStorage(topic.id);
  ASSERT_NE(storage, nullptr);
  EXPECT_EQ(storage->columnDescriptors().size(), 2U);
  EXPECT_EQ(storage->columnDescriptors()[0].field_path, "temp");
  EXPECT_EQ(storage->columnDescriptors()[1].field_path, "unit");
}

// ===========================================================================
// 14. appendRecord — duplicate field names within one call
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordDuplicateFieldNames) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "dup");

  std::vector<NamedFieldValue> fields = {
      {.name = "x", .type = FieldType::kFloat64, .value = 1.0},
      {.name = "x", .type = FieldType::kFloat64, .value = 2.0},
  };
  auto status = host.appendRecord(topic, 100, fields);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 15. appendRecord — null values
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordNullValues) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "nulls");

  std::vector<NamedFieldValue> fields = {
      {.name = "val", .type = FieldType::kFloat64, .is_null = true, .value = 0.0},
  };
  ASSERT_TRUE(host.appendRecord(topic, 100, fields).has_value());
  host.flush();

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 200});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([](const SampleRow& row) { EXPECT_TRUE(row.chunk->isNull(0, row.row_index)); });
}

// ===========================================================================
// 16. appendRecord — invalid topic handle
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordInvalidTopic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  std::vector<NamedFieldValue> fields = {
      {.name = "x", .type = FieldType::kFloat64, .value = 1.0},
  };
  auto status = host.appendRecord(TopicHandle{.id = 999}, 100, fields);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 17. appendRecord — empty fields list is valid (writes a timestamp-only row)
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordEmptyFields) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "empty");

  auto status = host.appendRecord(topic, 100, Span<const NamedFieldValue>());
  ASSERT_TRUE(status.has_value()) << status.error();
  host.flush();

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 200});
  ASSERT_TRUE(cursor_or.has_value());
  std::size_t count = 0;
  cursor_or->forEach([&](const SampleRow& row) {
    EXPECT_EQ(row.timestamp, 100);
    ++count;
  });
  EXPECT_EQ(count, 1U);
}

// ===========================================================================
// 18. appendRecordFast — basic use with pre-resolved handles
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastBasic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "fast");
  auto fx = *host.ensureField(topic, "x", FieldType::kFloat64);
  auto fy = *host.ensureField(topic, "y", FieldType::kFloat64);

  std::vector<BoundFieldValue> fields = {
      {.field = fx, .value = 1.0},
      {.field = fy, .value = 2.0},
  };
  auto status = host.appendRecordFast(topic, 1000, fields);
  ASSERT_TRUE(status.has_value()) << status.error();
  host.flush();

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 2000});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([](const SampleRow& row) {
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(0, row.row_index), 1.0);
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(1, row.row_index), 2.0);
  });
}

// ===========================================================================
// 19. appendRecordFast — wrong topic handle for field
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastWrongTopic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto t1 = *host.ensureTopic(src, "t1");
  auto t2 = *host.ensureTopic(src, "t2");
  auto f1 = *host.ensureField(t1, "x", FieldType::kFloat64);

  // Try to use field from t1 when writing to t2
  std::vector<BoundFieldValue> fields = {
      {.field = f1, .value = 1.0},
  };
  auto status = host.appendRecordFast(t2, 1000, fields);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 20. appendRecordFast — duplicate field handles in one call
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastDuplicateHandles) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "dup");
  auto fx = *host.ensureField(topic, "x", FieldType::kFloat64);

  std::vector<BoundFieldValue> fields = {
      {.field = fx, .value = 1.0},
      {.field = fx, .value = 2.0},
  };
  auto status = host.appendRecordFast(topic, 1000, fields);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 21. appendRecordFast — value type mismatch
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastValueTypeMismatch) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "mismatch");
  auto fx = *host.ensureField(topic, "x", FieldType::kFloat64);

  // Pass int32_t value for a float64 field
  std::vector<BoundFieldValue> fields = {
      {.field = fx, .value = int32_t{42}},
  };
  auto status = host.appendRecordFast(topic, 1000, fields);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 22. Multi-record write + flush + read roundtrip
// ===========================================================================

TEST(PluginHostWriteTest, MultiRecordRoundtrip) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "series");

  constexpr std::size_t kRows = 500;
  for (std::size_t i = 0; i < kRows; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "val", .type = FieldType::kFloat64, .value = static_cast<double>(i) * 0.1},
    };
    auto status = host.appendRecord(topic, static_cast<Timestamp>(i) * 1000, fields);
    ASSERT_TRUE(status.has_value()) << status.error();
  }
  host.flush();

  DataReader reader = engine.createReader();
  std::size_t count = 0;
  auto cursor_or = reader.rangeQuery(
      QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = static_cast<Timestamp>(kRows - 1) * 1000});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([&](const SampleRow& row) {
    double expected = static_cast<double>(count) * 0.1;
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(0, row.row_index), expected);
    ++count;
  });
  EXPECT_EQ(count, kRows);
}

// ===========================================================================
// 23. Large batch spanning multiple chunks
// ===========================================================================

TEST(PluginHostWriteTest, LargeBatchMultipleChunks) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "big");

  // Default chunk size is 1024; write 3000+ records
  constexpr std::size_t kRows = 3000;
  for (std::size_t i = 0; i < kRows; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "x", .type = FieldType::kFloat64, .value = static_cast<double>(i)},
    };
    ASSERT_TRUE(host.appendRecord(topic, static_cast<Timestamp>(i), fields).has_value());
  }
  host.flush();

  const auto* storage = engine.getTopicStorage(topic.id);
  ASSERT_NE(storage, nullptr);
  EXPECT_GE(storage->sealedChunks().size(), 2U);

  // Verify total row count
  auto metadata_opt = engine.createReader().getMetadata(topic.id);
  ASSERT_TRUE(metadata_opt.has_value());
  EXPECT_EQ(metadata_opt->total_row_count, kRows);
}

// ===========================================================================
// 24. appendArrowIpc — basic import with timestamp column
// ===========================================================================

TEST(PluginHostWriteTest, AppendArrowIpcBasic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "arrow");

  // Build Arrow IPC: struct { int64 "_timestamp", float64 "value" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 2), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT64), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "_timestamp"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[1]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_DOUBLE), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[1], "value"), NANOARROW_OK);

  constexpr int64_t N = 50;
  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  for (int64_t i = 0; i < N; ++i) {
    ASSERT_EQ(ArrowArrayAppendInt(array->children[0], i * 1000), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayAppendDouble(array->children[1], static_cast<double>(i) * 0.5), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  }
  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serializeToIpc(schema.get(), array.get());

  auto status = host.appendArrowIpc(topic, Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  ASSERT_TRUE(status.has_value()) << status.error();
  host.flush();

  // Verify roundtrip
  DataReader reader = engine.createReader();
  auto latest_or = reader.latestAt(QueryPoint{.topic_id = topic.id, .t = 25000});
  ASSERT_TRUE(latest_or.has_value());
  ASSERT_TRUE(latest_or->has_value());
  EXPECT_EQ((*latest_or)->timestamp, 25000);
  EXPECT_DOUBLE_EQ((*latest_or)->chunk->readNumericAsDouble(0, (*latest_or)->row_index), 25.0 * 0.5);
}

// ===========================================================================
// 25. appendArrowIpc — custom timestamp column name
// ===========================================================================

TEST(PluginHostWriteTest, AppendArrowIpcCustomTimestampColumn) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "custom_ts");

  // Build: struct { int64 "my_time", float32 "temp" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 2), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT64), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "my_time"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[1]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[1], "temp"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  for (int64_t i = 0; i < 10; ++i) {
    ASSERT_EQ(ArrowArrayAppendInt(array->children[0], i * 100), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayAppendDouble(array->children[1], static_cast<double>(i) * 1.5), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  }
  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serializeToIpc(schema.get(), array.get());

  auto status = host.appendArrowIpc(topic, Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), "my_time");
  ASSERT_TRUE(status.has_value()) << status.error();
  host.flush();

  DataReader reader = engine.createReader();
  std::size_t count = 0;
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 1000});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([&](const SampleRow&) { ++count; });
  EXPECT_EQ(count, 10U);
}

// ===========================================================================
// 26. appendArrowIpc — missing timestamp column
// ===========================================================================

TEST(PluginHostWriteTest, AppendArrowIpcMissingTimestampColumn) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "no_ts");

  // Build: struct { float64 "value" } — no timestamp column
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 1), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_DOUBLE), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "value"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayAppendDouble(array->children[0], 1.0), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serializeToIpc(schema.get(), array.get());

  // Default timestamp column is "_timestamp" which doesn't exist
  auto status = host.appendArrowIpc(topic, Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 27. appendArrowIpc — invalid topic
// ===========================================================================

TEST(PluginHostWriteTest, AppendArrowIpcInvalidTopic) {
  DataEngine engine;
  PluginHostWrite host(engine);

  std::vector<uint8_t> dummy = {0};
  auto status = host.appendArrowIpc(TopicHandle{.id = 999}, Span<const uint8_t>(dummy));
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 28. appendRecord — type mismatch on re-creating existing field
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFieldTypeMismatchOnSecondCall) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "mismatch");

  // First record creates field "x" as float64
  std::vector<NamedFieldValue> fields1 = {
      {.name = "x", .type = FieldType::kFloat64, .value = 1.0},
  };
  ASSERT_TRUE(host.appendRecord(topic, 100, fields1).has_value());

  // Second record tries "x" as int32 — should fail
  std::vector<NamedFieldValue> fields2 = {
      {.name = "x", .type = FieldType::kInt32, .value = int32_t{1}},
  };
  auto status = host.appendRecord(topic, 200, fields2);
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// 29. ensureTopic — same name on different sources are independent
// ===========================================================================

TEST(PluginHostWriteTest, EnsureTopicCrossSrcIndependence) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src1 = *host.createDataSource("s1");
  auto src2 = *host.createDataSource("s2");

  auto t1 = *host.ensureTopic(src1, "data");
  auto t2 = *host.ensureTopic(src2, "data");

  // Same name, different sources → different topic IDs
  EXPECT_NE(t1.id, t2.id);
}

// ===========================================================================
// 30. appendRecordFast with nulls
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastWithNulls) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "fastnull");
  auto fx = *host.ensureField(topic, "x", FieldType::kFloat64);
  auto fy = *host.ensureField(topic, "y", FieldType::kFloat64);

  std::vector<BoundFieldValue> fields = {
      {.field = fx, .value = 5.0},
      {.field = fy, .is_null = true, .value = 0.0},
  };
  ASSERT_TRUE(host.appendRecordFast(topic, 1000, fields).has_value());
  host.flush();

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic.id, .t_min = 0, .t_max = 2000});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([](const SampleRow& row) {
    EXPECT_DOUBLE_EQ(row.chunk->readNumericAsDouble(0, row.row_index), 5.0);
    EXPECT_TRUE(row.chunk->isNull(1, row.row_index));
  });
}

// ===========================================================================
// BUG REPRO #1: appendRecord must validate ValueRef type matches FieldType
//
// appendRecord resolves fields from NamedFieldValue.type but writes using
// the runtime ValueRef variant without checking they match.  A mismatched
// pair (type = kFloat64, value = int32_t) hits a storage-kind assert in the
// column buffer layer instead of returning a Status error.
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordValueTypeMismatchReturnsError) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "mismatch");

  // Declare field as float64, but pass int32_t value
  std::vector<NamedFieldValue> fields = {
      {.name = "x", .type = FieldType::kFloat64, .value = int32_t{42}},
  };
  auto status = host.appendRecord(topic, 100, fields);
  // Should return error, not crash via assert()
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// BUG REPRO #2: appendRecordFast must reject foreign/stale FieldHandles
//
// appendRecordFast only validates handles if they were previously seen by
// this PluginHostWrite instance (cached in field_types_).  A fabricated or
// restored FieldHandle bypasses type validation and is used as a raw
// column index, causing out-of-bounds access.
// ===========================================================================

TEST(PluginHostWriteTest, AppendRecordFastRejectsForeignHandle) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "foreign");
  // Only one field created (field id 0)
  ASSERT_TRUE(host.ensureField(topic, "x", FieldType::kFloat64).has_value());

  // Fabricate a FieldHandle with correct topic but out-of-range field id
  FieldHandle foreign{.topic = topic, .id = 999};
  std::vector<BoundFieldValue> fields = {
      {.field = foreign, .value = 1.0},
  };
  auto status = host.appendRecordFast(topic, 100, fields);
  // Should return error, not crash via out-of-bounds
  EXPECT_FALSE(status.has_value());
}

// ===========================================================================
// BUG REPRO #4: appendArrowIpc narrow-type widening mismatch
//
// When schemaFromIpc() reports int8/int16, the host layer widens the field
// to FieldType::kInt32 (StorageKind::kInt32) via ensureField(), but leaves
// ArrowColumnMapping::pj_type at the original narrow PrimitiveType.
// storageKindOf(kInt8) = kInt64, so the arrow importer writes Int64 data
// into an Int32 column → storage kind assert.
// ===========================================================================

TEST(PluginHostWriteTest, AppendArrowIpcNarrowIntDoesNotCrash) {
  DataEngine engine;
  PluginHostWrite host(engine);

  auto src = *host.createDataSource("src");
  auto topic = *host.ensureTopic(src, "narrow");

  // Build Arrow IPC: struct { int64 "_timestamp", int8 "temp" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 2), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT64), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "_timestamp"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[1]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_INT8), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[1], "temp"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  for (int64_t i = 0; i < 5; ++i) {
    ASSERT_EQ(ArrowArrayAppendInt(array->children[0], i * 100), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayAppendInt(array->children[1], i * 10), NANOARROW_OK);  // int8 range
    ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  }
  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serializeToIpc(schema.get(), array.get());

  // Should succeed without assert/crash
  auto status = host.appendArrowIpc(topic, Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  ASSERT_TRUE(status.has_value()) << status.error();
  host.flush();

  // Verify data roundtrips correctly
  PluginHostRead reader(engine);
  auto view = reader.catalogView();
  ASSERT_EQ(view.fields.size(), 1U);

  auto series_or = reader.readSeries(view.fields[0].handle);
  ASSERT_TRUE(series_or.has_value()) << series_or.error();
  EXPECT_EQ(series_or->timestamps.size(), 5U);
}

}  // namespace
}  // namespace PJ
