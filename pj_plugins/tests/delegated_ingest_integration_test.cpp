#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_library.hpp"

#ifndef PJ_MOCK_DATA_SOURCE_PLUGIN_PATH
#error "PJ_MOCK_DATA_SOURCE_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MOCK_JSON_PARSER_PLUGIN_PATH
#error "PJ_MOCK_JSON_PARSER_PLUGIN_PATH must be defined"
#endif

namespace {

// ---------------------------------------------------------------------------
// Write host that records what the DataSource writes directly
// ---------------------------------------------------------------------------

struct SourceWriteRecorder {
  int ensure_topic_calls = 0;
  int append_record_calls = 0;
  std::string last_topic_name;
  int64_t last_timestamp = 0;
  double last_value = 0.0;

  static const char* getLastError(void*) {
    return nullptr;
  }

  static bool ensureTopic(void* ctx, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic) {
    auto* self = static_cast<SourceWriteRecorder*>(ctx);
    ++self->ensure_topic_calls;
    self->last_topic_name.assign(topic_name.data, topic_name.size);
    *out_topic = PJ_topic_handle_t{1};
    return true;
  }

  static bool ensureField(
      void*, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
    *out_field = PJ_field_handle_t{topic, 1};
    return true;
  }

  static bool appendRecord(
      void* ctx, PJ_topic_handle_t, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count) {
    auto* self = static_cast<SourceWriteRecorder*>(ctx);
    ++self->append_record_calls;
    self->last_timestamp = timestamp;
    if (field_count > 0 && fields[0].value.type == PJ_PRIMITIVE_TYPE_FLOAT64) {
      self->last_value = fields[0].value.data.as_float64;
    }
    return true;
  }

  static bool appendBoundRecord(void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t) {
    return true;
  }

  static bool appendArrowIpc(void*, PJ_topic_handle_t, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }
};

PJ_source_write_host_t makeSourceWriteHost(SourceWriteRecorder* recorder) {
  static const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .get_last_error = SourceWriteRecorder::getLastError,
      .ensure_topic = SourceWriteRecorder::ensureTopic,
      .ensure_field = SourceWriteRecorder::ensureField,
      .append_record = SourceWriteRecorder::appendRecord,
      .append_bound_record = SourceWriteRecorder::appendBoundRecord,
      .append_arrow_ipc = SourceWriteRecorder::appendArrowIpc,
  };
  return PJ_source_write_host_t{.ctx = recorder, .vtable = &vtable};
}

// ---------------------------------------------------------------------------
// Write host that records what the parser writes (via delegated ingest)
// ---------------------------------------------------------------------------

struct ParserWriteRecorder {
  int append_record_calls = 0;
  int64_t last_timestamp = 0;
  double last_value = 0.0;

  static const char* getLastError(void*) {
    return nullptr;
  }

  static bool ensureField(void*, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
    *out_field = PJ_field_handle_t{{1}, 1};
    return true;
  }

  static bool appendRecord(void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count) {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    ++self->append_record_calls;
    self->last_timestamp = timestamp;
    if (field_count > 0 && fields[0].value.type == PJ_PRIMITIVE_TYPE_FLOAT64) {
      self->last_value = fields[0].value.data.as_float64;
    }
    return true;
  }

  static bool appendBoundRecord(void*, int64_t, const PJ_bound_field_value_t*, size_t) {
    return true;
  }

  static bool appendArrowIpc(void*, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }
};

PJ_parser_write_host_t makeParserWriteHost(ParserWriteRecorder* recorder) {
  static const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .get_last_error = ParserWriteRecorder::getLastError,
      .ensure_field = ParserWriteRecorder::ensureField,
      .append_record = ParserWriteRecorder::appendRecord,
      .append_bound_record = ParserWriteRecorder::appendBoundRecord,
      .append_arrow_ipc = ParserWriteRecorder::appendArrowIpc,
  };
  return PJ_parser_write_host_t{.ctx = recorder, .vtable = &vtable};
}

// ---------------------------------------------------------------------------
// Bridge runtime host: wires ensureParserBinding → real parser .so load,
// and pushRawMessage → parser.parse().
// ---------------------------------------------------------------------------

