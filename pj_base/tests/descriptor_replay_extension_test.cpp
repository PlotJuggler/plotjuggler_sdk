// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the "pj.descriptor_replay.v1" plugin extension consumer-side C++
// wrappers: DescriptorReplayProviderView (queryDescriptor/startReplay) and the
// RAII JoinableJob over PJ_joinable_job_t. Modeled on
// notify_available_topics_test.cpp's ExtensionSource pattern — a minimal
// ToolboxPluginBase subclass exposes the extension, and the tests drive it
// exactly like a real host would.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "pj_base/descriptor_replay_protocol.h"
#include "pj_base/sdk/descriptor_replay.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"

// A handful of tests deliberately provoke DescriptorReplayProviderView's
// leak-over-UAF path (an ABI-violating job with no usable destroy slot): the
// CallbackContext allocation is intentionally leaked (see the header's
// startReplay doc-comment). LeakSanitizer would otherwise flag that leak and
// fail the whole binary at process exit. __lsan_disable()/__lsan_enable()
// only suppress tracking for allocations made WHILE disabled, so the guarded
// region below must wrap the startReplay() call itself (where the allocation
// happens), not just the point where it becomes unreachable. Only available
// when actually built with ASAN/LSan; a no-op bracket otherwise.
#if defined(__SANITIZE_ADDRESS__)
#define PJ_TEST_HAS_LSAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PJ_TEST_HAS_LSAN 1
#endif
#endif
#ifndef PJ_TEST_HAS_LSAN
#define PJ_TEST_HAS_LSAN 0
#endif
#if PJ_TEST_HAS_LSAN
#include <sanitizer/lsan_interface.h>
#endif

