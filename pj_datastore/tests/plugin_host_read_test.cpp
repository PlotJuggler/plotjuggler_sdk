#include "pj_datastore/plugin_host_read.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_host_types.hpp"
#include "pj_datastore/plugin_host_write.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {
namespace {

// ===========================================================================
// Helper: create a populated engine via PluginHostWrite
// ===========================================================================

struct TestFixture {
  DataEngine engine;
  PluginHostWrite writer{engine};
  PluginHostRead reader{engine};
};

// ===========================================================================
// 1. catalogView — empty catalog
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewEmpty) {
  TestFixture f;
  auto view = f.reader.catalogView();

  EXPECT_TRUE(view.data_sources.empty());
  EXPECT_TRUE(view.topics.empty());
  EXPECT_TRUE(view.fields.empty());
}

// ===========================================================================
// 2. catalogView — single source, single topic, multiple fields
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewSingleTopicMultipleFields) {
  TestFixture f;

  auto src = *f.writer.createDataSource("sensor");
  auto topic = *f.writer.ensureTopic(src, "imu");
  ASSERT_TRUE(f.writer.ensureField(topic, "x", FieldType::kFloat32).has_value());
  ASSERT_TRUE(f.writer.ensureField(topic, "y", FieldType::kFloat32).has_value());
  ASSERT_TRUE(f.writer.ensureField(topic, "z", FieldType::kFloat32).has_value());

  auto view = f.reader.catalogView();

  ASSERT_EQ(view.data_sources.size(), 1U);
  EXPECT_EQ(view.data_sources[0].name, "sensor");
  EXPECT_EQ(view.data_sources[0].topic_count, 1U);

  ASSERT_EQ(view.topics.size(), 1U);
  EXPECT_EQ(view.topics[0].name, "imu");
  EXPECT_EQ(view.topics[0].field_count, 3U);
  EXPECT_EQ(view.topics[0].source, src);

  ASSERT_EQ(view.fields.size(), 3U);
  EXPECT_EQ(view.fields[0].name, "x");
  EXPECT_EQ(view.fields[0].type, FieldType::kFloat32);
  EXPECT_EQ(view.fields[1].name, "y");
  EXPECT_EQ(view.fields[2].name, "z");

  // Verify index ranges: topic's first_field and field_count
  uint32_t first = view.topics[0].first_field;
  uint32_t count = view.topics[0].field_count;
  EXPECT_EQ(first, 0U);
  EXPECT_EQ(count, 3U);

  // Verify data_source's first_topic and topic_count
  EXPECT_EQ(view.data_sources[0].first_topic, 0U);
  EXPECT_EQ(view.data_sources[0].topic_count, 1U);
}

// ===========================================================================
// 3. catalogView — multiple sources, multiple topics
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewMultiSourceMultiTopic) {
  TestFixture f;

  auto s1 = *f.writer.createDataSource("robot1");
  auto s2 = *f.writer.createDataSource("robot2");

  auto t1a = *f.writer.ensureTopic(s1, "gps");
  auto t1b = *f.writer.ensureTopic(s1, "imu");
  auto t2a = *f.writer.ensureTopic(s2, "lidar");

  ASSERT_TRUE(f.writer.ensureField(t1a, "lat", FieldType::kFloat64).has_value());
  ASSERT_TRUE(f.writer.ensureField(t1a, "lon", FieldType::kFloat64).has_value());
  ASSERT_TRUE(f.writer.ensureField(t1b, "ax", FieldType::kFloat32).has_value());
  ASSERT_TRUE(f.writer.ensureField(t2a, "range", FieldType::kFloat32).has_value());
  ASSERT_TRUE(f.writer.ensureField(t2a, "angle", FieldType::kFloat32).has_value());

  auto view = f.reader.catalogView();

  EXPECT_EQ(view.data_sources.size(), 2U);

  // Total topics across both sources
  EXPECT_EQ(view.topics.size(), 3U);

  // Total fields
  EXPECT_EQ(view.fields.size(), 5U);

  // source 1 has 2 topics
  EXPECT_EQ(view.data_sources[0].topic_count, 2U);

  // source 2 has 1 topic
  EXPECT_EQ(view.data_sources[1].topic_count, 1U);

  // Verify index ranges compose correctly
  for (std::size_t si = 0; si < view.data_sources.size(); ++si) {
    const auto& ds = view.data_sources[si];
    for (uint32_t ti = 0; ti < ds.topic_count; ++ti) {
      const auto& t = view.topics[ds.first_topic + ti];
      // Every field in this topic should have its handle.topic match
      for (uint32_t fi = 0; fi < t.field_count; ++fi) {
        const auto& field = view.fields[t.first_field + fi];
        EXPECT_EQ(field.handle.topic, t.handle);
      }
    }
  }
}

