#include "pj/engine/derived_engine.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "pj/base/dataset.hpp"
#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/builtin_transforms.hpp"
#include "pj/engine/chunk.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Create a dataset and return its id.
static pj::DatasetId make_dataset(DataEngine& engine, const std::string& name = "test") {
  auto id_or = engine.create_dataset(pj::DatasetDescriptor{.source_name = name, .time_domain_id = 0});
  return *id_or;
}

// Write `n` rows to a float64 scalar topic with value = slope * (t_ns / 1e9).
// Commits the chunk. Returns the TopicId.
// Timestamps: 0, step_ns, 2*step_ns, ...
static pj::TopicId make_linear_topic(
    DataEngine& engine, pj::DatasetId dataset_id, double slope, int n, pj::Timestamp step_ns = 1'000'000'000LL) {
  DataWriter writer = engine.create_writer();
  auto handle_or = writer.register_scalar_series(dataset_id, "src", pj::NumericType::kFloat64);
  pj::TopicId tid = handle_or->topic_id;
  for (int i = 0; i < n; ++i) {
    pj::Timestamp ts = static_cast<pj::Timestamp>(i) * step_ns;
    double v = slope * (static_cast<double>(i) * static_cast<double>(step_ns) * 1e-9);
    writer.append_scalar(*handle_or, ts, v);
  }
  // commit_chunks returns the changed topic IDs — pass directly to on_source_committed.
  engine.commit_chunks(writer.flush_all());
  return tid;
}

// Append `n` more rows to an existing scalar topic (continuing timestamps from start_i).
static void append_linear_rows(
    DataEngine& engine, pj::TopicId src_topic_id, double slope, int n, int start_i,
    pj::Timestamp step_ns = 1'000'000'000LL) {
  // We need to write to an existing topic via begin_row / set_float64 / finish_row.
  DataWriter writer = engine.create_writer();
  for (int i = start_i; i < start_i + n; ++i) {
    pj::Timestamp ts = static_cast<pj::Timestamp>(i) * step_ns;
    double v = slope * (static_cast<double>(i) * static_cast<double>(step_ns) * 1e-9);
    auto s = writer.begin_row(src_topic_id, ts);
    (void)s;
    writer.set_float64(src_topic_id, 0, v);
    auto s2 = writer.finish_row(src_topic_id);
    (void)s2;
  }
  engine.commit_chunks(writer.flush_all());
}

// Collect all float64 values from a topic in timestamp order.
static std::vector<double> collect_values(DataEngine& engine, pj::TopicId topic_id) {
  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  if (!storage) {
    return {};
  }
  std::vector<double> out;
  auto cursor = range_query(storage->sealed_chunks(), 0, std::numeric_limits<pj::Timestamp>::max());
  cursor.for_each([&](const SampleRow& row) { out.push_back(row.chunk->read_numeric_as_double(0, row.row_index)); });
  return out;
}

// Wrapper: on_source_committed from an initializer list (std::span can't take {}).
static void notify(DerivedEngine& derived, std::initializer_list<pj::TopicId> topics) {
  std::vector<pj::TopicId> v(topics);
  derived.on_source_committed(v);
}

// Collect (timestamp, value) pairs.
static std::vector<std::pair<pj::Timestamp, double>> collect_rows(DataEngine& engine, pj::TopicId topic_id) {
  const TopicStorage* storage = engine.get_topic_storage(topic_id);
  if (!storage) {
    return {};
  }
  std::vector<std::pair<pj::Timestamp, double>> out;
  auto cursor = range_query(storage->sealed_chunks(), 0, std::numeric_limits<pj::Timestamp>::max());
  cursor.for_each([&](const SampleRow& row) {
    out.emplace_back(row.timestamp, row.chunk->read_numeric_as_double(0, row.row_index));
  });
  return out;
}

// ---------------------------------------------------------------------------
// DerivativeTransform — unit tests (no engine needed)
// ---------------------------------------------------------------------------

TEST(DerivativeTransformTest, SkipsFirstRow) {
  DerivativeTransform op;
  pj::Timestamp t = 0;
  VarValue v = 0.0;
  EXPECT_FALSE(op.calculate(0, VarValue{0.0}, t, v));
}