namespace {

// RAII bracket around a deliberate-leak region — see the file-level comment
// above. No-op in a non-ASAN build.
class ScopedLeakSanitizerDisable {
 public:
  ScopedLeakSanitizerDisable() {
#if PJ_TEST_HAS_LSAN
    __lsan_disable();
#endif
  }
  ~ScopedLeakSanitizerDisable() {
#if PJ_TEST_HAS_LSAN
    __lsan_enable();
#endif
  }
  ScopedLeakSanitizerDisable(const ScopedLeakSanitizerDisable&) = delete;
  ScopedLeakSanitizerDisable& operator=(const ScopedLeakSanitizerDisable&) = delete;
};

// A toolbox plugin advertising pj.descriptor_replay.v1, modeled on
// notify_available_topics_test.cpp's ExtensionSource. Exposes:
//   - force_unknown_trust / force_unknown_outcome: make the thunks hand back
//     out-of-range C enum values, pinning the view's fail-closed mapping.
//   - releaseStart(): the started job's worker thread blocks on a start gate
//     as its FIRST action (conforming to the ABI's "no job callback may
//     occur before start_replay returns" rule deterministically, rather than
//     racing a background thread against the caller). Tests release it once
//     they've observed whatever pre-release state they need to pin.
class FakeReplayToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;
  }

  const void* pluginExtension(std::string_view id) override {
    if (id == PJ_DESCRIPTOR_REPLAY_EXTENSION_V1) {
      return &ext_;
    }
    return nullptr;
  }

  // Release the most recently created job's start gate. No-op if no job has
  // been created yet.
  void releaseStart() {
    if (last_job_ != nullptr) {
      last_job_->releaseStartOnce();
    }
  }

  bool force_unknown_trust = false;
  bool force_unknown_outcome = false;

 private:
  struct JobState {
    std::thread worker;
    std::atomic<bool> cancelled{false};
    std::binary_semaphore start_gate{0};
    std::atomic<bool> start_released{false};

    // At-most-once release: safe to call both from the test (via
    // releaseStart()) and unconditionally from jobDestroy (so a job whose
    // gate the test never released can't deadlock destroy()'s join).
    // std::binary_semaphore::release() on an already-released semaphore is
    // undefined behavior, hence the guard.
    void releaseStartOnce() {
      bool expected = false;
      if (start_released.compare_exchange_strong(expected, true)) {
        start_gate.release();
      }
    }
  };

  static bool queryThunk(
      void* plugin_ctx, PJ_string_view_t descriptor_json, PJ_descriptor_query_result_v1_t* out,
      PJ_error_t* /*err*/) noexcept {
    // Growth contract: write only fields wholly covered by out->struct_size.
    auto covered = [out](std::size_t off, std::size_t sz) { return out->struct_size >= off + sz; };
    auto* self = static_cast<FakeReplayToolbox*>(plugin_ctx);
    static const std::string identity = "fake:v1:sha256/128:00";
    static const std::string path = "/tmp/fake.mcap";
    if (covered(offsetof(PJ_descriptor_query_result_v1_t, trust), sizeof(out->trust))) {
      if (self != nullptr && self->force_unknown_trust) {
        out->trust = static_cast<PJ_descriptor_trust_t>(42);  // out-of-range: pins fail-closed mapping
      } else {
        out->trust = descriptor_json.size > 0 ? PJ_DESCRIPTOR_TRUST_TRUSTED : PJ_DESCRIPTOR_TRUST_REFUSED;
      }
    }
    if (covered(offsetof(PJ_descriptor_query_result_v1_t, is_materialized), sizeof(out->is_materialized))) {
      out->is_materialized = 0;
    }
    if (covered(offsetof(PJ_descriptor_query_result_v1_t, source_identity), sizeof(out->source_identity))) {
      out->source_identity = PJ_string_view_t{identity.data(), identity.size()};
    }
    if (covered(offsetof(PJ_descriptor_query_result_v1_t, local_path_utf8), sizeof(out->local_path_utf8))) {
      out->local_path_utf8 = PJ_string_view_t{path.data(), path.size()};
    }
    if (covered(offsetof(PJ_descriptor_query_result_v1_t, estimated_bytes), sizeof(out->estimated_bytes))) {
      out->estimated_bytes = 12345;
    }
    return true;
  }

  static bool startThunk(
      void* plugin_ctx, const PJ_descriptor_replay_start_request_v1_t* request,
      const PJ_descriptor_replay_callbacks_v1_t* callbacks, void* callback_ctx, PJ_joinable_job_t* out_job,
      PJ_error_t* err) noexcept {
    if ((request->flags & ~PJ_DESCRIPTOR_REPLAY_START_FLAGS_V1_MASK) != 0) {
      PJ::sdk::fillError(err, 1, "plugin", "unknown flag bits");
      return false;  // fail closed: no callbacks, out_job untouched
    }
    auto* self = static_cast<FakeReplayToolbox*>(plugin_ctx);
    const bool force_unknown_outcome = self != nullptr && self->force_unknown_outcome;
    auto on_dataset = callbacks->on_dataset;
    auto on_terminal = callbacks->on_terminal;
    auto* state = new JobState();
    // Fill out_job BEFORE spawning the worker thread: the protocol forbids
    // any job callback before start_replay itself returns, and the worker
    // must not race the caller's read of *out_job.
    out_job->ctx = state;
    out_job->vtable = &kJobVtable;
    if (self != nullptr) {
      self->last_job_ = state;
    }
    state->worker = std::thread([state, on_dataset, on_terminal, callback_ctx, force_unknown_outcome] {
      // FIRST action: block until the test explicitly releases us. This
      // makes "no job callback before start_replay returns" a deterministic,
      // assertable property (see NoJobCallbackBeforeStartGateReleased) rather
      // than a race the test would have to get lucky to catch.
      state->start_gate.acquire();
      if (on_dataset != nullptr) {
        on_dataset(callback_ctx, PJ_data_source_handle_t{7});
      }
      PJ_descriptor_replay_outcome_t outcome{};
      const char* msg = nullptr;
      if (force_unknown_outcome) {
        outcome = static_cast<PJ_descriptor_replay_outcome_t>(999);  // out-of-range: pins fail-closed mapping
        msg = "weird";
      } else {
        const bool cancelled = state->cancelled.load();
        outcome = cancelled ? PJ_DESCRIPTOR_REPLAY_CANCELLED : PJ_DESCRIPTOR_REPLAY_SUCCEEDED_UNMATERIALIZED;
        msg = cancelled ? "cancelled" : "done";
      }
      on_terminal(callback_ctx, outcome, PJ_string_view_t{msg, std::char_traits<char>::length(msg)});
    });
    return true;
  }

  static void jobCancel(void* ctx) noexcept {
    static_cast<JobState*>(ctx)->cancelled.store(true);
  }
  static void jobJoin(void* ctx) noexcept {
    auto* state = static_cast<JobState*>(ctx);
    if (state->worker.joinable()) {
      state->worker.join();
    }
  }
  static void jobDestroy(void* ctx) noexcept {
    auto* state = static_cast<JobState*>(ctx);
    jobCancel(ctx);
    // An unreleased job's worker is still blocked on start_gate: release it
    // here so join() below can't deadlock destroy().
    state->releaseStartOnce();
    jobJoin(ctx);
    delete state;
  }

  static constexpr PJ_joinable_job_vtable_t kJobVtable{
      sizeof(PJ_joinable_job_vtable_t), 0, &FakeReplayToolbox::jobCancel, &FakeReplayToolbox::jobJoin,
      &FakeReplayToolbox::jobDestroy};

  PJ_descriptor_replay_provider_v1_t ext_{
      sizeof(PJ_descriptor_replay_provider_v1_t), 0, &FakeReplayToolbox::queryThunk, &FakeReplayToolbox::startThunk};
  JobState* last_job_ = nullptr;  // non-owning; owned by the returned out_job->ctx
};