// ===========================================================================
// 4. catalogView — field handles carry correct topic reference
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewFieldHandleTopic) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat64);

  auto view = f.reader.catalogView();
  ASSERT_EQ(view.fields.size(), 1U);
  EXPECT_EQ(view.fields[0].handle.topic, topic);
  EXPECT_EQ(view.fields[0].handle.id, fld.id);
}

// ===========================================================================
// 5. catalogView — re-acquire after structural mutation
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewReacquireAfterMutation) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  ASSERT_TRUE(f.writer.ensureField(topic, "a", FieldType::kFloat64).has_value());

  auto view1 = f.reader.catalogView();
  EXPECT_EQ(view1.fields.size(), 1U);

  // Add a new field (structural mutation)
  ASSERT_TRUE(f.writer.ensureField(topic, "b", FieldType::kFloat64).has_value());

  // Old view is potentially stale; re-acquire
  auto view2 = f.reader.catalogView();
  EXPECT_EQ(view2.fields.size(), 2U);
}

// ===========================================================================
// 6. readSeries — float64 series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesFloat64) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat64);

  constexpr std::size_t N = 100;
  for (std::size_t i = 0; i < N; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "val", .type = FieldType::kFloat64, .value = static_cast<double>(i) * 0.5},
    };
    ASSERT_TRUE(f.writer.appendRecord(topic, static_cast<Timestamp>(i) * 1000, fields).has_value());
  }
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value()) << series_or.error();

  const auto& series = *series_or;
  EXPECT_EQ(series.type, FieldType::kFloat64);
  EXPECT_EQ(series.source, src);
  EXPECT_EQ(series.topic, topic);
  EXPECT_EQ(series.field, fld);
  EXPECT_EQ(series.timestamps.size(), N);

  const auto& vals = std::get<std::vector<double>>(series.values);
  ASSERT_EQ(vals.size(), N);

  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(series.timestamps[i], static_cast<Timestamp>(i) * 1000);
    EXPECT_DOUBLE_EQ(vals[i], static_cast<double>(i) * 0.5);
  }

  // All valid — check validity bits
  for (std::size_t i = 0; i < N; ++i) {
    const bool valid = (series.validity_bits[i / 8] & (1U << (i % 8))) != 0;
    EXPECT_TRUE(valid) << "Row " << i << " should be valid";
  }
}

// ===========================================================================
// 7. readSeries — float32 series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesFloat32) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat32);

  std::vector<NamedFieldValue> fields = {
      {.name = "val", .type = FieldType::kFloat32, .value = 3.14F},
  };
  ASSERT_TRUE(f.writer.appendRecord(topic, 1000, fields).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  EXPECT_EQ(series_or->type, FieldType::kFloat32);
  const auto& vals = std::get<std::vector<float>>(series_or->values);
  ASSERT_EQ(vals.size(), 1U);
  EXPECT_FLOAT_EQ(vals[0], 3.14F);
}