TEST(DerivativeTransformTest, CorrectDerivative_ConstantRate) {
  DerivativeTransform op;
  pj::Timestamp out_t;
  VarValue out_v = 0.0;

  // slope = 3.0, step = 1s → dy/dt should be 3.0
  EXPECT_FALSE(op.calculate(0, VarValue{0.0}, out_t, out_v));
  EXPECT_TRUE(op.calculate(1'000'000'000LL, VarValue{3.0}, out_t, out_v));
  EXPECT_EQ(out_t, 1'000'000'000LL);
  EXPECT_NEAR(std::get<double>(out_v), 3.0, 1e-9);

  EXPECT_TRUE(op.calculate(2'000'000'000LL, VarValue{6.0}, out_t, out_v));
  EXPECT_NEAR(std::get<double>(out_v), 3.0, 1e-9);
}

TEST(DerivativeTransformTest, ZeroDt_DoesNotDivideByZero) {
  // dt = 0 should produce 0.0, not inf/nan
  DerivativeTransform op;
  pj::Timestamp out_t;
  VarValue out_v = 0.0;
  EXPECT_FALSE(op.calculate(0, VarValue{1.0}, out_t, out_v));
  EXPECT_TRUE(op.calculate(0, VarValue{2.0}, out_t, out_v));
  EXPECT_EQ(std::get<double>(out_v), 0.0);
}

TEST(DerivativeTransformTest, Reset_ClearsState) {
  DerivativeTransform op;
  pj::Timestamp out_t;
  VarValue out_v = 0.0;

  // First pass
  EXPECT_FALSE(op.calculate(0, VarValue{0.0}, out_t, out_v));
  EXPECT_TRUE(op.calculate(1'000'000'000LL, VarValue{5.0}, out_t, out_v));

  // After reset, first row must be suppressed again
  op.reset();
  EXPECT_FALSE(op.calculate(0, VarValue{0.0}, out_t, out_v));
  EXPECT_TRUE(op.calculate(1'000'000'000LL, VarValue{5.0}, out_t, out_v));
  EXPECT_NEAR(std::get<double>(out_v), 5.0, 1e-9);
}

TEST(DerivativeTransformTest, OutputKind_IsFloat64) {
  DerivativeTransform op;
  EXPECT_EQ(op.output_kind(StorageKind::kFloat64), StorageKind::kFloat64);
  EXPECT_EQ(op.output_kind(StorageKind::kFloat32), StorageKind::kFloat64);
  EXPECT_EQ(op.output_kind(StorageKind::kInt64), StorageKind::kFloat64);
}

// ---------------------------------------------------------------------------
// add_siso_transform
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, AddTransform_CreatesOutputTopic) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  auto node_or = derived.add_siso_transform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  pj::NodeId node = *node_or;
  EXPECT_TRUE(derived.has_node(node));

  auto out_topics = derived.output_topics(node);
  ASSERT_EQ(out_topics.size(), 1u);
  EXPECT_NE(engine.get_topic_storage(out_topics[0]), nullptr);
}

TEST(DerivedEngineTest, AddTransform_DuplicateOutputName_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  ASSERT_TRUE(derived.add_siso_transform(src, "deriv", ds, std::make_unique<DerivativeTransform>()).has_value());
  // Same output name → should fail
  auto r = derived.add_siso_transform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  EXPECT_FALSE(r.has_value());
}

TEST(DerivedEngineTest, AddTransform_UnknownInputTopic_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  auto r = derived.add_siso_transform(9999u, "deriv", ds, std::make_unique<DerivativeTransform>());
  EXPECT_FALSE(r.has_value());
}

// ---------------------------------------------------------------------------
// topological_order
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, TopologicalOrder_SingleNode) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  pj::NodeId n = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  auto order = derived.topological_order();
  ASSERT_EQ(order.size(), 1u);
  EXPECT_EQ(order[0], n);
}

TEST(DerivedEngineTest, TopologicalOrder_Chain_ABOrder) {
  // A → B: output of A is input of B. Order must be [A, B].
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  pj::NodeId a = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  pj::TopicId a_out = derived.output_topics(a)[0];
  pj::NodeId b = *derived.add_siso_transform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  auto order = derived.topological_order();
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], a);
  EXPECT_EQ(order[1], b);
}

TEST(DerivedEngineTest, TopologicalOrder_Fork) {
  // A → B and A → C: A must appear before both B and C.
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  pj::NodeId a = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  pj::TopicId a_out = derived.output_topics(a)[0];
  pj::NodeId b = *derived.add_siso_transform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());
  pj::NodeId c = *derived.add_siso_transform(a_out, "d3", ds, std::make_unique<DerivativeTransform>());

  auto order = derived.topological_order();
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], a);
  // B and C must both appear after A
  EXPECT_NE(std::find(order.begin(), order.end(), b), order.end());
  EXPECT_NE(std::find(order.begin(), order.end(), c), order.end());
}

