// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/arrow_import.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow.hpp"
#include "nanoarrow/nanoarrow_ipc.h"
#include "pj_base/dataset.hpp"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/reader.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ::arrow_import {
namespace {

// ---------------------------------------------------------------------------
// Helper: serialize an ArrowArrayStream to IPC bytes using nanoarrow
// ---------------------------------------------------------------------------

// Build an IPC byte buffer from a schema + array. The array must be a struct
// array whose children are the column arrays.
std::vector<uint8_t> serialize_to_ipc(ArrowSchema* schema, ArrowArray* array) {
  // Create output buffer
  ArrowBuffer out_buf;
  ArrowBufferInit(&out_buf);

  // Create output stream backed by buffer
  ArrowIpcOutputStream out_stream;
  EXPECT_EQ(ArrowIpcOutputStreamInitBuffer(&out_stream, &out_buf), NANOARROW_OK);

  // Create writer
  ArrowIpcWriter writer;
  EXPECT_EQ(ArrowIpcWriterInit(&writer, &out_stream), NANOARROW_OK);

  // Write schema
  ArrowError error;
  EXPECT_EQ(ArrowIpcWriterWriteSchema(&writer, schema, &error), NANOARROW_OK) << error.message;

  // Create array view from schema for writing
  nanoarrow::UniqueArrayView view;
  EXPECT_EQ(ArrowArrayViewInitFromSchema(view.get(), schema, nullptr), NANOARROW_OK);
  EXPECT_EQ(ArrowArrayViewSetArray(view.get(), array, nullptr), NANOARROW_OK);

  // Write array as a record batch
  EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, view.get(), &error), NANOARROW_OK) << error.message;

  // Write end-of-stream marker
  EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, nullptr, &error), NANOARROW_OK);

  ArrowIpcWriterReset(&writer);

  // Copy bytes out
  std::vector<uint8_t> result(static_cast<std::size_t>(out_buf.size_bytes));
  std::memcpy(result.data(), out_buf.data, result.size());
  ArrowBufferReset(&out_buf);

  return result;
}

// Serialize multiple batches into a single IPC stream
std::vector<uint8_t> serialize_batches_to_ipc(ArrowSchema* schema, std::vector<ArrowArray*> batches) {
  ArrowBuffer out_buf;
  ArrowBufferInit(&out_buf);

  ArrowIpcOutputStream out_stream;
  EXPECT_EQ(ArrowIpcOutputStreamInitBuffer(&out_stream, &out_buf), NANOARROW_OK);

  ArrowIpcWriter writer;
  EXPECT_EQ(ArrowIpcWriterInit(&writer, &out_stream), NANOARROW_OK);

  ArrowError error;
  EXPECT_EQ(ArrowIpcWriterWriteSchema(&writer, schema, &error), NANOARROW_OK) << error.message;

  for (auto* batch : batches) {
    nanoarrow::UniqueArrayView view;
    EXPECT_EQ(ArrowArrayViewInitFromSchema(view.get(), schema, nullptr), NANOARROW_OK);
    EXPECT_EQ(ArrowArrayViewSetArray(view.get(), batch, nullptr), NANOARROW_OK);
    EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, view.get(), &error), NANOARROW_OK) << error.message;
  }

  EXPECT_EQ(ArrowIpcWriterWriteArrayView(&writer, nullptr, &error), NANOARROW_OK);
  ArrowIpcWriterReset(&writer);

  std::vector<uint8_t> result(static_cast<std::size_t>(out_buf.size_bytes));
  std::memcpy(result.data(), out_buf.data, result.size());
  ArrowBufferReset(&out_buf);

  return result;
}

// ===========================================================================
// Test: schema_from_ipc — mixed supported/unsupported types
// ===========================================================================