// ===========================================================================
// 8. readSeries — int32 series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesInt32) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kInt32);

  std::vector<NamedFieldValue> fields = {
      {.name = "val", .type = FieldType::kInt32, .value = int32_t{-42}},
  };
  ASSERT_TRUE(f.writer.appendRecord(topic, 1000, fields).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  const auto& vals = std::get<std::vector<int32_t>>(series_or->values);
  ASSERT_EQ(vals.size(), 1U);
  EXPECT_EQ(vals[0], -42);
}

// ===========================================================================
// 9. readSeries — int64 series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesInt64) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kInt64);

  std::vector<NamedFieldValue> fields = {
      {.name = "val", .type = FieldType::kInt64, .value = int64_t{9999999999LL}},
  };
  ASSERT_TRUE(f.writer.appendRecord(topic, 1000, fields).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  const auto& vals = std::get<std::vector<int64_t>>(series_or->values);
  ASSERT_EQ(vals.size(), 1U);
  EXPECT_EQ(vals[0], 9999999999LL);
}

// ===========================================================================
// 10. readSeries — uint64 series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesUint64) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kUint64);

  std::vector<NamedFieldValue> fields = {
      {.name = "val", .type = FieldType::kUint64, .value = uint64_t{0xDEADBEEF}},
  };
  ASSERT_TRUE(f.writer.appendRecord(topic, 1000, fields).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  const auto& vals = std::get<std::vector<uint64_t>>(series_or->values);
  ASSERT_EQ(vals.size(), 1U);
  EXPECT_EQ(vals[0], uint64_t{0xDEADBEEF});
}

// ===========================================================================
// 11. readSeries — bool series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesBool) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "flag", FieldType::kBool);

  for (int i = 0; i < 5; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "flag", .type = FieldType::kBool, .value = (i % 2 == 0)},
    };
    ASSERT_TRUE(f.writer.appendRecord(topic, static_cast<Timestamp>(i) * 100, fields).has_value());
  }
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  EXPECT_EQ(series_or->type, FieldType::kBool);
  const auto& bsv = std::get<BoolSeriesValues>(series_or->values);
  ASSERT_EQ(bsv.values.size(), 5U);
  EXPECT_EQ(bsv.values[0], 1);  // true
  EXPECT_EQ(bsv.values[1], 0);  // false
  EXPECT_EQ(bsv.values[2], 1);  // true
  EXPECT_EQ(bsv.values[3], 0);  // false
  EXPECT_EQ(bsv.values[4], 1);  // true
}

// ===========================================================================
// 12. readSeries — string series
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesString) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "label", FieldType::kString);

  const char* labels[] = {"alpha", "bravo", "charlie"};
  for (int i = 0; i < 3; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "label", .type = FieldType::kString, .value = std::string_view(labels[i])},
    };
    ASSERT_TRUE(f.writer.appendRecord(topic, static_cast<Timestamp>(i) * 100, fields).has_value());
  }
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());
  EXPECT_EQ(series_or->type, FieldType::kString);

  const auto& ssv = std::get<StringSeriesValues>(series_or->values);
  ASSERT_EQ(ssv.offsets.size(), 4U);  // N+1 offsets for N strings

  // Reconstruct and verify each string
  for (std::size_t i = 0; i < 3; ++i) {
    uint32_t start = ssv.offsets[i];
    uint32_t end = ssv.offsets[i + 1];
    std::string_view sv(ssv.bytes.data() + start, end - start);
    EXPECT_EQ(sv, labels[i]) << "String mismatch at index " << i;
  }
}

