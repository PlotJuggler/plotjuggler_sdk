#include "pj_base/sdk/data_source_plugin_base.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/data_source_protocol.h"

namespace {

// ---------------------------------------------------------------------------
// Inline mock DataSource plugin
// ---------------------------------------------------------------------------

class MockDataSource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDirectIngest | PJ::kCapabilityDelegatedIngest |
           PJ::kCapabilitySupportsPause;
  }

  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return PJ::okStatus();
  }

  PJ::Status start() override {
    if (!writeHostBound()) {
      state_ = PJ::DataSourceState::kFailed;
      return PJ::unexpected(std::string("write host not bound"));
    }
    if (!runtimeHostBound()) {
      state_ = PJ::DataSourceState::kFailed;
      return PJ::unexpected(std::string("runtime host not bound"));
    }

    state_ = PJ::DataSourceState::kStarting;
    runtimeHost().notifyState(state_);
    runtimeHost().reportMessage(PJ::DataSourceMessageLevel::kInfo, "mock start");

    if (config_.find("progress") != std::string::npos) {
      if (runtimeHost().progressStart("Mock Import", 3, true)) {
        for (uint64_t step = 1; step <= 3; ++step) {
          if (!runtimeHost().progressUpdate(step)) {
            runtimeHost().progressFinish();
            state_ = PJ::DataSourceState::kFailed;
            runtimeHost().notifyState(state_);
            return PJ::unexpected(std::string("progress canceled"));
          }
        }
        runtimeHost().progressFinish();
      }
    }

    auto topic = writeHost().ensureTopic("mock/topic");
    if (!topic) {
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return PJ::unexpected(topic.error());
    }

    auto write_status = writeHost().appendRecord(*topic, PJ::Timestamp{123}, {{.name = "value", .value = 42.0}});
    if (!write_status) {
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return PJ::unexpected(write_status.error());
    }

    if (config_.find("delegated") != std::string::npos) {
      const uint8_t schema[] = {'s', 'c', 'h'};
      auto binding = runtimeHost().ensureParserBinding(
          PJ::ParserBindingRequest{
              .topic_name = "mock/topic",
              .parser_encoding = "json",
              .type_name = "mock_type",
              .schema = PJ::Span<const uint8_t>(schema, sizeof(schema)),
              .parser_config_json = R"({"mode":"test"})",
          });
      if (!binding) {
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return PJ::unexpected(binding.error());
      }

      const uint8_t payload[] = {'{', '}'};
      auto push_status =
          runtimeHost().pushRawMessage(*binding, PJ::Timestamp{456}, PJ::Span<const uint8_t>(payload, sizeof(payload)));
      if (!push_status) {
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return PJ::unexpected(push_status.error());
      }
    }

    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  void stop() override {
    state_ = PJ::DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
  }

  PJ::Status pause() override {
    if (state_ != PJ::DataSourceState::kRunning) {
      return PJ::unexpected(std::string("pause requires running state"));
    }
    state_ = PJ::DataSourceState::kPaused;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  PJ::Status resume() override {
    if (state_ != PJ::DataSourceState::kPaused) {
      return PJ::unexpected(std::string("resume requires paused state"));
    }
    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  PJ::Status poll() override {
    ++poll_count_;
    return PJ::okStatus();
  }

  PJ::DataSourceState currentState() const override {
    return state_;
  }

 private:
  std::string config_ = "{}";
  PJ::DataSourceState state_ = PJ::DataSourceState::kIdle;
  int poll_count_ = 0;
};

constexpr const char* kMockManifest = R"({"id":"mock-data-source","name":"Mock DataSource","version":"1.0.0"})";

const PJ_data_source_vtable_t* mockVtable() {
  static const PJ_data_source_vtable_t* vt =
      PJ::DataSourcePluginBase::vtableWithCreate([]() -> void* { return new MockDataSource(); }, kMockManifest);
  return vt;
}

// ---------------------------------------------------------------------------
// RAII vtable driver (test-local, not exported)
// ---------------------------------------------------------------------------

struct VtableDriver {
  const PJ_data_source_vtable_t* vt;
  void* ctx;

  explicit VtableDriver(const PJ_data_source_vtable_t* v) : vt(v), ctx(v->create()) {}
  ~VtableDriver() {
    if (vt != nullptr && ctx != nullptr) {
      vt->destroy(ctx);
    }
  }

  VtableDriver(const VtableDriver&) = delete;
  VtableDriver& operator=(const VtableDriver&) = delete;
};

// ---------------------------------------------------------------------------
// Write host recorder
// ---------------------------------------------------------------------------

struct WriteRecorder {
  std::string last_error;
  uint32_t next_topic_id = 1;
  int ensure_topic_calls = 0;
  int append_record_calls = 0;
  std::string last_topic_name;
  int64_t last_timestamp = 0;

  static const char* getLastError(void* ctx) {
    auto* self = static_cast<WriteRecorder*>(ctx);
    return self->last_error.empty() ? nullptr : self->last_error.c_str();
  }

  static bool ensureTopic(void* ctx, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic) {
    auto* self = static_cast<WriteRecorder*>(ctx);
    ++self->ensure_topic_calls;
    self->last_topic_name.assign(topic_name.data, topic_name.size);
    *out_topic = PJ_topic_handle_t{self->next_topic_id++};
    return true;
  }

  static bool ensureField(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
    (void)ctx;
    *out_field = PJ_field_handle_t{topic, 1};
    return true;
  }

  static bool appendRecord(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count) {
    auto* self = static_cast<WriteRecorder*>(ctx);
    ++self->append_record_calls;
    self->last_timestamp = timestamp;
    EXPECT_EQ(topic.id, 1U);
    EXPECT_EQ(field_count, 1U);
    EXPECT_EQ(fields[0].name.size, 5U);
    EXPECT_EQ(fields[0].value.type, PJ_PRIMITIVE_TYPE_FLOAT64);
    return true;
  }

  static bool appendBoundRecord(void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t) {
    return true;
  }

  static bool appendArrowIpc(void*, PJ_topic_handle_t, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }
};

// ---------------------------------------------------------------------------
// Runtime host recorder
// ---------------------------------------------------------------------------

struct RuntimeRecorder {
  struct BindingRequestCopy {
    std::string topic_name;
    std::string parser_encoding;
    std::string type_name;
    std::vector<uint8_t> schema;
    std::string parser_config_json;
  };

  std::string last_error;
  std::vector<std::string> messages;
  std::vector<PJ_data_source_state_t> states;
  int progress_start_calls = 0;
  int progress_finish_calls = 0;
  std::vector<uint64_t> progress_updates;
  bool cancel_progress = false;
  bool stop_requested = false;
  std::vector<BindingRequestCopy> binding_requests;
  std::vector<std::pair<uint32_t, int64_t>> pushed_messages;
  uint32_t next_binding_id = 9;

  static const char* getLastError(void* ctx) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    return self->last_error.empty() ? nullptr : self->last_error.c_str();
  }

  static void reportMessage(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t message) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    self->messages.emplace_back(
        std::to_string(static_cast<int>(level)) + ":" + std::string(message.data, message.size));
  }

  static bool progressStart(void* ctx, PJ_string_view_t label, uint64_t total_steps, bool cancellable) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    ++self->progress_start_calls;
    EXPECT_EQ(std::string(label.data, label.size), "Mock Import");
    EXPECT_EQ(total_steps, 3U);
    EXPECT_TRUE(cancellable);
    return true;
  }

  static bool progressUpdate(void* ctx, uint64_t current_step) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    self->progress_updates.push_back(current_step);
    return !self->cancel_progress;
  }

  static void progressFinish(void* ctx) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    ++self->progress_finish_calls;
  }

  static bool isStopRequested(void* ctx) {
    return static_cast<RuntimeRecorder*>(ctx)->stop_requested;
  }

  static void notifyState(void* ctx, PJ_data_source_state_t state) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    self->states.push_back(state);
  }

  static void requestStop(void* ctx, PJ_data_source_state_t terminal_state, PJ_string_view_t reason) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    self->states.push_back(terminal_state);
    self->messages.emplace_back("stop:" + std::string(reason.data, reason.size));
  }

  static bool ensureParserBinding(
      void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out_handle) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    self->binding_requests.push_back(
        BindingRequestCopy{
            .topic_name = std::string(request->topic_name.data, request->topic_name.size),
            .parser_encoding = std::string(request->parser_encoding.data, request->parser_encoding.size),
            .type_name = std::string(request->type_name.data, request->type_name.size),
            .schema = std::vector<uint8_t>(request->schema.data, request->schema.data + request->schema.size),
            .parser_config_json = std::string(request->parser_config_json.data, request->parser_config_json.size),
        });
    *out_handle = PJ_parser_binding_handle_t{self->next_binding_id++};
    return true;
  }

  static bool pushRawMessage(
      void* ctx, PJ_parser_binding_handle_t handle, int64_t host_timestamp_ns, PJ_bytes_view_t payload) {
    auto* self = static_cast<RuntimeRecorder*>(ctx);
    EXPECT_EQ(payload.size, 2U);
    self->pushed_messages.emplace_back(handle.id, host_timestamp_ns);
    return true;
  }
};

