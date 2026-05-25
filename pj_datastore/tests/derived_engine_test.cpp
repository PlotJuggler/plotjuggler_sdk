// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/derived_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "pj_base/dataset.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/builtin_transforms.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/topic_storage.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Create a dataset and return its id.
static PJ::DatasetId make_dataset(DataEngine& engine, const std::string& name = "test") {
  auto id_or = engine.createDataset(PJ::DatasetDescriptor{.source_name = name, .time_domain_id = 0});
  return *id_or;
}

// Write `n` rows to a float64 scalar topic with value = slope * (t_ns / 1e9).
// Commits the chunk. Returns the TopicId.
// Timestamps: 0, step_ns, 2*step_ns, ...
static PJ::TopicId make_linear_topic(
    DataEngine& engine, PJ::DatasetId dataset_id, double slope, int n, PJ::Timestamp step_ns = 1'000'000'000LL) {
  DataWriter writer = engine.createWriter();
  auto handle_or = writer.registerScalarSeries(dataset_id, "src", PJ::NumericType::kFloat64);
  PJ::TopicId tid = handle_or->topic_id;
  for (int i = 0; i < n; ++i) {
    PJ::Timestamp ts = static_cast<PJ::Timestamp>(i) * step_ns;
    double v = slope * (static_cast<double>(i) * static_cast<double>(step_ns) * 1e-9);
    writer.appendScalar(*handle_or, ts, v);
  }
  // commit_chunks returns the changed topic IDs — pass directly to on_source_committed.
  engine.commitChunks(writer.flushAll());
  return tid;
}

// Append `n` more rows to an existing scalar topic (continuing timestamps from start_i).
static void append_linear_rows(
    DataEngine& engine, PJ::TopicId src_topic_id, double slope, int n, int start_i,
    PJ::Timestamp step_ns = 1'000'000'000LL) {
  // We need to write to an existing topic via begin_row / set_float64 / finish_row.
  DataWriter writer = engine.createWriter();
  for (int i = start_i; i < start_i + n; ++i) {
    PJ::Timestamp ts = static_cast<PJ::Timestamp>(i) * step_ns;
    double v = slope * (static_cast<double>(i) * static_cast<double>(step_ns) * 1e-9);
    auto s = writer.beginRow(src_topic_id, ts);
    (void)s;
    writer.set(src_topic_id, 0, v);
    auto s2 = writer.finishRow(src_topic_id);
    (void)s2;
  }
  engine.commitChunks(writer.flushAll());
}

// Collect all float64 values from a topic in timestamp order.
static std::vector<double> collect_values(DataEngine& engine, PJ::TopicId topic_id) {
  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  if (!storage) {
    return {};
  }
  std::vector<double> out;
  auto cursor = rangeQuery(storage->sealedChunks(), 0, std::numeric_limits<PJ::Timestamp>::max());
  cursor.forEach([&](const SampleRow& row) { out.push_back(row.chunk->readNumericAsDouble(0, row.row_index)); });
  return out;
}

// Wrapper: on_source_committed from an initializer list (std::span can't take {}).
static void notify(DerivedEngine& derived, std::initializer_list<PJ::TopicId> topics) {
  std::vector<PJ::TopicId> v(topics);
  derived.onSourceCommitted(v);
}

// Collect (timestamp, value) pairs.
static std::vector<std::pair<PJ::Timestamp, double>> collect_rows(DataEngine& engine, PJ::TopicId topic_id) {
  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  if (!storage) {
    return {};
  }
  std::vector<std::pair<PJ::Timestamp, double>> out;
  auto cursor = rangeQuery(storage->sealedChunks(), 0, std::numeric_limits<PJ::Timestamp>::max());
  cursor.forEach(
      [&](const SampleRow& row) { out.emplace_back(row.timestamp, row.chunk->readNumericAsDouble(0, row.row_index)); });
  return out;
}

// ---------------------------------------------------------------------------
// DerivativeTransform — unit tests (no engine needed)
// ---------------------------------------------------------------------------

TEST(DerivativeTransformTest, SkipsFirstRow) {
  DerivativeTransform op;
  PJ::Timestamp t = 0;
  VarValue v = 0.0;
  EXPECT_FALSE(op.calculate(0, VarValue{0.0}, t, v));
}

TEST(DerivativeTransformTest, CorrectDerivative_ConstantRate) {
  DerivativeTransform op;
  PJ::Timestamp out_t;
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
  PJ::Timestamp out_t;
  VarValue out_v = 0.0;
  EXPECT_FALSE(op.calculate(0, VarValue{1.0}, out_t, out_v));
  EXPECT_TRUE(op.calculate(0, VarValue{2.0}, out_t, out_v));
  EXPECT_EQ(std::get<double>(out_v), 0.0);
}

