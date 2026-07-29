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
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "pj_base/descriptor_replay_protocol.h"
#include "pj_base/sdk/descriptor_replay.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"

namespace {

// A toolbox plugin advertising pj.descriptor_replay.v1, modeled on
// notify_available_topics_test.cpp's ExtensionSource. Exposes two test knobs
// (force_unknown_trust / force_unknown_outcome) that make the thunks hand
// back out-of-range C enum values, so tests can pin the C++ view's
// fail-closed mapping without a second provider implementation.
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

  bool force_unknown_trust = false;
  bool force_unknown_outcome = false;

 private:
  struct JobState {
    std::thread worker;
    std::atomic<bool> cancelled{false};
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
    state->worker = std::thread([state, on_dataset, on_terminal, callback_ctx, force_unknown_outcome] {
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
    jobCancel(ctx);
    jobJoin(ctx);
    delete static_cast<JobState*>(ctx);
  }

  static constexpr PJ_joinable_job_vtable_t kJobVtable{
      sizeof(PJ_joinable_job_vtable_t), 0, &FakeReplayToolbox::jobCancel, &FakeReplayToolbox::jobJoin,
      &FakeReplayToolbox::jobDestroy};

  PJ_descriptor_replay_provider_v1_t ext_{
      sizeof(PJ_descriptor_replay_provider_v1_t), 0, &FakeReplayToolbox::queryThunk, &FakeReplayToolbox::startThunk};
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
    // no join: the destructor must destroy (cancel+join) safely
  }
  EXPECT_EQ(terminals.load(), 1);
}

TEST(DescriptorReplayExtension, MoveWhileRunningTransfersOwnershipSafely) {
  // Pins the address-stable CallbackContext property: the worker thread
  // captured callback_ctx.get() once at start_replay time, so moving the
  // JoinableJob around (which only moves the owning unique_ptr, never the
  // pointee) must never disturb an in-flight callback.
  FakeReplayToolbox plugin;
  PJ::DescriptorReplayProviderView view(plugin.pluginExtension(PJ_DESCRIPTOR_REPLAY_EXTENSION_V1), &plugin);
  PJ::ReplayStartRequest request;
  request.descriptor_json = R"({"v":1})";
  std::atomic<int> terminals{0};

  auto job1 = view.startReplay(request, nullptr, [&](PJ::ReplayOutcome, std::string) { terminals.fetch_add(1); });
  ASSERT_TRUE(job1.has_value());

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

}  // namespace