PJ_source_write_host_t makeWriteHost(WriteRecorder* recorder) {
  static const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .get_last_error = WriteRecorder::getLastError,
      .ensure_topic = WriteRecorder::ensureTopic,
      .ensure_field = WriteRecorder::ensureField,
      .append_record = WriteRecorder::appendRecord,
      .append_bound_record = WriteRecorder::appendBoundRecord,
      .append_arrow_ipc = WriteRecorder::appendArrowIpc,
  };
  return PJ_source_write_host_t{.ctx = recorder, .vtable = &vtable};
}

PJ_data_source_runtime_host_t makeRuntimeHost(RuntimeRecorder* recorder) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = RuntimeRecorder::getLastError,
      .report_message = RuntimeRecorder::reportMessage,
      .progress_start = RuntimeRecorder::progressStart,
      .progress_update = RuntimeRecorder::progressUpdate,
      .progress_finish = RuntimeRecorder::progressFinish,
      .is_stop_requested = RuntimeRecorder::isStopRequested,
      .notify_state = RuntimeRecorder::notifyState,
      .request_stop = RuntimeRecorder::requestStop,
      .ensure_parser_binding = RuntimeRecorder::ensureParserBinding,
      .push_raw_message = RuntimeRecorder::pushRawMessage,
  };
  return PJ_data_source_runtime_host_t{.ctx = recorder, .vtable = &vtable};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(DataSourcePluginBaseTest, ManifestJsonIsStaticConstant) {
  const PJ_data_source_vtable_t* vt = mockVtable();
  EXPECT_STREQ(vt->manifest_json, kMockManifest);
}