TEST(DerivativeTransformTest, Reset_ClearsState) {
  DerivativeTransform op;
  PJ::Timestamp out_t;
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
  EXPECT_EQ(op.outputKind(StorageKind::kFloat64), StorageKind::kFloat64);
  EXPECT_EQ(op.outputKind(StorageKind::kFloat32), StorageKind::kFloat64);
  EXPECT_EQ(op.outputKind(StorageKind::kInt64), StorageKind::kFloat64);
}

// ---------------------------------------------------------------------------
// add_siso_transform
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, AddTransform_CreatesOutputTopic) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  auto node_or = derived.addSisoTransform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::NodeId node = *node_or;
  EXPECT_TRUE(derived.hasNode(node));

  auto out_topics = derived.outputTopics(node);
  ASSERT_EQ(out_topics.size(), 1u);
  EXPECT_NE(engine.getTopicStorage(out_topics[0]), nullptr);
}

TEST(DerivedEngineTest, AddTransform_DuplicateOutputName_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  ASSERT_TRUE(derived.addSisoTransform(src, "deriv", ds, std::make_unique<DerivativeTransform>()).has_value());
  // Same output name → should fail
  auto r = derived.addSisoTransform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  EXPECT_FALSE(r.has_value());
}

TEST(DerivedEngineTest, AddTransform_UnknownInputTopic_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  auto r = derived.addSisoTransform(9999u, "deriv", ds, std::make_unique<DerivativeTransform>());
  EXPECT_FALSE(r.has_value());
}

// ---------------------------------------------------------------------------
// topological_order
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, TopologicalOrder_SingleNode) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  PJ::NodeId n = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  auto order = derived.topologicalOrder();
  ASSERT_EQ(order.size(), 1u);
  EXPECT_EQ(order[0], n);
}

TEST(DerivedEngineTest, TopologicalOrder_Chain_ABOrder) {
  // A → B: output of A is input of B. Order must be [A, B].
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  PJ::NodeId a = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId a_out = derived.outputTopics(a)[0];
  PJ::NodeId b = *derived.addSisoTransform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  auto order = derived.topologicalOrder();
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], a);
  EXPECT_EQ(order[1], b);
}

TEST(DerivedEngineTest, TopologicalOrder_Fork) {
  // A → B and A → C: A must appear before both B and C.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  PJ::NodeId a = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId a_out = derived.outputTopics(a)[0];
  PJ::NodeId b = *derived.addSisoTransform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());
  PJ::NodeId c = *derived.addSisoTransform(a_out, "d3", ds, std::make_unique<DerivativeTransform>());

  auto order = derived.topologicalOrder();
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
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  PJ::NodeId n = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());

  // Run schedule to clear dirty flag
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // Append more data and notify
  append_linear_rows(engine, src, 1.0, 5, 5);
  notify(derived, {src});

  // Node must be dirty again — schedule should produce more rows
  auto before = collect_values(engine, derived.outputTopics(n)[0]).size();
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto after = collect_values(engine, derived.outputTopics(n)[0]).size();
  EXPECT_GT(after, before);
}

TEST(DerivedEngineTest, DirtyPropagation_Chain) {
  // A → B: committing source dirtifies A; schedule runs A and dirtifies B;
  // subsequent schedule runs B.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 10);
  PJ::NodeId a = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId a_out = derived.outputTopics(a)[0];
  PJ::NodeId b = *derived.addSisoTransform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // Both A and B should have been processed
  EXPECT_FALSE(collect_values(engine, derived.outputTopics(a)[0]).empty());
  EXPECT_FALSE(collect_values(engine, derived.outputTopics(b)[0]).empty());
}

// ---------------------------------------------------------------------------
// schedule (incremental)
// ---------------------------------------------------------------------------

TEST(DerivedEngineTest, Schedule_ProducesCorrectDerivative) {
  // slope=2.0, step=1s, 11 rows → derivative is always 2.0 (10 rows output)
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 2.0, 11);
  PJ::NodeId node = *derived.addSisoTransform(src, "deriv", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto vals = collect_values(engine, derived.outputTopics(node)[0]);
  ASSERT_EQ(vals.size(), 10u);
  for (double v : vals) {
    EXPECT_NEAR(v, 2.0, 1e-6);
  }
}

TEST(DerivedEngineTest, Schedule_SecondCallNoNewChunks_NoOp) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  PJ::NodeId node = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto count1 = collect_values(engine, derived.outputTopics(node)[0]).size();

  // No new data — second schedule should not change output count
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto count2 = collect_values(engine, derived.outputTopics(node)[0]).size();
  EXPECT_EQ(count1, count2);
}

TEST(DerivedEngineTest, Schedule_Lazy_SkipsInactiveNode) {
  // Two independent source nodes. schedule({a}) should not run b.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src1 = make_linear_topic(engine, ds, 1.0, 5);
  PJ::TopicId src2 = make_linear_topic(engine, ds, 2.0, 5);

  PJ::NodeId a = *derived.addSisoTransform(src1, "da", ds, std::make_unique<DerivativeTransform>());
  PJ::NodeId b = *derived.addSisoTransform(src2, "db", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src1, src2});
  // Only process node A
  ASSERT_TRUE(derived.scheduleActive({a}).has_value());

  auto a_vals = collect_values(engine, derived.outputTopics(a)[0]);
  auto b_vals = collect_values(engine, derived.outputTopics(b)[0]);

  EXPECT_FALSE(a_vals.empty());  // A was processed
  EXPECT_TRUE(b_vals.empty());   // B was skipped
}

