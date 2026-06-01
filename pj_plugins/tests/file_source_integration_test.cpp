// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"

#ifndef PJ_MOCK_FILE_SOURCE_PLUGIN_PATH
#error "PJ_MOCK_FILE_SOURCE_PLUGIN_PATH must be defined"
#endif

namespace {

// ---------------------------------------------------------------------------
// Tracking host stubs
// ---------------------------------------------------------------------------

struct WriteHostState {
  int topics_created = 0;
  int records_appended = 0;
  bool fail_next_append = false;
  std::string last_error;
};

struct RuntimeHostState {
  std::vector<PJ_data_source_state_t> state_transitions;
  int progress_starts = 0;
  int progress_updates = 0;
  int progress_finishes = 0;
  uint64_t cancel_at_step = 0;  // 0 = don't cancel
  bool stop_requested = false;
  PJ_data_source_state_t last_stop_state = PJ_DATA_SOURCE_STATE_IDLE;
  std::string last_stop_reason;
  std::vector<std::string> messages;
};

// --- Source write-host callbacks (v4, typed, noexcept) ---

bool setErr(PJ_error_t* err, const char* msg) noexcept {
  if (err != nullptr) {
    PJ::sdk::fillError(err, 1, "test", msg);
  }
  return false;
}

bool whEnsureTopic(void* ctx, PJ_string_view_t, PJ_topic_handle_t* out, PJ_error_t*) noexcept {
  auto* s = static_cast<WriteHostState*>(ctx);
  s->topics_created++;
  *out = PJ_topic_handle_t{static_cast<uint32_t>(s->topics_created)};
  return true;
}
bool whEnsureField(
    void*, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out,
    PJ_error_t*) noexcept {
  *out = PJ_field_handle_t{topic, 1};
  return true;
}
bool whAppendRecord(
    void* ctx, PJ_topic_handle_t, int64_t, const PJ_named_field_value_t*, uint64_t, PJ_error_t* err) noexcept {
  auto* s = static_cast<WriteHostState*>(ctx);
  if (s->fail_next_append) {
    s->last_error = "mock append failure";
    return setErr(err, "mock append failure");
  }
  s->records_appended++;
  return true;
}
bool whAppendBoundRecord(
    void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, uint64_t, PJ_error_t*) noexcept {
  return true;
}
bool whAppendArrowStream(
    void*, PJ_topic_handle_t, struct ArrowArrayStream* stream, PJ_string_view_t, PJ_error_t*) noexcept {
  if (stream != nullptr && stream->release != nullptr) {
    stream->release(stream);
  }
  return true;
}

// --- Runtime host callbacks (v4 — noexcept on all slots) ---

void rhReportMessage(void* ctx, PJ_data_source_message_level_t, PJ_string_view_t msg) noexcept {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  s->messages.emplace_back(msg.data, msg.size);
}
bool rhProgressStart(void* ctx, PJ_string_view_t, uint64_t, bool, PJ_error_t*) noexcept {
  static_cast<RuntimeHostState*>(ctx)->progress_starts++;
  return true;
}
bool rhProgressUpdate(void* ctx, uint64_t step) noexcept {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  s->progress_updates++;
  return s->cancel_at_step == 0 || step < s->cancel_at_step;
}
void rhProgressFinish(void* ctx) noexcept {
  static_cast<RuntimeHostState*>(ctx)->progress_finishes++;
}
bool rhIsStopRequested(void* ctx) noexcept {
  return static_cast<RuntimeHostState*>(ctx)->stop_requested;
}
void rhNotifyState(void* ctx, PJ_data_source_state_t state) noexcept {
  static_cast<RuntimeHostState*>(ctx)->state_transitions.push_back(state);
}
void rhRequestStop(void* ctx, PJ_data_source_state_t terminal, PJ_string_view_t reason) noexcept {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  s->last_stop_state = terminal;
  s->last_stop_reason = std::string(reason.data, reason.size);
}
bool rhEnsureParserBinding(
    void*, const PJ_parser_binding_request_t*, PJ_parser_binding_handle_t* out, PJ_error_t*) noexcept {
  *out = PJ_parser_binding_handle_t{1};
  return true;
}
int rhShowMessageBox(void*, PJ_message_box_type_t, PJ_string_view_t, PJ_string_view_t, int) noexcept {
  return PJ_MSG_BTN_OK;
}

// ---------------------------------------------------------------------------
// Host factory helpers
// ---------------------------------------------------------------------------

PJ_source_write_host_t makeWriteHost(WriteHostState* state) {
  static const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .ensure_topic = whEnsureTopic,
      .ensure_field = whEnsureField,
      .append_record = whAppendRecord,
      .append_bound_record = whAppendBoundRecord,
      .append_arrow_stream = whAppendArrowStream,
  };
  return PJ_source_write_host_t{.ctx = state, .vtable = &vtable};
}