TEST(ArrowImportTest, SchemaFromIpc) {
  // Build schema: float32 "x", float64 "y", utf8 "name", list<int32> (skip)
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 4), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "x"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[1]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_DOUBLE), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[1], "y"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[2]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[2], NANOARROW_TYPE_STRING), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[2], "name"), NANOARROW_OK);

  // Unsupported type: list<int32>
  // ArrowSchemaSetType for LIST auto-allocates one child
  ArrowSchemaInit(schema->children[3]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[3], NANOARROW_TYPE_LIST), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[3], "unsupported"), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[3]->children[0], NANOARROW_TYPE_INT32), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[3]->children[0], "item"), NANOARROW_OK);

  // Build a minimal struct array with 0 rows just to serialize
  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  auto result_or = schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  ASSERT_TRUE(result_or.has_value()) << result_or.error();

  const auto& [type_tree, mappings] = *result_or;
  ASSERT_EQ(mappings.size(), 3u);

  EXPECT_EQ(mappings[0].field_name, "x");
  EXPECT_EQ(mappings[0].pj_type, PrimitiveType::kFloat32);
  EXPECT_EQ(mappings[0].pj_column_index, 0u);
  EXPECT_EQ(mappings[0].arrow_column_index, 0);

  EXPECT_EQ(mappings[1].field_name, "y");
  EXPECT_EQ(mappings[1].pj_type, PrimitiveType::kFloat64);

  EXPECT_EQ(mappings[2].field_name, "name");
  EXPECT_EQ(mappings[2].pj_type, PrimitiveType::kString);
  EXPECT_EQ(mappings[2].arrow_column_index, 2);

  EXPECT_EQ(type_tree->name, "arrow_row");
  EXPECT_EQ(type_tree->children.size(), 3u);
}

// ===========================================================================
// Test: import float32 columns via IPC
// ===========================================================================

TEST(ArrowImportTest, ImportFloat32) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { float32 "x", float32 "y" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 2), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "x"), NANOARROW_OK);

  ArrowSchemaInit(schema->children[1]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[1], "y"), NANOARROW_OK);

  // Build array with 100 rows
  constexpr int64_t N = 100;
  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  std::vector<float> x_vals(N), y_vals(N);
  for (int64_t i = 0; i < N; ++i) {
    x_vals[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.1F;
    y_vals[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.2F;

    ASSERT_EQ(
        ArrowArrayAppendDouble(array->children[0], static_cast<double>(x_vals[static_cast<std::size_t>(i)])),
        NANOARROW_OK);
    ASSERT_EQ(
        ArrowArrayAppendDouble(array->children[1], static_cast<double>(y_vals[static_cast<std::size_t>(i)])),
        NANOARROW_OK);
    ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  }

  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  // Parse schema and register
  auto [type_tree, mappings] = *schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  auto schema_id = *writer.registerSchema("test_schema", type_tree);

  TopicDescriptor desc;
  desc.name = "test_topic";
  desc.schema_id = schema_id;
  auto topic_id = *writer.registerTopic(*ds_or, desc);

  // Import
  auto status =
      importIpcStream(writer, topic_id, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  // Verify round-trip
  DataReader reader = engine.createReader();
  std::size_t count = 0;
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = topic_id, .t_min = 0, .t_max = N - 1});
  ASSERT_TRUE(cursor_or.has_value()) << cursor_or.error();
  cursor_or->forEach([&](const SampleRow& row) {
    auto x = static_cast<float>(row.chunk->readNumericAsDouble(0, row.row_index));
    EXPECT_FLOAT_EQ(x, x_vals[count]);
    ++count;
  });
  EXPECT_EQ(count, static_cast<std::size_t>(N));
}

// ===========================================================================
// Test: import with explicit timestamp column
// ===========================================================================