// A provider whose start_replay hands back a job with a deliberately
// malformed vtable, to exercise DescriptorReplayProviderView::startReplay's
// post-true ABI-violation validation (review Finding 2): a full-sized vtable
// missing cancel and/or join must be rejected too, not just a missing
// destroy. jobDestroy is observable via destroy_invoked so tests can confirm
// the wrapper actually calls destroy on a job it rejects (when destroy
// itself is usable).
class MalformedJobReplayToolbox : public PJ::ToolboxPluginBase {
 public:
  enum class Malformation { kNullCancel, kNullJoin, kNullDestroy, kTruncatedStructSize };

  explicit MalformedJobReplayToolbox(Malformation malformation) : malformation_(malformation) {}

  uint64_t capabilities() const override {
    return 0;
  }

  const void* pluginExtension(std::string_view id) override {
    if (id == PJ_DESCRIPTOR_REPLAY_EXTENSION_V1) {
      return &ext_;
    }
    return nullptr;
  }

  std::atomic<bool> destroy_invoked{false};

 private:
  static bool queryThunk(void*, PJ_string_view_t, PJ_descriptor_query_result_v1_t*, PJ_error_t*) noexcept {
    return true;
  }

  static void jobCancel(void*) noexcept {}
  static void jobJoin(void*) noexcept {}
  static void jobDestroy(void* ctx) noexcept {
    static_cast<MalformedJobReplayToolbox*>(ctx)->destroy_invoked.store(true);
  }

  static bool startThunk(
      void* plugin_ctx, const PJ_descriptor_replay_start_request_v1_t* /*request*/,
      const PJ_descriptor_replay_callbacks_v1_t* /*callbacks*/, void* /*callback_ctx*/, PJ_joinable_job_t* out_job,
      PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<MalformedJobReplayToolbox*>(plugin_ctx);
    out_job->ctx = self;
    out_job->vtable = self->vtableForMalformation();
    return true;
  }

