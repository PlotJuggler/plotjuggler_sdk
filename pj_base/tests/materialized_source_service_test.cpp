// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the "pj.materialized_source.v1" host service consumer-side C++
// wrapper: MaterializedSourceHostView and the PJ::sdk::MaterializedSourceHostService
// trait. Modeled on data_processors_api_test.cpp's fake-host pattern.
//
// pj_base's test target links only pj_base + GTest::gtest_main (see the
// foreach in pj_base/CMakeLists.txt) — pj_plugins' ServiceRegistryBuilder is
// not visible here. So registry construction below hand-rolls a minimal
// PJ_service_registry_vtable_t (FakeServiceRegistry) instead, which has the
// added benefit of exercising name/version matching explicitly.

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "pj_base/descriptor_replay_protocol.h"
#include "pj_base/sdk/descriptor_replay.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"

namespace {

// Fake host for the pj.materialized_source.v1 service, modeled on
// data_processors_api_test.cpp's fake host.
struct FakeAdoptionHost {
  struct Captured {
    uint32_t dataset = 0;
    std::string source_identity;
    std::string local_path;
    std::string loader_plugin_id;
    std::string loader_config_json;
    std::string descriptor_json;
  };
  std::vector<Captured> requests;
  bool accept = true;   // false => synchronous rejection, callback must not run
  bool succeed = true;  // outcome delivered through the callback

  static bool adoptThunk(
      void* ctx, const PJ_materialized_source_adopt_request_v1_t* request,
      PJ_materialized_source_adopt_result_fn result_cb, void* callback_ctx, PJ_error_t* err) noexcept {
    auto* self = static_cast<FakeAdoptionHost*>(ctx);
    if (!self->accept) {
      PJ::sdk::fillError(err, 1, "materialized_source", "rejected");
      return false;
    }
    Captured c;
    c.dataset = request->dataset.id;
    c.source_identity = std::string(PJ::sdk::toStringView(request->source_identity));
    c.local_path = std::string(PJ::sdk::toStringView(request->local_path_utf8));
    c.loader_plugin_id = std::string(PJ::sdk::toStringView(request->loader_plugin_id));
    c.loader_config_json = std::string(PJ::sdk::toStringView(request->loader_config_json));
    c.descriptor_json = std::string(PJ::sdk::toStringView(request->descriptor_json));
    self->requests.push_back(std::move(c));
    const char* msg = self->succeed ? "adopted" : "generation mismatch";
    result_cb(callback_ctx, self->succeed, PJ_string_view_t{msg, std::char_traits<char>::length(msg)});
    return true;
  }

  [[nodiscard]] PJ_materialized_source_host_t view() {
    static constexpr PJ_materialized_source_host_vtable_t kVtable{
        1, sizeof(PJ_materialized_source_host_vtable_t), &FakeAdoptionHost::adoptThunk};
    return PJ_materialized_source_host_t{this, &kVtable};
  }
};

// A host whose adopt() is genuinely asynchronous — the ABI's documented
// NORMAL mode ("the call returns before the operation completes"), unlike
// FakeAdoptionHost above which delivers result_cb re-entrantly. adoptThunk
// stores {result_cb, callback_ctx} and returns true immediately; a separate
// worker thread delivers the result later, gated on an explicit release()
// so the test can deterministically observe "adopt() returned before the
// callback ran" without sleeps-as-synchronization.
struct DeferredAdoptionHost {
  std::mutex mu;
  std::condition_variable cv;
  bool release_requested = false;
  PJ_materialized_source_adopt_result_fn stored_cb = nullptr;
  void* stored_ctx = nullptr;
  std::thread worker;

  static bool adoptThunk(
      void* ctx, const PJ_materialized_source_adopt_request_v1_t* /*request*/,
      PJ_materialized_source_adopt_result_fn result_cb, void* callback_ctx, PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<DeferredAdoptionHost*>(ctx);
    self->stored_cb = result_cb;
    self->stored_ctx = callback_ctx;
    self->worker = std::thread([self]() {
      {
        std::unique_lock<std::mutex> lock(self->mu);
        self->cv.wait(lock, [self] { return self->release_requested; });
      }
      const char* msg = "adopted";
      self->stored_cb(self->stored_ctx, true, PJ_string_view_t{msg, std::char_traits<char>::length(msg)});
    });
    return true;
  }

  // Signal the worker thread to deliver the deferred result now.
  void release() {
    {
      const std::lock_guard<std::mutex> lock(mu);
      release_requested = true;
    }
    cv.notify_one();
  }

