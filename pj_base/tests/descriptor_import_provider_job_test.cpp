// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// The callee-side job runner against the ABI's threading rules: no callback
// before start returns, on_dataset zero-or-one, on_terminal exactly-once and
// last, cancel/join/destroy semantics, the header-only growth-contract
// helpers, and the settlement latch.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "pj_base/sdk/descriptor_import.hpp"
#include "pj_base/sdk/descriptor_import/provider_job.hpp"
#include "pj_base/sdk/testing/provider_job_probe.hpp"

namespace {

using namespace std::chrono_literals;
using PJ::readDescriptorImportStartRequest;
using PJ::writeDescriptorQueryResult;
using PJ::sdk::descriptor_import::ImportOutcome;
using PJ::sdk::descriptor_import::JobControl;
using PJ::sdk::descriptor_import::ProviderJob;
using PJ::sdk::descriptor_import::SettlementLatch;
using PJ::sdk::descriptor_import::testing::startWithGateProbe;

struct Recorder {
  std::atomic<int> datasets{0};
  std::atomic<int> terminals{0};
  std::atomic<int> last_outcome{-1};
  std::string last_message;
  std::mutex mu;

  static void onDataset(void* ctx, PJ_data_source_handle_t) noexcept {
    static_cast<Recorder*>(ctx)->datasets.fetch_add(1);
  }
  static void onTerminal(void* ctx, PJ_descriptor_import_outcome_t outcome, PJ_string_view_t message) noexcept {
    auto* self = static_cast<Recorder*>(ctx);
    self->terminals.fetch_add(1);
    self->last_outcome.store(static_cast<int>(outcome));
    std::lock_guard<std::mutex> lock(self->mu);
    self->last_message.assign(message.data, message.size);
  }
};

/// The recorder, a filled callback struct, and a job handle destroyed through
/// the vtable — the preamble every job test repeats.
class ProviderJobTest : public ::testing::Test {
 protected:
  ProviderJobTest() {
    callbacks_.struct_size = sizeof(callbacks_);
    callbacks_.on_dataset = &Recorder::onDataset;
    callbacks_.on_terminal = &Recorder::onTerminal;
  }
  ~ProviderJobTest() override {
    if (job_.vtable != nullptr) {
      job_.vtable->destroy(job_.ctx);
    }
  }
  void join() {
    job_.vtable->join(job_.ctx);
  }
  void cancel() {
    job_.vtable->cancel(job_.ctx);
  }

  Recorder rec_;
  PJ_descriptor_import_callbacks_v1_t callbacks_{};
  PJ_joinable_job_t job_{};
};

TEST_F(ProviderJobTest, RejectsMissingTerminalAndNullOutJob) {
  PJ_error_t error{};
  const auto body = [](JobControl&) { return ImportOutcome{}; };

  auto no_terminal = callbacks_;
  no_terminal.on_terminal = nullptr;
  EXPECT_FALSE(ProviderJob::start(body, &no_terminal, &rec_, &job_, &error));
  EXPECT_EQ(job_.vtable, nullptr);  // untouched

  auto short_struct = callbacks_;
  short_struct.struct_size = offsetof(PJ_descriptor_import_callbacks_v1_t, on_terminal);  // terminal not covered
  EXPECT_FALSE(ProviderJob::start(body, &short_struct, &rec_, &job_, &error));
  EXPECT_FALSE(ProviderJob::start(body, nullptr, &rec_, &job_, &error));
  EXPECT_FALSE(ProviderJob::start(body, &callbacks_, &rec_, nullptr, &error));
  EXPECT_FALSE(ProviderJob::start(nullptr, &callbacks_, &rec_, &job_, &error));
  EXPECT_EQ(rec_.terminals.load(), 0);
}

TEST_F(ProviderJobTest, NoCallbackBeforeStartReturnsAndTerminalExactlyOnce) {
  std::atomic<bool> terminal_seen_in_probe{false};
  const auto body = [](JobControl& control) {
    control.notifyDataset(PJ_data_source_handle_t{7});
    control.notifyDataset(PJ_data_source_handle_t{8});  // ignored: zero-or-one
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_SUCCEEDED_PROMOTED, "done"};
  };
  // The probe runs after the worker exists but before the gate opens: the
  // worker must still be parked, so no terminal can have fired.
  ASSERT_TRUE(startWithGateProbe(body, &callbacks_, &rec_, &job_, nullptr, [&] {
    std::this_thread::sleep_for(30ms);
    terminal_seen_in_probe.store(rec_.terminals.load() != 0);
  }));
  EXPECT_FALSE(terminal_seen_in_probe.load());
  join();
  join();  // idempotent
  EXPECT_EQ(rec_.datasets.load(), 1);
  EXPECT_EQ(rec_.terminals.load(), 1);
  EXPECT_EQ(rec_.last_outcome.load(), PJ_DESCRIPTOR_IMPORT_SUCCEEDED_PROMOTED);
  EXPECT_EQ(rec_.last_message, "done");
}