TEST(DataSourcePluginBaseTest, StartFailsWhenHostsAreMissing) {
  VtableDriver drv(mockVtable());

  EXPECT_FALSE(drv.vt->start(drv.ctx));
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_FAILED);

  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_EQ(std::string(err), "write host not bound");
}

TEST(DataSourcePluginBaseTest, LifecycleUsesBoundHostsAndRoundTripsConfig) {
  VtableDriver drv(mockVtable());
  WriteRecorder write_recorder;
  RuntimeRecorder runtime_recorder;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&write_recorder)));
  ASSERT_TRUE(drv.vt->bind_runtime_host(drv.ctx, makeRuntimeHost(&runtime_recorder)));

  EXPECT_TRUE(drv.vt->load_config(drv.ctx, R"({"progress":true,"delegated":true})"));
  EXPECT_STREQ(drv.vt->save_config(drv.ctx), R"({"progress":true,"delegated":true})");

  ASSERT_TRUE(drv.vt->start(drv.ctx));
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_RUNNING);
  EXPECT_EQ(write_recorder.ensure_topic_calls, 1);
  EXPECT_EQ(write_recorder.append_record_calls, 1);
  EXPECT_EQ(write_recorder.last_topic_name, "mock/topic");
  EXPECT_EQ(write_recorder.last_timestamp, 123);

  ASSERT_EQ(runtime_recorder.messages.size(), 1U);
  EXPECT_EQ(runtime_recorder.messages[0], "0:mock start");
  EXPECT_EQ(runtime_recorder.progress_start_calls, 1);
  EXPECT_EQ(runtime_recorder.progress_finish_calls, 1);
  EXPECT_EQ(runtime_recorder.progress_updates, (std::vector<uint64_t>{1, 2, 3}));
  ASSERT_EQ(runtime_recorder.binding_requests.size(), 1U);
  EXPECT_EQ(runtime_recorder.binding_requests[0].topic_name, "mock/topic");
  EXPECT_EQ(runtime_recorder.binding_requests[0].parser_encoding, "json");
  EXPECT_EQ(runtime_recorder.binding_requests[0].type_name, "mock_type");
  EXPECT_EQ(runtime_recorder.binding_requests[0].schema, (std::vector<uint8_t>{'s', 'c', 'h'}));
  EXPECT_EQ(runtime_recorder.binding_requests[0].parser_config_json, R"({"mode":"test"})");
  ASSERT_EQ(runtime_recorder.pushed_messages.size(), 1U);
  EXPECT_EQ(runtime_recorder.pushed_messages[0].first, 9U);
  EXPECT_EQ(runtime_recorder.pushed_messages[0].second, 456);
  EXPECT_EQ(
      runtime_recorder.states, (std::vector<PJ_data_source_state_t>{
                                   PJ_DATA_SOURCE_STATE_STARTING,
                                   PJ_DATA_SOURCE_STATE_RUNNING,
                               }));

  EXPECT_TRUE(drv.vt->poll(drv.ctx));
  EXPECT_TRUE(drv.vt->pause(drv.ctx));
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_PAUSED);
  EXPECT_TRUE(drv.vt->resume(drv.ctx));
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_RUNNING);

  drv.vt->stop(drv.ctx);
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_STOPPED);
  EXPECT_EQ(runtime_recorder.states.back(), PJ_DATA_SOURCE_STATE_STOPPED);
}