  // Join deterministically (no sleeps) before asserting on delivery.
  void joinWorker() {
    if (worker.joinable()) {
      worker.join();
    }
  }

  [[nodiscard]] PJ_materialized_source_host_t view() {
    static constexpr PJ_materialized_source_host_vtable_t kVtable{
        1, sizeof(PJ_materialized_source_host_vtable_t), &DeferredAdoptionHost::adoptThunk};
    return PJ_materialized_source_host_t{this, &kVtable};
  }
};

// A minimal, hand-rolled PJ_service_registry_t backing a single named
// service registration. Kept local to this test rather than pulling in
// pj_plugins' ServiceRegistryBuilder (not visible to pj_base's test target).
// Mirrors real lookup semantics: both name AND min_version must match.
class FakeServiceRegistry {
 public:
  void registerService(std::string name, uint32_t protocol_version, PJ_service_t service) {
    name_ = std::move(name);
    protocol_version_ = protocol_version;
    service_ = service;
  }

  [[nodiscard]] PJ_service_registry_t view() noexcept {
    return PJ_service_registry_t{this, &kVtable};
  }

 private:
  static bool dispatchGetService(
      void* ctx, PJ_string_view_t name, uint32_t min_version, PJ_service_t* out_service,
      PJ_error_t* out_error) noexcept {
    auto* self = static_cast<FakeServiceRegistry*>(ctx);
    const std::string key(name.data == nullptr ? "" : name.data, name.size);
    if (self->name_.empty() || key != self->name_) {
      PJ::sdk::fillError(out_error, 1, "registry", "unknown service name");
      return false;
    }
    if (self->protocol_version_ < min_version) {
      PJ::sdk::fillError(out_error, 2, "registry", "registered service version is lower than requested minimum");
      return false;
    }
    *out_service = self->service_;
    return true;
  }

  static constexpr PJ_service_registry_vtable_t kVtable{
      1, sizeof(PJ_service_registry_vtable_t), &FakeServiceRegistry::dispatchGetService};