TEST_F(ProviderJobTest, CancelFlagsTheBodyAndFiresTheHookOnce) {
  std::atomic<int> hook_calls{0};
  std::atomic<bool> body_started{false};
  const auto body = [&](JobControl& control) {
    control.onCancel([&hook_calls] { hook_calls.fetch_add(1); });
    body_started.store(true);
    while (!control.isCancelled()) {
      std::this_thread::sleep_for(1ms);
    }
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_CANCELLED, "cancelled"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  while (!body_started.load()) {
    std::this_thread::sleep_for(1ms);
  }
  cancel();
  cancel();  // idempotent
  join();
  EXPECT_EQ(hook_calls.load(), 1);
  EXPECT_EQ(rec_.terminals.load(), 1);
  EXPECT_EQ(rec_.last_outcome.load(), PJ_DESCRIPTOR_IMPORT_CANCELLED);
}

TEST_F(ProviderJobTest, HookRegisteredAfterCancelFiresImmediately) {
  std::atomic<int> hook_calls{0};
  std::atomic<bool> may_register{false};
  std::atomic<bool> waiting{false};
  const auto body = [&](JobControl& control) {
    waiting.store(true);
    while (!may_register.load()) {
      std::this_thread::sleep_for(1ms);
    }
    control.onCancel([&hook_calls] { hook_calls.fetch_add(1); });
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_CANCELLED, "cancelled"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  while (!waiting.load()) {
    std::this_thread::sleep_for(1ms);
  }
  cancel();
  may_register.store(true);
  join();
  EXPECT_EQ(hook_calls.load(), 1);
}

TEST_F(ProviderJobTest, CancelBeforeTheGateOpensSkipsTheBody) {
  std::atomic<bool> body_ran{false};
  const auto body = [&](JobControl&) {
    body_ran.store(true);
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY, "ran"};
  };
  ASSERT_TRUE(startWithGateProbe(body, &callbacks_, &rec_, &job_, nullptr, [this] { cancel(); }));
  join();
  EXPECT_FALSE(body_ran.load());
  EXPECT_EQ(rec_.last_outcome.load(), PJ_DESCRIPTOR_IMPORT_CANCELLED);
}

TEST_F(ProviderJobTest, BodyExceptionBecomesFailedTerminal) {
  const auto body = [](JobControl&) -> ImportOutcome { throw std::runtime_error("boom"); };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  join();
  EXPECT_EQ(rec_.terminals.load(), 1);
  EXPECT_EQ(rec_.last_outcome.load(), PJ_DESCRIPTOR_IMPORT_FAILED);
  EXPECT_NE(rec_.last_message.find("internal error"), std::string::npos);
}

TEST_F(ProviderJobTest, WatchdogFiresOnceWhenTheBodyOverruns) {
  std::atomic<int> expired{0};
  const auto body = [&](JobControl& control) {
    control.armWatchdog(20ms, [&expired] { expired.fetch_add(1); });
    std::this_thread::sleep_for(120ms);
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_FAILED, "ceiling"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  join();
  EXPECT_EQ(expired.load(), 1);
  EXPECT_EQ(rec_.terminals.load(), 1);
}

TEST_F(ProviderJobTest, WatchdogDoesNotFireWhenTheBodyFinishesFirst) {
  std::atomic<int> expired{0};
  const auto body = [&](JobControl& control) {
    control.armWatchdog(5s, [&expired] { expired.fetch_add(1); });
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY, "quick"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  join();  // returns promptly: the watchdog is stopped, not awaited
  EXPECT_EQ(expired.load(), 0);
}

TEST_F(ProviderJobTest, DestroyWithoutJoinCancelsAndJoins) {
  std::atomic<bool> started{false};
  const auto body = [&](JobControl& control) {
    started.store(true);
    while (!control.isCancelled()) {
      std::this_thread::sleep_for(1ms);
    }
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_CANCELLED, "cancelled"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  while (!started.load()) {
    std::this_thread::sleep_for(1ms);
  }
  job_.vtable->destroy(job_.ctx);
  job_ = PJ_joinable_job_t{};  // the fixture must not destroy it again
  EXPECT_EQ(rec_.terminals.load(), 1);
  EXPECT_EQ(rec_.last_outcome.load(), PJ_DESCRIPTOR_IMPORT_CANCELLED);
}

TEST_F(ProviderJobTest, ConcurrentJoinersAllReturnAfterTheTerminal) {
  const auto body = [](JobControl&) {
    std::this_thread::sleep_for(20ms);
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY, "ok"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  std::vector<std::thread> joiners;
  std::atomic<int> saw_terminal{0};
  for (int i = 0; i < 4; ++i) {
    joiners.emplace_back([&] {
      join();
      if (rec_.terminals.load() == 1) {
        saw_terminal.fetch_add(1);
      }
    });
  }
  for (auto& t : joiners) {
    t.join();
  }
  EXPECT_EQ(saw_terminal.load(), 4);
}

TEST_F(ProviderJobTest, JoinFromTheJobCallbackThreadIsANoOp) {
  std::atomic<bool> returned{false};
  const auto body = [&](JobControl&) {
    join();  // self-join: must not deadlock
    returned.store(true);
    return ImportOutcome{PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY, "ok"};
  };
  ASSERT_TRUE(ProviderJob::start(body, &callbacks_, &rec_, &job_, nullptr));
  join();
  EXPECT_TRUE(returned.load());
}

// ---------------------------------------------------------------------------
// The header-only growth-contract helpers (pj_base/sdk/descriptor_import.hpp)
// ---------------------------------------------------------------------------

TEST(DescriptorImportAbi, ReadStartRequestFailsClosedOnUnknownFlags) {
  PJ_descriptor_import_start_request_v1_t request{};
  request.struct_size = sizeof(request);
  const char* json = "{\"v\":1}";
  request.descriptor_json = PJ_string_view_t{json, std::strlen(json)};
  request.max_transfer_bytes = 42;
  auto view = readDescriptorImportStartRequest(&request);
  ASSERT_TRUE(view) << view.error();
  EXPECT_EQ(view->descriptor_json, "{\"v\":1}");
  EXPECT_EQ(view->max_transfer_bytes, 42u);

  request.flags = UINT64_C(1) << 40;
  view = readDescriptorImportStartRequest(&request);
  ASSERT_FALSE(view);
  EXPECT_NE(view.error().find("fail closed"), std::string::npos);
  EXPECT_FALSE(readDescriptorImportStartRequest(nullptr));
}

TEST(DescriptorImportAbi, ReadStartRequestHonoursTheCallerStructSize) {
  PJ_descriptor_import_start_request_v1_t request{};
  request.struct_size = offsetof(PJ_descriptor_import_start_request_v1_t, flags);  // only descriptor_json covered
  const char* json = "{}";
  request.descriptor_json = PJ_string_view_t{json, 2};
  request.flags = 0xFF;             // NOT covered: must be ignored, not rejected
  request.max_transfer_bytes = 99;  // NOT covered
  const auto view = readDescriptorImportStartRequest(&request);
  ASSERT_TRUE(view) << view.error();
  EXPECT_EQ(view->descriptor_json, "{}");
  EXPECT_EQ(view->flags, PJ_DESCRIPTOR_IMPORT_START_FLAG_NONE);
  EXPECT_EQ(view->max_transfer_bytes, 0u);
}

TEST(DescriptorImportAbi, WriteQueryResultTouchesOnlyCoveredFields) {
  PJ_descriptor_query_result_v1_t raw{};
  raw.struct_size = sizeof(raw);
  PJ::DescriptorQueryResult result;
  result.trust = PJ::DescriptorTrust::kTrusted;
  result.is_materialized = true;
  result.source_identity = "id";
  result.local_path_utf8 = "/p";
  result.message = "m";
  result.estimated_bytes = 1234;
  writeDescriptorQueryResult(&raw, result);
  EXPECT_EQ(raw.trust, PJ_DESCRIPTOR_TRUST_TRUSTED);
  EXPECT_EQ(raw.is_materialized, 1u);
  EXPECT_EQ(std::string(raw.source_identity.data, raw.source_identity.size), "id");
  EXPECT_EQ(std::string(raw.local_path_utf8.data, raw.local_path_utf8.size), "/p");
  EXPECT_EQ(std::string(raw.message.data, raw.message.size), "m");
  EXPECT_EQ(raw.estimated_bytes, 1234u);

  PJ_descriptor_query_result_v1_t old_peer{};
  old_peer.struct_size = offsetof(PJ_descriptor_query_result_v1_t, message);  // message + estimate not covered
  old_peer.estimated_bytes = 5;
  writeDescriptorQueryResult(&old_peer, result);
  EXPECT_EQ(old_peer.trust, PJ_DESCRIPTOR_TRUST_TRUSTED);
  EXPECT_EQ(old_peer.message.data, nullptr);
  EXPECT_EQ(old_peer.estimated_bytes, 5u);
  writeDescriptorQueryResult(nullptr, result);  // no-op
}

TEST(SettlementLatch, SettlesOnceAndWaitsCancelAware) {
  SettlementLatch latch;
  EXPECT_FALSE(latch.settled());
  EXPECT_FALSE(latch.wait([] { return true; }, 1ms));  // cancelled first, still live
  std::thread settler([&latch] {
    std::this_thread::sleep_for(10ms);
    latch.settle(true, "first");
    latch.settle(false, "second");  // ignored
  });
  EXPECT_TRUE(latch.wait([] { return false; }, 1ms));
  settler.join();
  EXPECT_TRUE(latch.settled());
  EXPECT_TRUE(latch.ok());
  EXPECT_EQ(latch.detail(), "first");
  // A settle that raced a cancel still reports truthfully: settled wins.
  EXPECT_TRUE(latch.wait([] { return true; }, 1ms));
}

}  // namespace