  const PJ_joinable_job_vtable_t* vtableForMalformation() {
    switch (malformation_) {
      case Malformation::kNullCancel:
        vtable_ = PJ_joinable_job_vtable_t{
            sizeof(PJ_joinable_job_vtable_t), 0, nullptr, &MalformedJobReplayToolbox::jobJoin,
            &MalformedJobReplayToolbox::jobDestroy};
        break;
      case Malformation::kNullJoin:
        vtable_ = PJ_joinable_job_vtable_t{
            sizeof(PJ_joinable_job_vtable_t), 0, &MalformedJobReplayToolbox::jobCancel, nullptr,
            &MalformedJobReplayToolbox::jobDestroy};
        break;
      case Malformation::kNullDestroy:
        vtable_ = PJ_joinable_job_vtable_t{
            sizeof(PJ_joinable_job_vtable_t), 0, &MalformedJobReplayToolbox::jobCancel,
            &MalformedJobReplayToolbox::jobJoin, nullptr};
        break;
      case Malformation::kTruncatedStructSize:
        // Covers cancel and join but stops short of destroy.
        vtable_ = PJ_joinable_job_vtable_t{
            offsetof(PJ_joinable_job_vtable_t, destroy), 0, &MalformedJobReplayToolbox::jobCancel,
            &MalformedJobReplayToolbox::jobJoin, &MalformedJobReplayToolbox::jobDestroy};
        break;
    }
    return &vtable_;
  }

  Malformation malformation_;
  PJ_joinable_job_vtable_t vtable_{};
  PJ_descriptor_replay_provider_v1_t ext_{
      sizeof(PJ_descriptor_replay_provider_v1_t), 0, &MalformedJobReplayToolbox::queryThunk,
      &MalformedJobReplayToolbox::startThunk};
};

// Reproduces review Finding 1's scenario deterministically: start_replay
// returns true with an UNUSABLE job (no vtable at all — out_job left
// zeroed), while holding the copied callback pointers in a background thread
// gated to fire only once the test explicitly releases it — i.e. well after
// startReplay has already returned its error and the caller has had a chance
// to tear down whatever its closures captured by reference.
class DelayedUnusableJobReplayToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;
  }

  const void* pluginExtension(std::string_view id) override {
    if (id == PJ_DESCRIPTOR_REPLAY_EXTENSION_V1) {
      return &ext_;
    }
    return nullptr;
  }

  void releaseProviderThread() {
    gate_.release();
  }
  void joinProviderThread() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  static bool queryThunk(void*, PJ_string_view_t, PJ_descriptor_query_result_v1_t*, PJ_error_t*) noexcept {
    return true;
  }

  static bool startThunk(
      void* plugin_ctx, const PJ_descriptor_replay_start_request_v1_t* /*request*/,
      const PJ_descriptor_replay_callbacks_v1_t* callbacks, void* callback_ctx, PJ_joinable_job_t* out_job,
      PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<DelayedUnusableJobReplayToolbox*>(plugin_ctx);
    auto on_terminal = callbacks->on_terminal;
    self->worker_ = std::thread([self, on_terminal, callback_ctx] {
      self->gate_.acquire();
      const char* msg = "late";
      on_terminal(callback_ctx, PJ_DESCRIPTOR_REPLAY_SUCCEEDED_UNMATERIALIZED, PJ_string_view_t{msg, 4});
    });
    // ABI violation: leave out_job unusable (zeroed vtable) despite
    // returning true — this is exactly the case startReplay's
    // leak-over-UAF/quiesce path exists for.
    *out_job = PJ_joinable_job_t{};
    return true;
  }

  std::thread worker_;
  std::binary_semaphore gate_{0};
  PJ_descriptor_replay_provider_v1_t ext_{
      sizeof(PJ_descriptor_replay_provider_v1_t), 0, &DelayedUnusableJobReplayToolbox::queryThunk,
      &DelayedUnusableJobReplayToolbox::startThunk};
};

TEST(DescriptorReplayExtension, QueryRoundTripsThroughView) {
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  ASSERT_TRUE(view.valid());
  auto result = view.queryDescriptor(R"({"v":1})");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->trust, PJ::DescriptorTrust::kTrusted);
  EXPECT_FALSE(result->is_materialized);
  EXPECT_EQ(result->source_identity, "fake:v1:sha256/128:00");
  EXPECT_EQ(result->local_path_utf8, "/tmp/fake.mcap");
  EXPECT_EQ(result->estimated_bytes, 12345u);
}