TEST(DerivedEngineTest, Schedule_Chain_BothNodesRun) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 12);
  PJ::NodeId a = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId a_out = derived.outputTopics(a)[0];
  PJ::NodeId b = *derived.addSisoTransform(a_out, "d2", ds, std::make_unique<DerivativeTransform>());

  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // A: 11 derivative rows of linear → all constant
  auto a_vals = collect_values(engine, derived.outputTopics(a)[0]);
  EXPECT_EQ(a_vals.size(), 11u);

  // B: derivative of constant → all zero (10 rows, first suppressed)
  auto b_vals = collect_values(engine, derived.outputTopics(b)[0]);
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
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 6);
  PJ::NodeId node = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto before = collect_values(engine, derived.outputTopics(node)[0]);
  ASSERT_FALSE(before.empty());

  // recompute_batch clears output and replays from scratch
  ASSERT_TRUE(derived.recompute_batch(node).has_value());

  auto after = collect_values(engine, derived.outputTopics(node)[0]);
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
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 3.0, 11);
  PJ::NodeId node = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto incremental = collect_values(engine, derived.outputTopics(node)[0]);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.outputTopics(node)[0]);

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
  PJ::DatasetId ds = make_dataset(engine);

  // Chunk 1: 20 rows (forces auto-chunk at 1024 capacity — but step_ns large enough
  // that all rows stay in one chunk unless we push more)
  PJ::TopicId src = make_linear_topic(engine, ds, 2.0, 20);
  PJ::NodeId node = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // Chunk 2: append 20 more rows
  append_linear_rows(engine, src, 2.0, 20, 20);
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto incremental = collect_values(engine, derived.outputTopics(node)[0]);

  // Batch recompute and compare
  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.outputTopics(node)[0]);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