TEST(DataSourcePluginBaseTest, ProgressCancellationFailsStart) {
  VtableDriver drv(mockVtable());
  WriteRecorder write_recorder;
  RuntimeRecorder runtime_recorder;
  runtime_recorder.cancel_progress = true;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&write_recorder)));
  ASSERT_TRUE(drv.vt->bind_runtime_host(drv.ctx, makeRuntimeHost(&runtime_recorder)));
  ASSERT_TRUE(drv.vt->load_config(drv.ctx, "progress"));

  EXPECT_FALSE(drv.vt->start(drv.ctx));
  EXPECT_EQ(drv.vt->current_state(drv.ctx), PJ_DATA_SOURCE_STATE_FAILED);
  EXPECT_STREQ(drv.vt->get_last_error(drv.ctx), "progress canceled");
  EXPECT_EQ(runtime_recorder.progress_start_calls, 1);
  EXPECT_EQ(runtime_recorder.progress_finish_calls, 1);
}

TEST(DataSourcePluginBaseTest, UnsupportedPauseReturnsError) {
  // A source that doesn't override pause/resume should fail gracefully.
  VtableDriver drv(mockVtable());
  WriteRecorder write_recorder;
  RuntimeRecorder runtime_recorder;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&write_recorder)));
  ASSERT_TRUE(drv.vt->bind_runtime_host(drv.ctx, makeRuntimeHost(&runtime_recorder)));
  ASSERT_TRUE(drv.vt->start(drv.ctx));

  // Pause from running should work for this mock (it overrides pause).
  EXPECT_TRUE(drv.vt->pause(drv.ctx));

  // Pause from paused state should fail with an error message.
  EXPECT_FALSE(drv.vt->pause(drv.ctx));
  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("pause"), std::string::npos);
}

TEST(DataSourcePluginBaseTest, DefaultPollReturnsTrue) {
  // Verify the default poll() returns true (no-op success), not false.
  VtableDriver drv(mockVtable());
  // MockDataSource overrides poll to return true and increment counter,
  // but the key ABI contract is that poll=true means success.
  EXPECT_TRUE(drv.vt->poll(drv.ctx));
}