  std::string name_;
  uint32_t protocol_version_ = 0;
  PJ_service_t service_{};
};

PJ::AdoptRequest makeRequest() {
  PJ::AdoptRequest r;
  r.dataset = 7;
  r.source_identity = "mcap-cloud:v1:sha256/128:aa";
  r.local_path_utf8 = "/cache/aa.mcap";
  r.loader_plugin_id = "data_load_mcap";
  r.loader_config_json = R"({"use_log_time":true})";
  r.descriptor_json = R"({"v":1})";
  return r;
}

TEST(MaterializedSourceService, RegistryLookupAndAdoptExactlyOnce) {
  FakeAdoptionHost host;
  const PJ_materialized_source_host_t raw_host = host.view();

  // Registered with protocol_version 1, exactly matching the trait's
  // kMinVersion 1 — this is the min-version-match path (equal, not just
  // "greater than"), exercised through the fake registry's real comparison.
  FakeServiceRegistry fake_registry;
  fake_registry.registerService(
      PJ::sdk::MaterializedSourceHostService::kName, /*protocol_version=*/1,
      PJ_service_t{raw_host.ctx, static_cast<const void*>(raw_host.vtable)});

  PJ::sdk::ServiceRegistry registry(fake_registry.view());
  auto view_opt = registry.get<PJ::sdk::MaterializedSourceHostService>();
  ASSERT_TRUE(view_opt.has_value());
  EXPECT_TRUE(view_opt->valid());

  int callback_calls = 0;
  bool last_ok = false;
  std::string last_message;
  auto status = view_opt->adopt(makeRequest(), [&](bool ok, std::string message) {
    ++callback_calls;
    last_ok = ok;
    last_message = std::move(message);
  });

  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(callback_calls, 1);
  EXPECT_TRUE(last_ok);
  EXPECT_EQ(last_message, "adopted");

  ASSERT_EQ(host.requests.size(), 1u);
  const auto& captured = host.requests[0];
  EXPECT_EQ(captured.dataset, 7u);
  EXPECT_EQ(captured.source_identity, "mcap-cloud:v1:sha256/128:aa");
  EXPECT_EQ(captured.local_path, "/cache/aa.mcap");
  EXPECT_EQ(captured.loader_plugin_id, "data_load_mcap");
  EXPECT_EQ(captured.loader_config_json, R"({"use_log_time":true})");
  EXPECT_EQ(captured.descriptor_json, R"({"v":1})");
}

// Pins kMinVersion forwarding: a registration below the trait's kMinVersion
// (1) must be treated as absent, exercising FakeServiceRegistry's
// version-rejection branch (dead code until this test).
TEST(MaterializedSourceService, ProtocolVersionBelowMinIsNullopt) {
  FakeAdoptionHost host;
  const PJ_materialized_source_host_t raw_host = host.view();

  FakeServiceRegistry fake_registry;
  fake_registry.registerService(
      PJ::sdk::MaterializedSourceHostService::kName, /*protocol_version=*/0,
      PJ_service_t{raw_host.ctx, static_cast<const void*>(raw_host.vtable)});

  PJ::sdk::ServiceRegistry registry(fake_registry.view());
  auto view_opt = registry.get<PJ::sdk::MaterializedSourceHostService>();
  EXPECT_FALSE(view_opt.has_value());
}

TEST(MaterializedSourceService, SynchronousRejectionNeverRunsCallback) {
  FakeAdoptionHost host;
  host.accept = false;
  PJ::MaterializedSourceHostView view(host.view());
  ASSERT_TRUE(view.valid());

  int callback_calls = 0;
  auto status = view.adopt(makeRequest(), [&](bool /*ok*/, std::string /*message*/) { ++callback_calls; });

  EXPECT_FALSE(status);
  EXPECT_EQ(callback_calls, 0);
  EXPECT_TRUE(host.requests.empty());
}

TEST(MaterializedSourceService, AbsentServiceIsNullopt) {
  FakeServiceRegistry fake_registry;  // nothing registered
  PJ::sdk::ServiceRegistry registry(fake_registry.view());

  auto view_opt = registry.get<PJ::sdk::MaterializedSourceHostService>();
  EXPECT_FALSE(view_opt.has_value());
}

TEST(MaterializedSourceService, InvalidViewRejectsAdopt) {
  PJ::MaterializedSourceHostView view;  // default-constructed
  EXPECT_FALSE(view.valid());

  int callback_calls = 0;
  auto status = view.adopt(makeRequest(), [&](bool /*ok*/, std::string /*message*/) { ++callback_calls; });

  EXPECT_FALSE(status);
  EXPECT_EQ(callback_calls, 0);
}

TEST(MaterializedSourceService, CallbackExceptionIsSwallowed) {
  FakeAdoptionHost host;
  PJ::MaterializedSourceHostView view(host.view());
  ASSERT_TRUE(view.valid());

  // The exception must be swallowed at the ABI thunk boundary — this test's
  // assertion is simply that adopt() returns normally and the process does
  // not crash/terminate.
  auto status =
      view.adopt(makeRequest(), [](bool /*ok*/, std::string /*message*/) { throw std::runtime_error("boom"); });

  EXPECT_TRUE(status) << status.error();
  ASSERT_EQ(host.requests.size(), 1u);
}

// The ABI's documented normal mode is asynchronous ("the call returns before
// the operation completes") — exercised here with DeferredAdoptionHost
// instead of FakeAdoptionHost's re-entrant-synchronous delivery. This is
// also the path where adopt()'s release()-before-the-vtable-call ordering
// earns its keep: ownership must already belong to the raw pointer handed
// to the host, not to the local unique_ptr, by the time result_cb might run
// on another thread.
TEST(MaterializedSourceService, DeferredAsyncResultDeliveredExactlyOnceAfterReturn) {
  DeferredAdoptionHost host;
  PJ::MaterializedSourceHostView view(host.view());
  ASSERT_TRUE(view.valid());

  std::atomic<int> callback_calls{0};
  std::atomic<bool> callback_ran{false};
  bool last_ok = false;
  std::string last_message;

  auto status = view.adopt(makeRequest(), [&](bool ok, std::string message) {
    callback_calls.fetch_add(1);
    last_ok = ok;
    last_message = std::move(message);
    callback_ran.store(true);
  });

  // adopt() must report ACCEPTED before the deferred callback has run: the
  // worker thread is blocked on host.release(), which we have not called
  // yet, so this is deterministic rather than a timing-dependent guess.
  ASSERT_TRUE(status) << status.error();
  EXPECT_FALSE(callback_ran.load());
  EXPECT_EQ(callback_calls.load(), 0);

  host.release();
  host.joinWorker();

  EXPECT_EQ(callback_calls.load(), 1);
  EXPECT_TRUE(last_ok);
  EXPECT_EQ(last_message, "adopted");
}

}  // namespace