TEST(DerivedEngineTest, Parity_ThreeChunks) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 5.0, 10);
  PJ::NodeId node = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  append_linear_rows(engine, src, 5.0, 10, 10);
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  append_linear_rows(engine, src, 5.0, 10, 20);
  notify(derived, {src});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto incremental = collect_values(engine, derived.outputTopics(node)[0]);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, derived.outputTopics(node)[0]);

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
  PJ::DatasetId ds = make_dataset(engine);

  DataWriter writer = engine.createWriter();
  auto handle = *writer.registerScalarSeries(ds, "sig", PJ::NumericType::kFloat64);
  PJ::TopicId src = handle.topic_id;

  PJ::NodeId node = *derived.addSisoTransform(src, "d_sig", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId out = derived.outputTopics(node)[0];

  // Frame 1: write 5 samples and use the one-liner pattern.
  for (int i = 0; i < 5; ++i) {
    writer.appendScalar(handle, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  derived.onSourceCommitted(engine.commitChunks(writer.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto after_frame1 = collect_values(engine, out).size();
  EXPECT_GT(after_frame1, 0u);

  // Frame 2: write 5 more samples.
  for (int i = 5; i < 10; ++i) {
    writer.appendScalar(handle, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  derived.onSourceCommitted(engine.commitChunks(writer.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto after_frame2 = collect_values(engine, out).size();
  EXPECT_GT(after_frame2, after_frame1);

  // Verify return value: single topic flushed → exactly one ID returned.
  for (int i = 10; i < 13; ++i) {
    writer.appendScalar(handle, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  auto changed = engine.commitChunks(writer.flushAll());
  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(changed[0], src);
}

// Regression: add_siso_transform must work on a series created via
// register_scalar_series even when no chunk has been committed yet
// (fewer rows than max_chunk_rows, or no flush/commit called at all).
TEST(DerivedEngineTest, AddTransform_NoCommittedChunks_Succeeds) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  // Create a topic with a few rows but do NOT commit any chunks.
  DataWriter writer = engine.createWriter();
  auto handle = *writer.registerScalarSeries(ds, "tiny_series", PJ::NumericType::kFloat64);
  PJ::TopicId src = handle.topic_id;

  // Write 3 rows (well below default max_chunk_rows=1024) — no commit.
  for (int i = 0; i < 3; ++i) {
    writer.appendScalar(handle, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  // Deliberately skip flush / commit_chunks.

  // add_siso_transform must succeed without any committed chunks in storage.
  auto result = derived.addSisoTransform(src, "d_tiny", ds, std::make_unique<DerivativeTransform>());
  EXPECT_TRUE(result.has_value()) << "Expected success, got: " << (result.has_value() ? "" : result.error());
}

// ---------------------------------------------------------------------------
// MIMO transform helpers
// ---------------------------------------------------------------------------

// SumMimoTransform: N inputs → 1 output (sum of all inputs as double).
class SumMimoTransform : public IMIMOTransform {
 public:
  std::vector<StorageKind> outputKinds(PJ::Span<const StorageKind> /*input_kinds*/) const override {
    return {StorageKind::kFloat64};
  }

  bool calculate(
      PJ::Timestamp time, PJ::Span<const VarValue> inputs, PJ::Timestamp& out_time,
      std::vector<VarValue>& output) override {
    out_time = time;
    double sum = 0.0;
    for (const auto& v : inputs) {
      sum += std::get<double>(v);
    }
    output[0] = sum;
    return true;
  }
};

// DiffMimoTransform: 2 inputs → 1 output (inputs[0] - inputs[1]).
class DiffMimoTransform : public IMIMOTransform {
 public:
  std::vector<StorageKind> outputKinds(PJ::Span<const StorageKind> /*input_kinds*/) const override {
    return {StorageKind::kFloat64};
  }

  bool calculate(
      PJ::Timestamp time, PJ::Span<const VarValue> inputs, PJ::Timestamp& out_time,
      std::vector<VarValue>& output) override {
    out_time = time;
    output[0] = std::get<double>(inputs[0]) - std::get<double>(inputs[1]);
    return true;
  }
};

// Collect (timestamp, value) pairs for a given column index.
static std::vector<std::pair<PJ::Timestamp, double>> collect_rows_col(
    DataEngine& engine, PJ::TopicId topic_id, std::size_t col = 0) {
  const TopicStorage* storage = engine.getTopicStorage(topic_id);
  if (!storage) {
    return {};
  }
  std::vector<std::pair<PJ::Timestamp, double>> out;
  auto cursor = rangeQuery(storage->sealedChunks(), 0, std::numeric_limits<PJ::Timestamp>::max());
  cursor.forEach([&](const SampleRow& row) {
    out.emplace_back(row.timestamp, row.chunk->readNumericAsDouble(col, row.row_index));
  });
  return out;
}

// ---------------------------------------------------------------------------
// MIMO — add_mimo_transform: basic registration
// ---------------------------------------------------------------------------

TEST(MimoTransformTest, AddMimo_CreatesOutputTopic) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 5);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 5);

  auto node_or = derived.addMimoTransform({t1, t2}, {"sum_out"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();

  PJ::NodeId node = *node_or;
  EXPECT_TRUE(derived.hasNode(node));
  auto outs = derived.outputTopics(node);
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_NE(engine.getTopicStorage(outs[0]), nullptr);
}

TEST(MimoTransformTest, AddMimo_UnknownInputTopic_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  auto r = derived.addMimoTransform({9999u}, {"out"}, ds, std::make_unique<SumMimoTransform>());
  EXPECT_FALSE(r.has_value());
}

TEST(MimoTransformTest, AddMimo_DuplicateOutputName_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 5);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 5);

  ASSERT_TRUE(derived.addMimoTransform({t1, t2}, {"dup"}, ds, std::make_unique<SumMimoTransform>()).has_value());
  // Same output name in same dataset must fail.
  auto r = derived.addMimoTransform({t1, t2}, {"dup"}, ds, std::make_unique<SumMimoTransform>());
  EXPECT_FALSE(r.has_value());
}

TEST(MimoTransformTest, AddMimo_MultipleOutputTopics) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 5);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 5);

  // DiffMimoTransform produces 1 output; use two separate nodes for two outputs.
  auto node_or = derived.addMimoTransform({t1, t2}, {"diff_out"}, ds, std::make_unique<DiffMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  ASSERT_EQ(derived.outputTopics(*node_or).size(), 1u);
}

// ---------------------------------------------------------------------------
// MIMO — join semantics (inner join on exact timestamps)
// ---------------------------------------------------------------------------

TEST(MimoTransformTest, JoinSemantics_OnlyMatchingTimestamps) {
  // Topic A: t=0,1,2,3,4 s (all 5 timestamps)
  // Topic B: t=0,2,4 s     (3 timestamps, a subset)
  // Sum at t=0: 0+0=0, t=2: 2+4=6, t=4: 4+8=12
  // t=1 and t=3 are in A but not B → no output row.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  // Topic A: t = 0,1,2,3,4 s, value = i seconds
  PJ::TopicId ta;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "a", PJ::NumericType::kFloat64);
    ta = h.topic_id;
    for (int i = 0; i < 5; ++i) {
      w.appendScalar(h, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
    }
    engine.commitChunks(w.flushAll());
  }

  // Topic B: t = 0,2,4 s, value = 0,4,8
  PJ::TopicId tb;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "b", PJ::NumericType::kFloat64);
    tb = h.topic_id;
    for (int i = 0; i < 3; ++i) {
      w.appendScalar(h, static_cast<PJ::Timestamp>(i * 2) * 1'000'000'000LL, static_cast<double>(i * 2 * 2));
    }
    engine.commitChunks(w.flushAll());
  }

  auto node_or = derived.addMimoTransform({ta, tb}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();

  notify(derived, {ta, tb});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto rows = collect_rows_col(engine, derived.outputTopics(*node_or)[0]);
  ASSERT_EQ(rows.size(), 3u);               // only t=0,2,4 produce output
  EXPECT_NEAR(rows[0].second, 0.0, 1e-9);   // 0+0
  EXPECT_NEAR(rows[1].second, 6.0, 1e-9);   // 2+4
  EXPECT_NEAR(rows[2].second, 12.0, 1e-9);  // 4+8
}

TEST(MimoTransformTest, JoinSemantics_NoCommonTimestamps_NoOutput) {
  // A: t=0,1 s;  B: t=2,3 s → no overlap → no output rows
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId ta;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "a2", PJ::NumericType::kFloat64);
    ta = h.topic_id;
    for (int i = 0; i < 2; ++i) {
      w.appendScalar(h, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
    }
    engine.commitChunks(w.flushAll());
  }
  PJ::TopicId tb;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "b2", PJ::NumericType::kFloat64);
    tb = h.topic_id;
    for (int i = 2; i < 4; ++i) {
      w.appendScalar(h, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
    }
    engine.commitChunks(w.flushAll());
  }

  auto node_or = derived.addMimoTransform({ta, tb}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  notify(derived, {ta, tb});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  EXPECT_TRUE(collect_values(engine, derived.outputTopics(*node_or)[0]).empty());
}

// ---------------------------------------------------------------------------
// MIMO — schedule (incremental)
// ---------------------------------------------------------------------------

TEST(MimoTransformTest, Schedule_ProducesCorrectSum) {
  // Two topics with the same timestamps (0,1,2,...,9 seconds).
  // A[i] = 1.0 * i, B[i] = 2.0 * i. Sum[i] = 3.0 * i.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 10);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 10);

  auto node_or = derived.addMimoTransform({t1, t2}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::TopicId out = derived.outputTopics(*node_or)[0];

  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto rows = collect_rows_col(engine, out);
  ASSERT_EQ(rows.size(), 10u);
  for (int i = 0; i < 10; ++i) {
    double expected = 3.0 * static_cast<double>(i);  // (1.0 + 2.0) * i
    EXPECT_NEAR(rows[i].second, expected, 1e-9) << "row " << i;
  }
}

TEST(MimoTransformTest, Schedule_IncrementalTwoChunks) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 10);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 10);

  auto node_or = derived.addMimoTransform({t1, t2}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::TopicId out = derived.outputTopics(*node_or)[0];

  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());
  std::size_t after_first = collect_values(engine, out).size();
  EXPECT_EQ(after_first, 10u);

  // Second batch of data
  append_linear_rows(engine, t1, 1.0, 10, 10);
  append_linear_rows(engine, t2, 2.0, 10, 10);
  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  std::size_t after_second = collect_values(engine, out).size();
  EXPECT_EQ(after_second, 20u);
}

// ---------------------------------------------------------------------------
// MIMO — recompute_batch + parity
// ---------------------------------------------------------------------------

TEST(MimoTransformTest, Parity_IncrementalMatchesBatch_SingleChunk) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 10);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 3.0, 10);

  auto node_or = derived.addMimoTransform({t1, t2}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::NodeId node = *node_or;
  PJ::TopicId out = derived.outputTopics(node)[0];

  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto incremental = collect_values(engine, out);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, out);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