TEST(ArrowImportTest, ImportWithTimestampColumn) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { int64 "timestamp", float64 "value" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 2), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT64), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "timestamp"), NANOARROW_OK);

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

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  // Only map "value" column (not timestamp)
  std::vector<ArrowColumnMapping> mappings = {{
      .arrow_column_index = 1,
      .pj_column_index = 0,
      .pj_type = PrimitiveType::kFloat64,
      .field_name = "value",
  }};

  auto val_tree = makePrimitive("value", PrimitiveType::kFloat64);
  auto sid = *writer.registerSchema("ts_schema", val_tree);
  TopicDescriptor desc;
  desc.name = "ts_topic";
  desc.schema_id = sid;
  auto tid = *writer.registerTopic(*ds_or, desc);

  // Import with timestamp_column=0
  auto status = importIpcStream(writer, tid, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings, 0);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  // Verify timestamps
  DataReader reader = engine.createReader();
  auto latest_or = reader.latestAt(QueryPoint{.topic_id = tid, .t = 25000});
  ASSERT_TRUE(latest_or.has_value()) << latest_or.error();
  ASSERT_TRUE(latest_or->has_value());
  EXPECT_EQ((*latest_or)->timestamp, 25000);
  EXPECT_DOUBLE_EQ((*latest_or)->chunk->readNumericAsDouble(0, (*latest_or)->row_index), 25.0 * 0.5);
}

// ===========================================================================
// Test: import string columns
// ===========================================================================

TEST(ArrowImportTest, ImportStrings) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { utf8 "name" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 1), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_STRING), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "name"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  ArrowStringView sv;

  sv = ArrowCharView("alpha");
  ASSERT_EQ(ArrowArrayAppendString(array->children[0], sv), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  sv = ArrowCharView("bravo");
  ASSERT_EQ(ArrowArrayAppendString(array->children[0], sv), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  sv = ArrowCharView("charlie");
  ASSERT_EQ(ArrowArrayAppendString(array->children[0], sv), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  auto [type_tree, mappings] = *schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  auto sid = *writer.registerSchema("str_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "str_topic";
  desc.schema_id = sid;
  auto tid = *writer.registerTopic(*ds_or, desc);

  auto status = importIpcStream(writer, tid, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  // Verify strings
  DataReader reader = engine.createReader();
  std::vector<std::string> read_strings;
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([&](const SampleRow& row) { read_strings.emplace_back(row.chunk->readString(0, row.row_index)); });
  ASSERT_EQ(read_strings.size(), 3u);
  EXPECT_EQ(read_strings[0], "alpha");
  EXPECT_EQ(read_strings[1], "bravo");
  EXPECT_EQ(read_strings[2], "charlie");
}

// ===========================================================================
// Test: narrow integer widening (int8 → int64)
// ===========================================================================

TEST(ArrowImportTest, ImportNarrowIntegerWidening) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { int8 "val" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 1), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT8), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "val"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayAppendInt(array->children[0], 10), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayAppendInt(array->children[0], -20), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayAppendInt(array->children[0], 127), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  auto [type_tree, mappings] = *schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  auto sid = *writer.registerSchema("i8_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "i8_topic";
  desc.schema_id = sid;
  auto tid = *writer.registerTopic(*ds_or, desc);

  auto status = importIpcStream(writer, tid, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  DataReader reader = engine.createReader();
  std::vector<double> values;
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([&](const SampleRow& row) { values.push_back(row.chunk->readNumericAsDouble(0, row.row_index)); });
  ASSERT_EQ(values.size(), 3u);
  EXPECT_DOUBLE_EQ(values[0], 10.0);
  EXPECT_DOUBLE_EQ(values[1], -20.0);
  EXPECT_DOUBLE_EQ(values[2], 127.0);
}

// ===========================================================================
// Test: large dataset (500+ rows, multiple chunks)
// ===========================================================================

TEST(ArrowImportTest, ImportLargeDataset) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { float64 "value" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 1), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_DOUBLE), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "value"), NANOARROW_OK);

  constexpr int64_t N = 500;
  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  for (int64_t i = 0; i < N; ++i) {
    ASSERT_EQ(ArrowArrayAppendDouble(array->children[0], static_cast<double>(i)), NANOARROW_OK);
    ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);
  }

  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  auto [type_tree, mappings] = *schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  auto sid = *writer.registerSchema("tbl_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "tbl_topic";
  desc.schema_id = sid;
  desc.max_chunk_rows = 128;
  auto tid = *writer.registerTopic(*ds_or, desc);

  auto status = importIpcStream(writer, tid, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  DataReader reader = engine.createReader();
  std::size_t count = 0;
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = tid, .t_min = 0, .t_max = N - 1});
  ASSERT_TRUE(cursor_or.has_value());
  cursor_or->forEach([&](const SampleRow&) { ++count; });
  EXPECT_EQ(count, static_cast<std::size_t>(N));
}