// ===========================================================================
// 13. readSeries — with nulls and validity bits
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesWithNulls) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat64);

  // Row 0: 1.0, Row 1: null, Row 2: 3.0, Row 3: null
  for (int i = 0; i < 4; ++i) {
    const bool is_null = (i % 2 == 1);
    std::vector<NamedFieldValue> fields = {
        {.name = "val", .type = FieldType::kFloat64, .is_null = is_null, .value = static_cast<double>(i)},
    };
    ASSERT_TRUE(f.writer.appendRecord(topic, static_cast<Timestamp>(i) * 100, fields).has_value());
  }
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());

  const auto& series = *series_or;
  const auto& vals = std::get<std::vector<double>>(series.values);
  ASSERT_EQ(vals.size(), 4U);

  // Check validity bits
  auto isValid = [&](std::size_t row) -> bool {
    return (series.validity_bits[row / 8] & (1U << (row % 8))) != 0;
  };

  EXPECT_TRUE(isValid(0));
  EXPECT_DOUBLE_EQ(vals[0], 0.0);

  EXPECT_FALSE(isValid(1));  // null

  EXPECT_TRUE(isValid(2));
  EXPECT_DOUBLE_EQ(vals[2], 2.0);

  EXPECT_FALSE(isValid(3));  // null
}

// ===========================================================================
// 14. readSeries — across multiple chunks
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesMultipleChunks) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat64);

  // Write enough rows to span multiple chunks (default 1024)
  constexpr std::size_t N = 3000;
  for (std::size_t i = 0; i < N; ++i) {
    std::vector<NamedFieldValue> fields = {
        {.name = "val", .type = FieldType::kFloat64, .value = static_cast<double>(i)},
    };
    ASSERT_TRUE(f.writer.appendRecord(topic, static_cast<Timestamp>(i), fields).has_value());
  }
  f.writer.flush();

  // Verify multiple chunks
  const auto* storage = f.engine.getTopicStorage(topic.id);
  ASSERT_NE(storage, nullptr);
  EXPECT_GE(storage->sealedChunks().size(), 2U);

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());

  const auto& vals = std::get<std::vector<double>>(series_or->values);
  ASSERT_EQ(vals.size(), N);
  ASSERT_EQ(series_or->timestamps.size(), N);

  // Verify first, middle, and last
  EXPECT_DOUBLE_EQ(vals[0], 0.0);
  EXPECT_DOUBLE_EQ(vals[N / 2], static_cast<double>(N / 2));
  EXPECT_DOUBLE_EQ(vals[N - 1], static_cast<double>(N - 1));

  EXPECT_EQ(series_or->timestamps[0], 0);
  EXPECT_EQ(series_or->timestamps[N - 1], static_cast<Timestamp>(N - 1));
}

// ===========================================================================
// 15. readSeries — unknown field error
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesUnknownField) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");

  // Field 999 doesn't exist
  FieldHandle bad_field{.topic = topic, .id = 999};
  auto result = f.reader.readSeries(bad_field);
  EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// 16. readSeries — unknown topic error
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesUnknownTopic) {
  TestFixture f;

  FieldHandle bad_field{.topic = TopicHandle{.id = 999}, .id = 0};
  auto result = f.reader.readSeries(bad_field);
  EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// 17. readSeries — empty field (no data written yet)
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesEmptyField) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "val", FieldType::kFloat64);

  // No data written — field exists but has no rows
  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());

  EXPECT_TRUE(series_or->timestamps.empty());
  const auto& vals = std::get<std::vector<double>>(series_or->values);
  EXPECT_TRUE(vals.empty());
}