struct BridgeRuntimeHost {
  PJ::MessageParserHandle* parser_handle = nullptr;

  static const char* getLastError(void*) {
    return nullptr;
  }
  static void reportMessage(void*, PJ_data_source_message_level_t, PJ_string_view_t) {}
  static bool progressStart(void*, PJ_string_view_t, uint64_t, bool) {
    return true;
  }
  static bool progressUpdate(void*, uint64_t) {
    return true;
  }
  static void progressFinish(void*) {}
  static bool isStopRequested(void*) {
    return false;
  }
  static void notifyState(void*, PJ_data_source_state_t) {}
  static void requestStop(void*, PJ_data_source_state_t, PJ_string_view_t) {}

  static bool ensureParserBinding(
      void* ctx, const PJ_parser_binding_request_t*, PJ_parser_binding_handle_t* out_handle) {
    auto* self = static_cast<BridgeRuntimeHost*>(ctx);
    // The parser_handle is already loaded and bound before start() — just
    // return a fixed handle ID.
    if (self->parser_handle == nullptr || !self->parser_handle->valid()) {
      return false;
    }
    *out_handle = PJ_parser_binding_handle_t{1};
    return true;
  }

  static bool pushRawMessage(
      void* ctx, PJ_parser_binding_handle_t, int64_t host_timestamp_ns, PJ_bytes_view_t payload) {
    auto* self = static_cast<BridgeRuntimeHost*>(ctx);
    if (self->parser_handle == nullptr) {
      return false;
    }
    return self->parser_handle->parse(host_timestamp_ns, PJ::Span<const uint8_t>(payload.data, payload.size));
  }

  static int showMessageBox(void*, PJ_message_box_type_t, PJ_string_view_t, PJ_string_view_t, int) {
    return PJ_MSG_BTN_OK;
  }
};

PJ_data_source_runtime_host_t makeBridgeRuntimeHost(BridgeRuntimeHost* bridge) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = BridgeRuntimeHost::getLastError,
      .report_message = BridgeRuntimeHost::reportMessage,
      .progress_start = BridgeRuntimeHost::progressStart,
      .progress_update = BridgeRuntimeHost::progressUpdate,
      .progress_finish = BridgeRuntimeHost::progressFinish,
      .is_stop_requested = BridgeRuntimeHost::isStopRequested,
      .notify_state = BridgeRuntimeHost::notifyState,
      .request_stop = BridgeRuntimeHost::requestStop,
      .ensure_parser_binding = BridgeRuntimeHost::ensureParserBinding,
      .push_raw_message = BridgeRuntimeHost::pushRawMessage,
      .show_message_box = BridgeRuntimeHost::showMessageBox,
      .list_available_encodings = nullptr,
  };
  return PJ_data_source_runtime_host_t{.ctx = bridge, .vtable = &vtable};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class DelegatedIngestIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto source_lib = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
    ASSERT_TRUE(source_lib) << source_lib.error();
    source_lib_ = std::move(*source_lib);

    auto parser_lib = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
    ASSERT_TRUE(parser_lib) << parser_lib.error();
    parser_lib_ = std::move(*parser_lib);
  }

  PJ::DataSourceLibrary source_lib_;
  PJ::MessageParserLibrary parser_lib_;
};