// ===========================================================================
// Test: import with nulls
// ===========================================================================

TEST(ArrowImportTest, ImportWithNulls) {
  DataEngine engine;
  auto ds_or = engine.createDataset(DatasetDescriptor{.source_name = "test", .time_domain_id = 0});
  ASSERT_TRUE(ds_or.has_value());

  DataWriter writer = engine.createWriter();

  // Build schema: struct { float32 "val" }
  nanoarrow::UniqueSchema schema;
  ASSERT_EQ(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRUCT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaAllocateChildren(schema.get(), 1), NANOARROW_OK);

  ArrowSchemaInit(schema->children[0]);
  ASSERT_EQ(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetName(schema->children[0], "val"), NANOARROW_OK);

  nanoarrow::UniqueArray array;
  ASSERT_EQ(ArrowArrayInitFromSchema(array.get(), schema.get(), nullptr), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayStartAppending(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayAppendDouble(array->children[0], 1.0), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayAppendNull(array->children[0], 1), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayAppendDouble(array->children[0], 3.0), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayAppendNull(array->children[0], 1), NANOARROW_OK);
  ASSERT_EQ(ArrowArrayFinishElement(array.get()), NANOARROW_OK);

  ASSERT_EQ(ArrowArrayFinishBuildingDefault(array.get(), nullptr), NANOARROW_OK);

  auto ipc_bytes = serialize_to_ipc(schema.get(), array.get());

  auto [type_tree, mappings] = *schemaFromIpc(PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()));
  auto sid = *writer.registerSchema("null_schema", type_tree);
  TopicDescriptor desc;
  desc.name = "null_topic";
  desc.schema_id = sid;
  auto tid = *writer.registerTopic(*ds_or, desc);

  auto status = importIpcStream(writer, tid, PJ::Span<const uint8_t>(ipc_bytes.data(), ipc_bytes.size()), mappings);
  ASSERT_TRUE(status.has_value()) << status.error();

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  DataReader reader = engine.createReader();
  auto cursor_or = reader.rangeQuery(QueryRange{.topic_id = tid, .t_min = 0, .t_max = 10});
  ASSERT_TRUE(cursor_or.has_value());
  std::size_t row = 0;
  cursor_or->forEach([&](const SampleRow& r) {
    if (row == 0) {
      EXPECT_FALSE(r.chunk->isNull(0, r.row_index));
      EXPECT_FLOAT_EQ(static_cast<float>(r.chunk->readNumericAsDouble(0, r.row_index)), 1.0F);
    } else if (row == 1) {
      EXPECT_TRUE(r.chunk->isNull(0, r.row_index));
    } else if (row == 2) {
      EXPECT_FALSE(r.chunk->isNull(0, r.row_index));
      EXPECT_FLOAT_EQ(static_cast<float>(r.chunk->readNumericAsDouble(0, r.row_index)), 3.0F);
    } else if (row == 3) {
      EXPECT_TRUE(r.chunk->isNull(0, r.row_index));
    }
    ++row;
  });
  EXPECT_EQ(row, 4u);
}

}  // namespace
}  // namespace PJ::arrow_import