TEST(DescriptorReplayExtension, UnknownFlagBitsFailClosed) {
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  request.flags = UINT64_C(1) << 63;  // not in the v1 mask
  bool dataset_seen = false;
  bool terminal_seen = false;
  auto job = view.startReplay(
      request, [&](PJ::DatasetId) { dataset_seen = true; },
      [&](PJ::ReplayOutcome, std::string) { terminal_seen = true; });
  EXPECT_FALSE(job.has_value());
  EXPECT_FALSE(dataset_seen);
  EXPECT_FALSE(terminal_seen);
}

TEST(DescriptorReplayExtension, StartReplayDeliversDatasetThenTerminalExactlyOnce) {
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  std::vector<std::string> order;
  int terminals = 0;
  PJ::ReplayOutcome outcome = PJ::ReplayOutcome::kFailed;
  {
    auto job = view.startReplay(
        request, [&](PJ::DatasetId id) { order.push_back("dataset:" + std::to_string(id)); },
        [&](PJ::ReplayOutcome o, std::string) {
          order.push_back("terminal");
          outcome = o;
          ++terminals;
        });
    ASSERT_TRUE(job.has_value());
    plugin.releaseStart();
    job->join();  // returns only after on_terminal returned
    EXPECT_EQ(terminals, 1);
  }  // ~JoinableJob: destroy is idempotent after join
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], "dataset:7");
  EXPECT_EQ(order[1], "terminal");
  EXPECT_EQ(outcome, PJ::ReplayOutcome::kSucceededUnmaterialized);
}

TEST(DescriptorReplayExtension, DestroyWithoutJoinDestroysSafely) {
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  std::atomic<int> terminals{0};
  {
    auto job = view.startReplay(request, nullptr, [&](PJ::ReplayOutcome, std::string) { terminals.fetch_add(1); });
    ASSERT_TRUE(job.has_value());
    // No release and no join: the worker is still blocked on its start
    // gate. The destructor must destroy (cancel, release the gate so the
    // worker can't deadlock it, then join) safely.
  }
  EXPECT_EQ(terminals.load(), 1);
}

TEST(DescriptorReplayExtension, MoveWhileRunningTransfersOwnershipSafely) {
  // Pins the address-stable CallbackContext property: the worker thread
  // captured callback_ctx.get() once at start_replay time, so moving the
  // JoinableJob around (which only moves the owning unique_ptr, never the
  // pointee) must never disturb an in-flight callback. Released BEFORE the
  // moves so the worker is genuinely running (racing the moves below),
  // matching the property's original intent.
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  std::atomic<int> terminals{0};

  auto job1 = view.startReplay(request, nullptr, [&](PJ::ReplayOutcome, std::string) { terminals.fetch_add(1); });
  ASSERT_TRUE(job1.has_value());
  plugin.releaseStart();  // let the worker run while we perform the moves below

  // Move-construct while the worker may still be running.
  PJ::JoinableJob job2(std::move(*job1));
  EXPECT_FALSE(job1->valid());
  job1->cancel();  // moved-from: must be a safe no-op
  job1->join();    // moved-from: must be a safe no-op

  // Move-assign into a third handle.
  PJ::JoinableJob job3;
  job3 = std::move(job2);
  EXPECT_FALSE(job2.valid());
  job2.cancel();  // moved-from: must be a safe no-op
  job2.join();    // moved-from: must be a safe no-op

  job3.join();  // returns only after on_terminal has returned
  EXPECT_EQ(terminals.load(), 1);
}