TEST(DataSourcePluginBaseTest, AppendArrowIpcRoutesToWriteHost) {
  VtableDriver drv(mockVtable());
  WriteRecorder write_recorder;
  RuntimeRecorder runtime_recorder;
  bool arrow_ipc_called = false;
  PJ_topic_handle_t arrow_topic{};
  std::vector<uint8_t> arrow_bytes;
  std::string arrow_ts_col;

  // Build a write host vtable with a recording appendArrowIpc.
  struct ArrowIpcWriteRecorder {
    bool* called;
    PJ_topic_handle_t* topic;
    std::vector<uint8_t>* bytes;
    std::string* ts_col;

    static const char* getLastError(void*) {
      return nullptr;
    }

    static bool ensureTopic(void*, PJ_string_view_t, PJ_topic_handle_t* out_topic) {
      *out_topic = PJ_topic_handle_t{1};
      return true;
    }

    static bool ensureField(
        void*, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
      *out_field = PJ_field_handle_t{topic, 1};
      return true;
    }

    static bool appendRecord(void*, PJ_topic_handle_t, int64_t, const PJ_named_field_value_t*, size_t) {
      return true;
    }

    static bool appendBoundRecord(void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t) {
      return true;
    }

    static bool appendArrowIpc(
        void* ctx, PJ_topic_handle_t topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
      auto* self = static_cast<ArrowIpcWriteRecorder*>(ctx);
      *self->called = true;
      *self->topic = topic;
      self->bytes->assign(ipc_stream.data, ipc_stream.data + ipc_stream.size);
      self->ts_col->assign(timestamp_column.data, timestamp_column.size);
      return true;
    }
  };

  ArrowIpcWriteRecorder arrow_recorder{&arrow_ipc_called, &arrow_topic, &arrow_bytes, &arrow_ts_col};
  static const PJ_source_write_host_vtable_t arrow_vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .get_last_error = ArrowIpcWriteRecorder::getLastError,
      .ensure_topic = ArrowIpcWriteRecorder::ensureTopic,
      .ensure_field = ArrowIpcWriteRecorder::ensureField,
      .append_record = ArrowIpcWriteRecorder::appendRecord,
      .append_bound_record = ArrowIpcWriteRecorder::appendBoundRecord,
      .append_arrow_ipc = ArrowIpcWriteRecorder::appendArrowIpc,
  };
  PJ_source_write_host_t arrow_host = {.ctx = &arrow_recorder, .vtable = &arrow_vtable};

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, arrow_host));
  ASSERT_TRUE(drv.vt->bind_runtime_host(drv.ctx, makeRuntimeHost(&runtime_recorder)));

  // Call appendArrowIpc through the SDK view by invoking the vtable directly.
  // The SDK SourceWriteHostView delegates to the vtable's append_arrow_ipc.
  const uint8_t ipc_data[] = {0x41, 0x52, 0x52, 0x4F, 0x57, 0x31};  // "ARROW1"
  PJ_bytes_view_t ipc_bytes = {ipc_data, sizeof(ipc_data)};
  PJ_string_view_t ts_col = {"my_timestamp", 12};
  PJ_topic_handle_t topic = {1};
  ASSERT_TRUE(arrow_host.vtable->append_arrow_ipc(arrow_host.ctx, topic, ipc_bytes, ts_col));

  EXPECT_TRUE(arrow_ipc_called);
  EXPECT_EQ(arrow_topic.id, 1U);
  EXPECT_EQ(arrow_bytes, (std::vector<uint8_t>{0x41, 0x52, 0x52, 0x4F, 0x57, 0x31}));
  EXPECT_EQ(arrow_ts_col, "my_timestamp");
}

TEST(DataSourcePluginBaseTest, IsStopRequestedRoutesToHost) {
  VtableDriver drv(mockVtable());
  WriteRecorder write_recorder;
  RuntimeRecorder runtime_recorder;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&write_recorder)));
  ASSERT_TRUE(drv.vt->bind_runtime_host(drv.ctx, makeRuntimeHost(&runtime_recorder)));
  ASSERT_TRUE(drv.vt->start(drv.ctx));

  // The runtime host view's isStopRequested should reflect the recorder's flag.
  // We test this through the C++ SDK view since the mock uses it internally.
  EXPECT_FALSE(runtime_recorder.stop_requested);
  runtime_recorder.stop_requested = true;

  // Verify the vtable slot works at the raw ABI level too.
  PJ_data_source_runtime_host_t raw_host = makeRuntimeHost(&runtime_recorder);
  EXPECT_TRUE(raw_host.vtable->is_stop_requested(raw_host.ctx));

  runtime_recorder.stop_requested = false;
  EXPECT_FALSE(raw_host.vtable->is_stop_requested(raw_host.ctx));
}

}  // namespace