// ---------------------------------------------------------------------------
// on_source_committed / dirty propagation
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, DirtyPropagation_SourceChanged) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  pj::NodeId n = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());

  // Run schedule to clear dirty flag
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  // Append more data and notify
  append_linear_rows(engine, src, 1.0, 5, 5);
  notify(derived, {src});

  // Node must be dirty again — schedule should produce more rows
  auto before = collect_values(engine, derived.output_topics(n)[0]).size();
  ASSERT_TRUE(derived.schedule().has_value());
  auto after = collect_values(engine, derived.output_topics(n)[0]).size();
  EXPECT_GT(after, before);
}

TEST(DerivedEngineTest, DirtyPropagation_Chain) {
  // A → B: committing source dirtifies A; schedule runs A and dirtifies B;
  // subsequent schedule runs B.
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  pj::NodeId a = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  pj::TopicId a_out = derived.output_topics(a)[0];
  pj::NodeId b = *derived.add_siso_transform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  // Both A and B should have been processed
  EXPECT_FALSE(collect_values(engine, derived.output_topics(a)[0]).empty());
  EXPECT_FALSE(collect_values(engine, derived.output_topics(b)[0]).empty());
}

// ---------------------------------------------------------------------------
// schedule (incremental)
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, Schedule_ProducesCorrectDerivative) {
  // slope=2.0, step=1s, 11 rows → derivative is always 2.0 (10 rows output)
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 2.0, 11);
  pj::NodeId node = *derived.add_siso_transform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto vals = collect_values(engine, derived.output_topics(node)[0]);
  ASSERT_EQ(vals.size(), 10u);
  for (double v : vals) {
    EXPECT_NEAR(v, 2.0, 1e-6);
  }
}

TEST(DerivedEngineTest, Schedule_SecondCallNoNewChunks_NoOp) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  pj::NodeId node = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto count1 = collect_values(engine, derived.output_topics(node)[0]).size();

  // No new data — second schedule should not change output count
  ASSERT_TRUE(derived.schedule().has_value());
  auto count2 = collect_values(engine, derived.output_topics(node)[0]).size();
  EXPECT_EQ(count1, count2);
}

TEST(DerivedEngineTest, Schedule_Lazy_SkipsInactiveNode) {
  // Two independent source nodes. schedule({a}) should not run b.
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src1 = make_linear_topic(engine, ds, 1.0, 5);
  pj::TopicId src2 = make_linear_topic(engine, ds, 2.0, 5);

  pj::NodeId a = *derived.add_siso_transform(src1, "da", ds, std::make_unique<DerivativeTransform>());
  pj::NodeId b = *derived.add_siso_transform(src2, "db", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src1, src2});
  // Only process node A
  ASSERT_TRUE(derived.schedule({a}).has_value());

  auto a_vals = collect_values(engine, derived.output_topics(a)[0]);
  auto b_vals = collect_values(engine, derived.output_topics(b)[0]);

  EXPECT_FALSE(a_vals.empty());  // A was processed
  EXPECT_TRUE(b_vals.empty());   // B was skipped
}

TEST(DerivedEngineTest, Schedule_Chain_BothNodesRun) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 12);
  pj::NodeId a = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  pj::TopicId a_out = derived.output_topics(a)[0];
  pj::NodeId b = *derived.add_siso_transform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  // A: 11 derivative rows of linear → all constant
  auto a_vals = collect_values(engine, derived.output_topics(a)[0]);
  EXPECT_EQ(a_vals.size(), 11u);

  // B: derivative of constant → all zero (10 rows, first suppressed)
  auto b_vals = collect_values(engine, derived.output_topics(b)[0]);
  EXPECT_EQ(b_vals.size(), 10u);
  for (double v : b_vals) {
    EXPECT_NEAR(v, 0.0, 1e-6);
  }
}

// ---------------------------------------------------------------------------
// recompute_batch
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, RecomputeBatch_ClearsAndRegenerates) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 1.0, 6);
  pj::NodeId node = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto before = collect_values(engine, derived.output_topics(node)[0]);
  ASSERT_FALSE(before.empty());

  // recompute_batch clears output and replays from scratch
  ASSERT_TRUE(derived.recompute_batch(node).has_value());

  auto after = collect_values(engine, derived.output_topics(node)[0]);
  EXPECT_EQ(before.size(), after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_NEAR(before[i], after[i], 1e-9);
  }
}