PJ_data_source_runtime_host_t makeRuntimeHost(RuntimeHostState* state) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = 1,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .report_message = rhReportMessage,
      .progress_start = rhProgressStart,
      .progress_update = rhProgressUpdate,
      .progress_finish = rhProgressFinish,
      .is_stop_requested = rhIsStopRequested,
      .notify_state = rhNotifyState,
      .request_stop = rhRequestStop,
      .ensure_parser_binding = rhEnsureParserBinding,
      .show_message_box = rhShowMessageBox,
      .list_available_encodings = nullptr,
      .push_message = nullptr,
  };
  return PJ_data_source_runtime_host_t{.ctx = state, .vtable = &vtable};
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class FileSourceIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_FILE_SOURCE_PLUGIN_PATH);
    ASSERT_TRUE(lib) << lib.error();
    lib_ = std::move(*lib);
  }

  /// Register the standard write + runtime services on registry_ pointing at
  /// this fixture's state. `registry_` is a member (the builder is
  /// non-movable because view() returns a pointer into it).
  void populateRegistry() {
    registry_.registerService<PJ::sdk::SourceWriteHostService>(makeWriteHost(&write_state_));
    registry_.registerService<PJ::sdk::DataSourceRuntimeHostService>(makeRuntimeHost(&runtime_state_));
  }

  PJ::DataSourceLibrary lib_;
  WriteHostState write_state_;
  RuntimeHostState runtime_state_;
  PJ::ServiceRegistryBuilder registry_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(FileSourceIntegrationTest, LoadsAndReportsCapabilities) {
  auto handle = lib_.createHandle();
  EXPECT_TRUE(handle.valid());

  auto caps = handle.capabilities();
  EXPECT_NE(caps & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT, 0u);
  EXPECT_NE(caps & PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST, 0u);
  EXPECT_EQ(caps & PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM, 0u);
}

TEST_F(FileSourceIntegrationTest, ManifestContainsExpectedFields) {
  auto handle = lib_.createHandle();
  auto manifest = handle.manifest();
  EXPECT_NE(manifest.find("Mock File Source"), std::string::npos);
  EXPECT_NE(manifest.find(".mock"), std::string::npos);
}

TEST_F(FileSourceIntegrationTest, InitialStateIsIdle) {
  auto handle = lib_.createHandle();
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kIdle);
}

TEST_F(FileSourceIntegrationTest, SuccessfulImportLifecycle) {
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  ASSERT_TRUE(handle.start());

  ASSERT_GE(runtime_state_.state_transitions.size(), 2u);
  EXPECT_EQ(runtime_state_.state_transitions.front(), PJ_DATA_SOURCE_STATE_STARTING);
  EXPECT_EQ(runtime_state_.state_transitions.back(), PJ_DATA_SOURCE_STATE_STOPPED);
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kStopped);

  EXPECT_EQ(runtime_state_.last_stop_state, PJ_DATA_SOURCE_STATE_STOPPED);
  EXPECT_EQ(runtime_state_.last_stop_reason, "import complete");
}

TEST_F(FileSourceIntegrationTest, ProgressReporting) {
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  ASSERT_TRUE(handle.start());

  EXPECT_EQ(runtime_state_.progress_starts, 1);
  EXPECT_EQ(runtime_state_.progress_updates, 3);
  EXPECT_EQ(runtime_state_.progress_finishes, 1);
}

TEST_F(FileSourceIntegrationTest, RecordsWritten) {
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  ASSERT_TRUE(handle.start());

  EXPECT_EQ(write_state_.topics_created, 1);
  EXPECT_EQ(write_state_.records_appended, 3);
}

TEST_F(FileSourceIntegrationTest, DiagnosticMessageSent) {
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  ASSERT_TRUE(handle.start());

  ASSERT_EQ(runtime_state_.messages.size(), 1u);
  EXPECT_EQ(runtime_state_.messages[0], "imported 3 records");
}

TEST_F(FileSourceIntegrationTest, ConfigRoundTrip) {
  auto handle = lib_.createHandle();
  std::string config = R"({"filepath":"/tmp/test.mock"})";
  ASSERT_TRUE(handle.loadConfig(config));

  std::string saved;
  ASSERT_TRUE(handle.saveConfig(saved));
  EXPECT_EQ(saved, config);
}

TEST_F(FileSourceIntegrationTest, FailedAppendTransitionsToFailed) {
  write_state_.fail_next_append = true;
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  EXPECT_FALSE(handle.start());
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kFailed);

  ASSERT_GE(runtime_state_.state_transitions.size(), 2u);
  EXPECT_EQ(runtime_state_.state_transitions.front(), PJ_DATA_SOURCE_STATE_STARTING);
  EXPECT_EQ(runtime_state_.state_transitions.back(), PJ_DATA_SOURCE_STATE_FAILED);
}

TEST_F(FileSourceIntegrationTest, CancelViaProgressReturnsFalse) {
  runtime_state_.cancel_at_step = 2;
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  EXPECT_FALSE(handle.start());
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kFailed);

  EXPECT_EQ(write_state_.records_appended, 2);
}

TEST_F(FileSourceIntegrationTest, CancelViaStopRequested) {
  runtime_state_.stop_requested = true;
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  EXPECT_FALSE(handle.start());
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kFailed);

  EXPECT_EQ(write_state_.records_appended, 0);
}

TEST_F(FileSourceIntegrationTest, ProgressFinishCalledEvenOnFailure) {
  write_state_.fail_next_append = true;
  auto handle = lib_.createHandle();
  populateRegistry();
  ASSERT_TRUE(handle.bind(registry_.view()));

  EXPECT_FALSE(handle.start());

  EXPECT_EQ(runtime_state_.progress_finishes, 1);
}

}  // namespace