TEST(MimoTransformTest, Parity_IncrementalMatchesBatch_MultipleChunks) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 10);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 2.0, 10);

  auto node_or = derived.addMimoTransform({t1, t2}, {"sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::NodeId node = *node_or;
  PJ::TopicId out = derived.outputTopics(node)[0];

  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  append_linear_rows(engine, t1, 1.0, 10, 10);
  append_linear_rows(engine, t2, 2.0, 10, 10);
  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto incremental = collect_values(engine, out);

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, out);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

// ---------------------------------------------------------------------------
// MIMO — chained with SISO
// ---------------------------------------------------------------------------

TEST(MimoTransformTest, ChainedSisoThenMimo) {
  // Compute derivative of two source series (SISO), then sum the derivatives (MIMO).
  // src1: y = 2*t → dy/dt = 2.0
  // src2: y = 3*t → dy/dt = 3.0
  // MIMO sum = 5.0 for each output row.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src1 = make_linear_topic(engine, ds, 2.0, 11);
  PJ::TopicId src2 = make_linear_topic(engine, ds, 3.0, 11);

  PJ::NodeId n1 = *derived.addSisoTransform(src1, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::NodeId n2 = *derived.addSisoTransform(src2, "d2", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId d1_out = derived.outputTopics(n1)[0];
  PJ::TopicId d2_out = derived.outputTopics(n2)[0];

  auto node_or = derived.addMimoTransform({d1_out, d2_out}, {"sum_deriv"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::TopicId sum_out = derived.outputTopics(*node_or)[0];

  notify(derived, {src1, src2});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto vals = collect_values(engine, sum_out);
  EXPECT_FALSE(vals.empty());
  for (double v : vals) {
    EXPECT_NEAR(v, 5.0, 1e-6);
  }
}

// ---------------------------------------------------------------------------
// MIMO — duplicate timestamp bug (C1)
// ---------------------------------------------------------------------------

// When topic A has two rows at t=5 (equal timestamps are permitted by the
// engine), the N-way join must produce exactly one output row at t=5 using
// consistent (last-write-wins) values — NOT two output rows at t=5 (duplicate).
TEST(MimoTransformTest, DuplicateTimestamp_ProducesOneOutputRow) {
  // Topic A: t=0,5,5,10 ns — t=5 appears twice (values 1.0 and 2.0)
  // Topic B: t=0,5,10 ns   — normal, no duplicates
  // Expected join at t=0,5,10 → 3 output rows.
  // Bug: without dedup, joined_ts=[0,5,5,10] → 4 output rows, t=5 appears twice.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId ta;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "a_dup", PJ::NumericType::kFloat64);
    ta = h.topic_id;
    w.appendScalar(h, 0LL, 0.0);
    w.appendScalar(h, 5LL, 1.0);  // first row at t=5
    w.appendScalar(h, 5LL, 2.0);  // second row at t=5 (equal timestamp allowed)
    w.appendScalar(h, 10LL, 3.0);
    engine.commitChunks(w.flushAll());
  }

  PJ::TopicId tb;
  {
    DataWriter w = engine.createWriter();
    auto h = *w.registerScalarSeries(ds, "b_dup", PJ::NumericType::kFloat64);
    tb = h.topic_id;
    w.appendScalar(h, 0LL, 10.0);
    w.appendScalar(h, 5LL, 20.0);
    w.appendScalar(h, 10LL, 30.0);
    engine.commitChunks(w.flushAll());
  }

  auto node_or = derived.addMimoTransform({ta, tb}, {"sum_dup"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();

  notify(derived, {ta, tb});
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto rows = collect_rows_col(engine, derived.outputTopics(*node_or)[0]);
  // Must produce exactly 3 rows (t=0, t=5, t=10), not 4.
  ASSERT_EQ(rows.size(), 3u) << "Duplicate timestamp in input caused duplicate output rows";
  EXPECT_NEAR(rows[0].second, 10.0, 1e-9);  // 0+10
  // t=5: last-write-wins for A → 2.0; B=20.0 → sum=22.0
  EXPECT_NEAR(rows[1].second, 22.0, 1e-9);
  EXPECT_NEAR(rows[2].second, 33.0, 1e-9);  // 3+30
}

// ---------------------------------------------------------------------------
// MIMO — staggered cross-chunk parity (I1)
// ---------------------------------------------------------------------------

// Parity with staggered inputs: A advances ahead of B across chunk boundaries.
// This tests the watermark's correctness when topic rates differ.
TEST(MimoTransformTest, Parity_StaggeredChunks_IncrementalMatchesBatch) {
  // Chunk 1: A commits t=0..9 (10 rows), B commits t=0,2,4,6,8 (5 rows, even only)
  // Chunk 2: A commits t=10..14 (5 rows), B commits t=10,12,14 (3 rows, even only)
  // Expected joins: t=0,2,4,6,8,10,12,14 (8 rows)
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  // Register both topics upfront
  DataWriter wa = engine.createWriter();
  auto ha = *wa.registerScalarSeries(ds, "stag_a", PJ::NumericType::kFloat64);
  PJ::TopicId ta = ha.topic_id;

  DataWriter wb = engine.createWriter();
  auto hb = *wb.registerScalarSeries(ds, "stag_b", PJ::NumericType::kFloat64);
  PJ::TopicId tb = hb.topic_id;

  auto node_or = derived.addMimoTransform({ta, tb}, {"stag_sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::NodeId node = *node_or;
  PJ::TopicId out = derived.outputTopics(node)[0];

  // Chunk 1
  for (int i = 0; i < 10; ++i) {
    wa.appendScalar(ha, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  for (int i = 0; i < 5; ++i) {
    wb.appendScalar(hb, static_cast<PJ::Timestamp>(i * 2) * 1'000'000'000LL, static_cast<double>(i * 2));
  }
  derived.onSourceCommitted(engine.commitChunks(wa.flushAll()));
  derived.onSourceCommitted(engine.commitChunks(wb.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // Chunk 2
  for (int i = 10; i < 15; ++i) {
    wa.appendScalar(ha, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  for (int i = 5; i < 8; ++i) {
    wb.appendScalar(hb, static_cast<PJ::Timestamp>(i * 2) * 1'000'000'000LL, static_cast<double>(i * 2));
  }
  derived.onSourceCommitted(engine.commitChunks(wa.flushAll()));
  derived.onSourceCommitted(engine.commitChunks(wb.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());

  auto incremental = collect_rows_col(engine, out);
  ASSERT_EQ(incremental.size(), 8u) << "Expected 8 joined rows (even timestamps 0..14)";

  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_rows_col(engine, out);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_EQ(incremental[i].first, batch[i].first) << "timestamp mismatch at row " << i;
    EXPECT_NEAR(incremental[i].second, batch[i].second, 1e-9) << "value mismatch at row " << i;
  }
}

// ---------------------------------------------------------------------------
// MIMO — stateful transform reset (I2)
// ---------------------------------------------------------------------------

// A stateful MIMO transform that accumulates a running sum across calls.
// recompute_batch must call reset() and produce the same result as incremental.
class AccumulatingSumMimoTransform : public IMIMOTransform {
 public:
  std::vector<StorageKind> outputKinds(PJ::Span<const StorageKind> /*input_kinds*/) const override {
    return {StorageKind::kFloat64};
  }

  void reset() override {
    running_sum_ = 0.0;
  }

  bool calculate(
      PJ::Timestamp time, PJ::Span<const VarValue> inputs, PJ::Timestamp& out_time,
      std::vector<VarValue>& output) override {
    out_time = time;
    running_sum_ += std::get<double>(inputs[0]) + std::get<double>(inputs[1]);
    output[0] = running_sum_;
    return true;
  }

 private:
  double running_sum_ = 0.0;
};

TEST(MimoTransformTest, StatefulTransform_RecomputeBatchCallsReset) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 5);
  PJ::TopicId t2 = make_linear_topic(engine, ds, 1.0, 5);

  auto node_or = derived.addMimoTransform({t1, t2}, {"acc"}, ds, std::make_unique<AccumulatingSumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::NodeId node = *node_or;
  PJ::TopicId out = derived.outputTopics(node)[0];

  notify(derived, {t1, t2});
  ASSERT_TRUE(derived.scheduleAll().has_value());
  auto incremental = collect_values(engine, out);

  // recompute_batch must reset the transform and produce identical output
  ASSERT_TRUE(derived.recompute_batch(node).has_value());
  auto batch = collect_values(engine, out);

  ASSERT_EQ(incremental.size(), batch.size());
  for (std::size_t i = 0; i < batch.size(); ++i) {
    EXPECT_NEAR(incremental[i], batch[i], 1e-9) << "mismatch at row " << i;
  }
}

// ---------------------------------------------------------------------------
// MIMO — on_source_committed with partial inputs (I4)
// ---------------------------------------------------------------------------

// Notifying about only one of two MIMO inputs should cause the node to run
// but find no new data from the other input and produce no output.
// The watermark must NOT advance, so future data from the other input can match.
TEST(MimoTransformTest, PartialNotify_DoesNotAdvanceWatermark) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  // Register both topics; initially only commit data for t1
  DataWriter w1 = engine.createWriter();
  auto h1 = *w1.registerScalarSeries(ds, "p1", PJ::NumericType::kFloat64);
  PJ::TopicId t1 = h1.topic_id;

  DataWriter w2 = engine.createWriter();
  auto h2 = *w2.registerScalarSeries(ds, "p2", PJ::NumericType::kFloat64);
  PJ::TopicId t2 = h2.topic_id;

  auto node_or = derived.addMimoTransform({t1, t2}, {"partial_sum"}, ds, std::make_unique<SumMimoTransform>());
  ASSERT_TRUE(node_or.has_value()) << node_or.error();
  PJ::TopicId out = derived.outputTopics(*node_or)[0];

  // Commit t1 only, notify only t1
  for (int i = 0; i < 5; ++i) {
    w1.appendScalar(h1, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i));
  }
  derived.onSourceCommitted(engine.commitChunks(w1.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // No output: t2 has no data yet
  EXPECT_TRUE(collect_values(engine, out).empty());

  // Now commit t2 with the SAME timestamps as t1
  for (int i = 0; i < 5; ++i) {
    w2.appendScalar(h2, static_cast<PJ::Timestamp>(i) * 1'000'000'000LL, static_cast<double>(i * 2));
  }
  derived.onSourceCommitted(engine.commitChunks(w2.flushAll()));
  ASSERT_TRUE(derived.scheduleAll().has_value());

  // Now all 5 joins should be found (watermark was NOT advanced by the first schedule)
  auto rows = collect_rows_col(engine, out);
  ASSERT_EQ(rows.size(), 5u) << "Watermark advanced incorrectly; missed joins after lazy topic";
  for (int i = 0; i < 5; ++i) {
    EXPECT_NEAR(rows[i].second, static_cast<double>(i) + static_cast<double>(i * 2), 1e-9);
  }
}

// ---------------------------------------------------------------------------
// MIMO — wrong output_kinds count is rejected (M4)
// ---------------------------------------------------------------------------

class WrongOutputKindsMimoTransform : public IMIMOTransform {
 public:
  std::vector<StorageKind> outputKinds(PJ::Span<const StorageKind> /*input_kinds*/) const override {
    return {StorageKind::kFloat64, StorageKind::kFloat64};  // returns 2 but caller expects 1
  }

  bool calculate(PJ::Timestamp, PJ::Span<const VarValue>, PJ::Timestamp&, std::vector<VarValue>&) override {
    return false;
  }
};

TEST(MimoTransformTest, WrongOutputKindsCount_Fails) {
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId t1 = make_linear_topic(engine, ds, 1.0, 5);
  // One output topic name but op returns two kinds → error
  auto r = derived.addMimoTransform({t1}, {"single_out"}, ds, std::make_unique<WrongOutputKindsMimoTransform>());
  EXPECT_FALSE(r.has_value());
}

TEST(MimoTransformTest, TopologicalOrder_MimoComesAfterSiso) {
  // n1: src→d1, n2: src→d2, n_mimo: (d1,d2)→sum. Order: n1,n2 before n_mimo.
  DataEngine engine;
  DerivedEngine derived(engine);
  PJ::DatasetId ds = make_dataset(engine);

  PJ::TopicId src = make_linear_topic(engine, ds, 1.0, 5);
  PJ::NodeId n1 = *derived.addSisoTransform(src, "d1", ds, std::make_unique<DerivativeTransform>());
  PJ::NodeId n2 = *derived.addSisoTransform(src, "d2", ds, std::make_unique<DerivativeTransform>());
  PJ::TopicId d1_out = derived.outputTopics(n1)[0];
  PJ::TopicId d2_out = derived.outputTopics(n2)[0];

  PJ::NodeId n_mimo = *derived.addMimoTransform({d1_out, d2_out}, {"sum"}, ds, std::make_unique<SumMimoTransform>());

  auto order = derived.topologicalOrder();
  ASSERT_EQ(order.size(), 3u);

  auto pos = [&](PJ::NodeId id) {
    return static_cast<std::size_t>(std::find(order.begin(), order.end(), id) - order.begin());
  };
  EXPECT_LT(pos(n1), pos(n_mimo));
  EXPECT_LT(pos(n2), pos(n_mimo));
}

// ---------------------------------------------------------------------------
// Uint64 precision round-trip (BUG-001)
// ---------------------------------------------------------------------------

// Identity SISO transform: passes value through unchanged, preserving StorageKind.
class Uint64IdentityTransform : public ISISOTransform {
 public:
  StorageKind outputKind(StorageKind input_kind) const override {
    return input_kind;
  }

  bool calculate(PJ::Timestamp time, const VarValue& input, PJ::Timestamp& out_time, VarValue& out_value) override {
    out_time = time;
    out_value = input;
    return true;
  }
};

TEST(DerivedEngine, Uint64PrecisionRoundTrip) {
  DataEngine engine;
  auto ds = make_dataset(engine);

  // Create a uint64 scalar topic with values that exceed double precision (>2^53)
  // and exceed int64_t range (>INT64_MAX).
  DataWriter writer = engine.createWriter();
  auto handle_or = writer.registerScalarSeries(ds, "u64_src", PJ::NumericType::kUint64);
  ASSERT_TRUE(handle_or.has_value());
  PJ::TopicId src_tid = handle_or->topic_id;

  const std::vector<uint64_t> test_values = {
      0ULL,
      42ULL,
      (1ULL << 53) + 1,                     // first value not exactly representable as double
      std::numeric_limits<int64_t>::max(),  // INT64_MAX
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1,  // INT64_MAX + 1
      std::numeric_limits<uint64_t>::max(),                            // UINT64_MAX
  };

  for (std::size_t i = 0; i < test_values.size(); ++i) {
    auto ts = static_cast<PJ::Timestamp>(i * 1'000'000'000LL);
    writer.appendScalar(*handle_or, ts, test_values[i]);
  }
  engine.commitChunks(writer.flushAll());

  // Register identity transform: kUint64 → kUint64
  DerivedEngine derived(engine);
  auto node_or = derived.addSisoTransform(src_tid, "u64_out", ds, std::make_unique<Uint64IdentityTransform>());
  ASSERT_TRUE(node_or.has_value());

  derived.onSourceCommitted({&src_tid, 1});
  auto s = derived.scheduleAll();
  ASSERT_TRUE(s.has_value()) << s.error();

  // Read back and verify exact bit-for-bit equality.
  auto out_topics = derived.outputTopics(*node_or);
  ASSERT_EQ(out_topics.size(), 1u);

  const TopicStorage* out_storage = engine.getTopicStorage(out_topics[0]);
  ASSERT_NE(out_storage, nullptr);
  const auto& chunks = out_storage->sealedChunks();
  ASSERT_EQ(chunks.size(), 1u);
  ASSERT_EQ(chunks[0].stats.row_count, test_values.size());

  for (std::size_t i = 0; i < test_values.size(); ++i) {
    uint64_t actual = chunks[0].readNumericAsUint64(0, i);
    EXPECT_EQ(actual, test_values[i]) << "Mismatch at row " << i;
  }
}

}  // namespace
}  // namespace PJ