// ---------------------------------------------------------------------------
// Parity: incremental == batch
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, Parity_SingleChunk) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 3.0, 11);
  pj::NodeId node = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());
  auto incremental = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

TEST(DerivedEngineTest, Parity_TwoChunks_CrossBoundary) {
  // The cross-chunk boundary row is the key correctness test: state must carry
  // over naturally in the incremental path.
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  // Chunk 1: 20 rows (forces auto-chunk at 1024 capacity — but step_ns large enough
  // that all rows stay in one chunk unless we push more)
  pj::TopicId src = make_linear_topic(engine, ds, 2.0, 20);
  pj::NodeId node = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  // Chunk 2: append 20 more rows
  append_linear_rows(engine, src, 2.0, 20, 20);
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto incremental = collect_values(engine, derived.output_topics(node)[0]);

  // Batch recompute and compare
  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

TEST(DerivedEngineTest, Parity_ThreeChunks) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  pj::TopicId src = make_linear_topic(engine, ds, 5.0, 10);
  pj::NodeId node = *derived.add_siso_transform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  append_linear_rows(engine, src, 5.0, 10, 10);
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  append_linear_rows(engine, src, 5.0, 10, 20);
  notify(derived, {src});
  ASSERT_TRUE(derived.schedule().has_value());

  auto incremental = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.output_topics(node)[0]);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

// commit_chunks returns deduplicated changed topic IDs usable directly with
// on_source_committed — the streaming frame-loop pattern.
TEST(DerivedEngineTest, CommitCycle_ReturnValueDrivesNotify) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  DataWriter writer = engine.create_writer();
  auto handle = *writer.register_scalar_series(ds, "sig", pj::NumericType::kFloat64);
  pj::TopicId src = handle.topic_id;

  pj::NodeId node = *derived.add_siso_transform(src, "d_sig", ds, std::make_unique<DerivativeTransform>());
  pj::TopicId out = derived.output_topics(node)[0];

  // Frame 1: write 5 samples and use the one-liner pattern.
  for (int i = 0; i < 5; ++i) {
    writer.append_scalar(handle, static_cast<pj::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  derived.on_source_committed(engine.commit_chunks(writer.flush_all()));
  ASSERT_TRUE(derived.schedule().has_value());
  auto after_frame1 = collect_values(engine, out).size();
  EXPECT_GT(after_frame1, 0u);

  // Frame 2: write 5 more samples.
  for (int i = 5; i < 10; ++i) {
    writer.append_scalar(handle, static_cast<pj::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  derived.on_source_committed(engine.commit_chunks(writer.flush_all()));
  ASSERT_TRUE(derived.schedule().has_value());
  auto after_frame2 = collect_values(engine, out).size();
  EXPECT_GT(after_frame2, after_frame1);

  // Verify return value: single topic flushed → exactly one ID returned.
  for (int i = 10; i < 13; ++i) {
    writer.append_scalar(handle, static_cast<pj::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  auto changed = engine.commit_chunks(writer.flush_all());
  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(changed[0], src);
}

// Regression: add_siso_transform must work on a series created via
// register_scalar_series even when no chunk has been committed yet
// (fewer rows than max_chunk_rows, or no flush/commit called at all).
TEST(DerivedEngineTest, AddTransform_NoCommittedChunks_Succeeds) {
  DataEngine engine;
  DerivedEngine derived(engine);
  pj::DatasetId ds = make_dataset(engine);

  // Create a topic with a few rows but do NOT commit any chunks.
  DataWriter writer = engine.create_writer();
  auto handle = *writer.register_scalar_series(ds, "tiny_series", pj::NumericType::kFloat64);
  pj::TopicId src = handle.topic_id;

  // Write 3 rows (well below default max_chunk_rows=1024) — no commit.
  for (int i = 0; i < 3; ++i) {
    writer.append_scalar(handle, static_cast<pj::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  // Deliberately skip flush / commit_chunks.

  // add_siso_transform must succeed without any committed chunks in storage.
  auto result = derived.add_siso_transform(src, "d_tiny", ds, std::make_unique<DerivativeTransform>());
  EXPECT_TRUE(result.has_value()) << "Expected success, got: " << (result.has_value() ? "" : result.error());
}

}  // namespace
}  // namespace pj::engine