TEST(DescriptorReplayExtension, NoJobCallbackBeforeStartGateReleased) {
  // Pins "no job callback may occur before start_replay returns" using the
  // fake's deterministic start gate: right after startReplay returns, the
  // worker is guaranteed still blocked, so zero callbacks having happened
  // yet is assertable synchronously instead of racy.
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  std::atomic<int> datasets{0};
  std::atomic<int> terminals{0};

  auto job = view.startReplay(
      request, [&](PJ::DatasetId) { datasets.fetch_add(1); },
      [&](PJ::ReplayOutcome, std::string) { terminals.fetch_add(1); });
  ASSERT_TRUE(job.has_value());

  EXPECT_EQ(datasets.load(), 0);
  EXPECT_EQ(terminals.load(), 0);

  plugin.releaseStart();
  job->join();

  EXPECT_EQ(datasets.load(), 1);
  EXPECT_EQ(terminals.load(), 1);
}

TEST(DescriptorReplayExtension, GrowthContractSmallerCallerCapacityGetsOnlyCoveredFields) {
  // Simulate an OLD caller: capacity ends before estimated_bytes.
  FakeReplayToolbox plugin;
  const auto* ext =
      static_cast<const PJ_descriptor_replay_provider_v1_t*>(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1));
  ASSERT_NE(ext, nullptr);
  PJ_descriptor_query_result_v1_t result{};
  result.struct_size = offsetof(PJ_descriptor_query_result_v1_t, estimated_bytes);
  result.estimated_bytes = 999;  // sentinel: provider must NOT touch it
  PJ_string_view_t json{"{}", 2};
  PJ_error_t err{};
  ASSERT_TRUE(ext->query_descriptor(&plugin, json, &result, &err));
  EXPECT_EQ(result.estimated_bytes, 999u);
  EXPECT_EQ(result.trust, PJ_DESCRIPTOR_TRUST_TRUSTED);
}

TEST(DescriptorReplayExtension, UnknownTrustValueMapsToRefused) {
  FakeReplayToolbox plugin;
  plugin.force_unknown_trust = true;

  // Raw ABI call with a NULL out_error — pins the protocol header's rule
  // that out_error may be NULL everywhere and callees must tolerate it.
  const auto* ext =
      static_cast<const PJ_descriptor_replay_provider_v1_t*>(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1));
  ASSERT_NE(ext, nullptr);
  PJ_descriptor_query_result_v1_t raw{};
  raw.struct_size = sizeof(raw);
  PJ_string_view_t json{"{}", 2};
  ASSERT_TRUE(ext->query_descriptor(&plugin, json, &raw, nullptr));
  EXPECT_EQ(raw.trust, static_cast<PJ_descriptor_trust_t>(42));

  PJ::DescriptorReplayProviderView view(ext, &plugin);
  auto result = view.queryDescriptor("{}");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->trust, PJ::DescriptorTrust::kRefused);  // fail-closed
}

TEST(DescriptorReplayExtension, UnknownOutcomeValueMapsToFailed) {
  FakeReplayToolbox plugin;
  plugin.force_unknown_outcome = true;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  PJ::ReplayOutcome outcome = PJ::ReplayOutcome::kSucceededUnmaterialized;
  auto job = view.startReplay(request, nullptr, [&](PJ::ReplayOutcome o, std::string) { outcome = o; });
  ASSERT_TRUE(job.has_value());
  plugin.releaseStart();
  job->join();
  EXPECT_EQ(outcome, PJ::ReplayOutcome::kFailed);  // fail-closed
}

TEST(DescriptorReplayExtension, ThrowingTerminalCallbackIsSwallowed) {
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  auto job =
      view.startReplay(request, nullptr, [](PJ::ReplayOutcome, std::string) { throw std::runtime_error("boom"); });
  ASSERT_TRUE(job.has_value());
  plugin.releaseStart();
  job->join();  // must not terminate/crash even though on_terminal threw
  SUCCEED();
}

TEST(DescriptorReplayExtension, TruncatedExtensionYieldsInvalidView) {
  FakeReplayToolbox plugin;
  const auto* ext =
      static_cast<const PJ_descriptor_replay_provider_v1_t*>(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1));
  ASSERT_NE(ext, nullptr);
  PJ_descriptor_replay_provider_v1_t truncated = *ext;
  truncated.struct_size = offsetof(PJ_descriptor_replay_provider_v1_t, start_replay);
  PJ::DescriptorReplayProviderView view(&truncated, &plugin);
  EXPECT_FALSE(view.valid());
}