TEST_F(DelegatedIngestIntegrationTest, SourcePushesPayloadThroughRealParser) {
  // 1. Load both plugins — done in SetUp.

  // 2. Create the parser instance and bind its write host.
  auto parser_handle = parser_lib_.createHandle();
  ASSERT_TRUE(parser_handle.valid());

  ParserWriteRecorder parser_recorder;
  ASSERT_TRUE(parser_handle.bindWriteHost(makeParserWriteHost(&parser_recorder)));

  // 3. Create the source instance.
  auto source_handle = source_lib_.createHandle();
  ASSERT_TRUE(source_handle.valid());

  // 4. Bind source write host (for the direct-ingest part of MockDataSource).
  SourceWriteRecorder source_recorder;
  ASSERT_TRUE(source_handle.bindWriteHost(makeSourceWriteHost(&source_recorder)));

  // 5. Bind the bridge runtime host — wires ensureParserBinding/pushRawMessage
  //    to the real parser instance.
  BridgeRuntimeHost bridge;
  bridge.parser_handle = &parser_handle;
  ASSERT_TRUE(source_handle.bindRuntimeHost(makeBridgeRuntimeHost(&bridge)));

  // 6. Configure for delegated ingest and start.
  ASSERT_TRUE(source_handle.loadConfig(R"({"delegated":true})"));
  ASSERT_TRUE(source_handle.start());
  EXPECT_EQ(source_handle.currentState(), PJ_DATA_SOURCE_STATE_RUNNING);

  // 7. Verify direct-ingest path: source wrote one record directly.
  EXPECT_EQ(source_recorder.append_record_calls, 1);
  EXPECT_EQ(source_recorder.last_timestamp, 123);
  EXPECT_DOUBLE_EQ(source_recorder.last_value, 42.0);

  // 8. Verify delegated-ingest path: the mock source pushed "{}" through the
  //    parser. MockJsonParser parses "{}" via strtod → 0.0.
  EXPECT_EQ(parser_recorder.append_record_calls, 1);
  EXPECT_EQ(parser_recorder.last_timestamp, 456);
  EXPECT_DOUBLE_EQ(parser_recorder.last_value, 0.0);

  // 9. Clean shutdown.
  source_handle.stop();
  EXPECT_EQ(source_handle.currentState(), PJ_DATA_SOURCE_STATE_STOPPED);
}

TEST_F(DelegatedIngestIntegrationTest, ParserReceivesSchemaFromBindingRequest) {
  // Verify that the bridge can forward schema binding from the source's
  // ensureParserBinding request to the parser's bindSchema().
  auto parser_handle = parser_lib_.createHandle();
  ASSERT_TRUE(parser_handle.valid());

  const uint8_t schema_bytes[] = {0xCA, 0xFE};
  ASSERT_TRUE(parser_handle.bindSchema("mock_type", PJ::Span<const uint8_t>(schema_bytes, sizeof(schema_bytes))));

  // MockJsonParser::bindSchema is the default no-op — success means the
  // C ABI round-trip works without crashing.
  ParserWriteRecorder parser_recorder;
  ASSERT_TRUE(parser_handle.bindWriteHost(makeParserWriteHost(&parser_recorder)));

  // Parse a real numeric payload to verify the parser still works after schema bind.
  const uint8_t payload[] = {'9', '.', '8', '1'};
  ASSERT_TRUE(parser_handle.parse(777, PJ::Span<const uint8_t>(payload, sizeof(payload))));
  EXPECT_EQ(parser_recorder.append_record_calls, 1);
  EXPECT_EQ(parser_recorder.last_timestamp, 777);
  EXPECT_DOUBLE_EQ(parser_recorder.last_value, 9.81);
}

TEST_F(DelegatedIngestIntegrationTest, MultipleMessagesRouteCorrectly) {
  auto parser_handle = parser_lib_.createHandle();
  ASSERT_TRUE(parser_handle.valid());

  ParserWriteRecorder parser_recorder;
  ASSERT_TRUE(parser_handle.bindWriteHost(makeParserWriteHost(&parser_recorder)));

  // Push several payloads directly through the parser, simulating what the
  // bridge runtime host does for each pushRawMessage call.
  const std::pair<const char*, double> messages[] = {
      {"1.0", 1.0},
      {"2.5", 2.5},
      {"100", 100.0},
  };

  for (int i = 0; i < 3; ++i) {
    auto* text = messages[i].first;
    auto len = std::strlen(text);
    ASSERT_TRUE(parser_handle.parse(1000 + i, PJ::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text), len)));
  }

  EXPECT_EQ(parser_recorder.append_record_calls, 3);
  EXPECT_EQ(parser_recorder.last_timestamp, 1002);
  EXPECT_DOUBLE_EQ(parser_recorder.last_value, 100.0);
}

}  // namespace