// ===========================================================================
// 18. readSeries — string with nulls
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesStringWithNulls) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "label", FieldType::kString);

  // "hello", null, "world"
  std::vector<NamedFieldValue> f1 = {
      {.name = "label", .type = FieldType::kString, .value = std::string_view("hello")},
  };
  std::vector<NamedFieldValue> f2 = {
      {.name = "label", .type = FieldType::kString, .is_null = true, .value = std::string_view("")},
  };
  std::vector<NamedFieldValue> f3 = {
      {.name = "label", .type = FieldType::kString, .value = std::string_view("world")},
  };
  ASSERT_TRUE(f.writer.appendRecord(topic, 100, f1).has_value());
  ASSERT_TRUE(f.writer.appendRecord(topic, 200, f2).has_value());
  ASSERT_TRUE(f.writer.appendRecord(topic, 300, f3).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());

  const auto& ssv = std::get<StringSeriesValues>(series_or->values);
  ASSERT_EQ(ssv.offsets.size(), 4U);

  auto isValid = [&](std::size_t row) -> bool {
    return (series_or->validity_bits[row / 8] & (1U << (row % 8))) != 0;
  };

  // Row 0: "hello" (valid)
  EXPECT_TRUE(isValid(0));
  std::string_view s0(ssv.bytes.data() + ssv.offsets[0], ssv.offsets[1] - ssv.offsets[0]);
  EXPECT_EQ(s0, "hello");

  // Row 1: null
  EXPECT_FALSE(isValid(1));

  // Row 2: "world" (valid)
  EXPECT_TRUE(isValid(2));
  std::string_view s2(ssv.bytes.data() + ssv.offsets[2], ssv.offsets[3] - ssv.offsets[2]);
  EXPECT_EQ(s2, "world");
}

// ===========================================================================
// 19. catalogView — no fields on empty topic
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewEmptyTopic) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  ASSERT_TRUE(f.writer.ensureTopic(src, "empty_topic").has_value());

  auto view = f.reader.catalogView();
  ASSERT_EQ(view.topics.size(), 1U);
  EXPECT_EQ(view.topics[0].field_count, 0U);
  EXPECT_TRUE(view.fields.empty());
}

// ===========================================================================
// 20. readSeries — bool with nulls
// ===========================================================================

TEST(PluginHostReadTest, ReadSeriesBoolWithNulls) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto topic = *f.writer.ensureTopic(src, "data");
  auto fld = *f.writer.ensureField(topic, "flag", FieldType::kBool);

  // true, null, false
  std::vector<NamedFieldValue> r1 = {{.name = "flag", .type = FieldType::kBool, .value = true}};
  std::vector<NamedFieldValue> r2 = {{.name = "flag", .type = FieldType::kBool, .is_null = true, .value = false}};
  std::vector<NamedFieldValue> r3 = {{.name = "flag", .type = FieldType::kBool, .value = false}};

  ASSERT_TRUE(f.writer.appendRecord(topic, 100, r1).has_value());
  ASSERT_TRUE(f.writer.appendRecord(topic, 200, r2).has_value());
  ASSERT_TRUE(f.writer.appendRecord(topic, 300, r3).has_value());
  f.writer.flush();

  auto series_or = f.reader.readSeries(fld);
  ASSERT_TRUE(series_or.has_value());

  const auto& bsv = std::get<BoolSeriesValues>(series_or->values);
  ASSERT_EQ(bsv.values.size(), 3U);

  auto isValid = [&](std::size_t row) -> bool {
    return (series_or->validity_bits[row / 8] & (1U << (row % 8))) != 0;
  };

  EXPECT_TRUE(isValid(0));
  EXPECT_EQ(bsv.values[0], 1);

  EXPECT_FALSE(isValid(1));  // null

  EXPECT_TRUE(isValid(2));
  EXPECT_EQ(bsv.values[2], 0);
}

// ===========================================================================
// BUG REPRO #3: Schema-backed topics should be visible via catalogView()
//
// catalogView() and readSeries() only look at TopicStorage::columnDescriptors().
// For schema-backed topics (schema_id != 0), the writer populates columns from
// the TypeRegistry, but never persists them to TopicStorage::columnDescriptors()
// unless ensureColumn()/expandArray() is called.  This means schema-backed
// topics appear with zero fields in the catalog and readSeries() fails.
// ===========================================================================