TEST(DescriptorReplayExtension, PluginWithoutExtensionYieldsInvalidView) {
  class PlainToolbox : public PJ::ToolboxPluginBase {
   public:
    uint64_t capabilities() const override {
      return 0;
    }
  };
  PlainToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  EXPECT_FALSE(view.valid());
  EXPECT_FALSE(view.queryDescriptor("{}").has_value());
}

// ---------- Finding 2: malformed-vtable matrix ----------

TEST(DescriptorReplayExtension, MalformedJobNullCancelIsRejectedButDestroyed) {
  MalformedJobReplayToolbox plugin(MalformedJobReplayToolbox::Malformation::kNullCancel);
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  auto job = view.startReplay(request, nullptr, nullptr);
  EXPECT_FALSE(job.has_value());
  EXPECT_TRUE(plugin.destroy_invoked.load());  // destroy IS usable: the wrapper must call it on the rejected job
}

TEST(DescriptorReplayExtension, MalformedJobNullJoinIsRejectedButDestroyed) {
  MalformedJobReplayToolbox plugin(MalformedJobReplayToolbox::Malformation::kNullJoin);
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  auto job = view.startReplay(request, nullptr, nullptr);
  EXPECT_FALSE(job.has_value());
  EXPECT_TRUE(plugin.destroy_invoked.load());
}

TEST(DescriptorReplayExtension, MalformedJobNullDestroyIsRejectedWithoutCrashing) {
  MalformedJobReplayToolbox plugin(MalformedJobReplayToolbox::Malformation::kNullDestroy);
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  {
    const ScopedLeakSanitizerDisable guard;  // destroy is unusable: this path deliberately leaks
    auto job = view.startReplay(request, nullptr, nullptr);
    EXPECT_FALSE(job.has_value());
  }
  EXPECT_FALSE(plugin.destroy_invoked.load());  // no usable destroy to invoke
}

TEST(DescriptorReplayExtension, MalformedJobTruncatedVtableIsRejectedWithoutCrashing) {
  MalformedJobReplayToolbox plugin(MalformedJobReplayToolbox::Malformation::kTruncatedStructSize);
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  {
    const ScopedLeakSanitizerDisable guard;  // destroy is not covered by struct_size: deliberately leaks
    auto job = view.startReplay(request, nullptr, nullptr);
    EXPECT_FALSE(job.has_value());
  }
  EXPECT_FALSE(plugin.destroy_invoked.load());
}

// ---------- Finding 1: quiesce-before-leak ----------

TEST(DescriptorReplayExtension, DelayedProviderCallbackAfterAbiViolationNeverTouchesFreedUserState) {
  DelayedUnusableJobReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";

  int callback_calls = 0;
  {
    // Sentinel captured BY REFERENCE by on_terminal below. Heap-allocated so
    // that if a leaked, late provider callback ever dereferences it after
    // this scope exits, ASAN's heap-use-after-free detector fires.
    auto sentinel = std::make_unique<int>(42);
    int& sentinel_ref = *sentinel;

    {
      // The ABI-violation path deliberately leaks the CallbackContext (see
      // startReplay's doc-comment) — expected, not a bug under test here.
      const ScopedLeakSanitizerDisable guard;
      auto job = view.startReplay(request, nullptr, [&sentinel_ref, &callback_calls](PJ::ReplayOutcome, std::string) {
        ++callback_calls;
        sentinel_ref = 99;  // would be a use-after-free if this ever ran late
      });
      EXPECT_FALSE(job.has_value());
    }
    // sentinel is destroyed here (scope exit): simulates the caller treating
    // startReplay's error as terminal and tearing down its captured state.
  }

  // Only now does the provider's background thread get to proceed — well
  // after the error returned and the sentinel was destroyed.
  plugin.releaseProviderThread();
  plugin.joinProviderThread();

  EXPECT_EQ(callback_calls, 0);  // quiesced: the leaked context never dispatched
}

}  // namespace