TEST(PluginHostReadTest, SchemaBackedTopicVisibleInCatalog) {
  DataEngine engine;

  // Register a schema: struct { float64 x, float64 y }
  auto type_tree = makeStruct("Pose2D", {
      makePrimitive("x", PrimitiveType::kFloat64),
      makePrimitive("y", PrimitiveType::kFloat64),
  });

  DataWriter writer = engine.createWriter();
  auto schema_id_or = writer.registerSchema("Pose2D", type_tree);
  ASSERT_TRUE(schema_id_or.has_value()) << schema_id_or.error();

  // Create dataset + topic with schema_id
  auto ds_id_or = engine.createDataset(DatasetDescriptor{.source_name = "src", .time_domain_id = 0});
  ASSERT_TRUE(ds_id_or.has_value());

  TopicDescriptor desc;
  desc.name = "pose";
  desc.schema_id = *schema_id_or;
  auto tid_or = writer.registerTopic(*ds_id_or, std::move(desc));
  ASSERT_TRUE(tid_or.has_value()) << tid_or.error();

  // Write data using the DataWriter's bound path
  auto handle_or = writer.bindTopicWriter(*tid_or);
  ASSERT_TRUE(handle_or.has_value()) << handle_or.error();

  ASSERT_TRUE(writer.beginRow(*tid_or, 1000).has_value());
  writer.setFloat64(*tid_or, 0, 1.5);
  writer.setFloat64(*tid_or, 1, 2.5);
  ASSERT_TRUE(writer.finishRow(*tid_or).has_value());

  auto flushed = writer.flushAll();
  engine.commitChunks(std::move(flushed));

  // Read via PluginHostRead — schema-backed topic should show fields
  PluginHostRead reader(engine);
  auto view = reader.catalogView();

  ASSERT_EQ(view.topics.size(), 1U);
  // BUG: currently returns 0 fields because columnDescriptors() is empty
  EXPECT_GE(view.topics[0].field_count, 2U) << "Schema-backed topic should expose fields";
  EXPECT_GE(view.fields.size(), 2U);

  // readSeries should also work for the first field
  if (view.fields.size() >= 1) {
    auto series_or = reader.readSeries(view.fields[0].handle);
    ASSERT_TRUE(series_or.has_value()) << series_or.error();
    EXPECT_EQ(series_or->timestamps.size(), 1U);
  }
}

// ===========================================================================
// BUG REPRO #5: catalogView() should return deterministic ordering
//
// listDatasets() iterates an absl::flat_hash_map, so dataset ordering is
// not guaranteed.  Tests that assume data_sources[0] is always the first
// created source can fail non-deterministically.  catalogView() should
// sort datasets by ID for stable output.
// ===========================================================================

TEST(PluginHostReadTest, CatalogViewDeterministicOrdering) {
  TestFixture f;

  // Create 10 data sources to increase probability of hash-map reorder
  std::vector<DataSourceHandle> handles;
  for (int i = 0; i < 10; ++i) {
    auto src = f.writer.createDataSource(std::string("source_") + std::to_string(i));
    ASSERT_TRUE(src.has_value());
    handles.push_back(*src);
  }

  auto view = f.reader.catalogView();
  ASSERT_EQ(view.data_sources.size(), 10U);

  // Verify data sources are in ascending ID order
  for (std::size_t i = 1; i < view.data_sources.size(); ++i) {
    EXPECT_LT(view.data_sources[i - 1].handle.id, view.data_sources[i].handle.id)
        << "Data sources should be sorted by ID for deterministic ordering";
  }
}

// Deterministic ordering: topics within a source should also be stable
TEST(PluginHostReadTest, CatalogViewTopicOrderingStable) {
  TestFixture f;

  auto src = *f.writer.createDataSource("src");
  auto t1 = *f.writer.ensureTopic(src, "alpha");
  auto t2 = *f.writer.ensureTopic(src, "bravo");
  auto t3 = *f.writer.ensureTopic(src, "charlie");

  auto view = f.reader.catalogView();
  ASSERT_EQ(view.topics.size(), 3U);

  // Topics should be in creation (insertion) order since listTopics returns a vector
  EXPECT_EQ(view.topics[0].name, "alpha");
  EXPECT_EQ(view.topics[1].name, "bravo");
  EXPECT_EQ(view.topics[2].name, "charlie");
}

}  // namespace
}  // namespace PJ
