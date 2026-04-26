#include "pj_base/sdk/message_parser_plugin_base.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/message_parser_protocol.h"

namespace {

// ---------------------------------------------------------------------------
// Inline mock MessageParser plugin
// ---------------------------------------------------------------------------

class MockParser : public PJ::MessageParserPluginBase {
 public:
  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return PJ::okStatus();
  }

  PJ::Status bindSchema(std::string_view type_name, PJ::Span<const uint8_t> schema) override {
    bound_type_name_ = std::string(type_name);
    bound_schema_.assign(schema.begin(), schema.end());
    return PJ::okStatus();
  }

  PJ::Status parse(PJ::Timestamp timestamp_ns, PJ::Span<const uint8_t> payload) override {
    if (!writeHostBound()) {
      return PJ::unexpected(std::string("write host not bound"));
    }
    // Parse payload as a text-encoded double
    std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    double value = std::strtod(text.c_str(), nullptr);

    return writeHost().appendRecord(timestamp_ns, {{.name = "value", .value = value}});
  }

  std::string bound_type_name_;
  std::vector<uint8_t> bound_schema_;

 private:
  std::string config_ = "{}";
};

class ThrowingParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp /*timestamp_ns*/, PJ::Span<const uint8_t> /*payload*/) override {
    throw std::runtime_error("parse exploded");
  }
};

constexpr const char* kMockManifest =
    R"({"id":"mock-parser","name":"Mock Parser","version":"1.0.0","encoding":"json"})";

const PJ_message_parser_vtable_t* mockVtable() {
  static const PJ_message_parser_vtable_t* vt =
      PJ::MessageParserPluginBase::vtableWithCreate([]() -> void* { return new MockParser(); }, kMockManifest);
  return vt;
}

const PJ_message_parser_vtable_t* throwingVtable() {
  static const PJ_message_parser_vtable_t* vt = PJ::MessageParserPluginBase::vtableWithCreate(
      []() -> void* { return new ThrowingParser(); },
      R"({"id":"throwing-parser","name":"Thrower","version":"0.1.0","encoding":"test"})");
  return vt;
}

// ---------------------------------------------------------------------------
// RAII vtable driver (test-local, not exported)
// ---------------------------------------------------------------------------

struct VtableDriver {
  const PJ_message_parser_vtable_t* vt;
  void* ctx;

  explicit VtableDriver(const PJ_message_parser_vtable_t* v) : vt(v), ctx(v->create()) {}
  ~VtableDriver() {
    if (vt != nullptr && ctx != nullptr) {
      vt->destroy(ctx);
    }
  }

  VtableDriver(const VtableDriver&) = delete;
  VtableDriver& operator=(const VtableDriver&) = delete;
};

// ---------------------------------------------------------------------------
// Parser write host recorder
// ---------------------------------------------------------------------------

struct ParserWriteRecorder {
  std::string last_error;
  int ensure_field_calls = 0;
  int append_record_calls = 0;
  int64_t last_timestamp = 0;
  double last_value = 0.0;

  static const char* getLastError(void* ctx) {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    return self->last_error.empty() ? nullptr : self->last_error.c_str();
  }

  static bool ensureField(
      void* ctx, PJ_string_view_t /*field_name*/, PJ_primitive_type_t /*type*/, PJ_field_handle_t* out_field) {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    ++self->ensure_field_calls;
    *out_field = PJ_field_handle_t{{1}, 1};
    return true;
  }

  static bool appendRecord(void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count) {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    ++self->append_record_calls;
    self->last_timestamp = timestamp;
    EXPECT_EQ(field_count, 1U);
    EXPECT_EQ(fields[0].value.type, PJ_PRIMITIVE_TYPE_FLOAT64);
    self->last_value = fields[0].value.data.as_float64;
    return true;
  }

  static bool appendBoundRecord(void*, int64_t, const PJ_bound_field_value_t*, size_t) {
    return true;
  }

  static bool appendArrowIpc(void*, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }
};

PJ_parser_write_host_t makeWriteHost(ParserWriteRecorder* recorder) {
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
// Tests
// ---------------------------------------------------------------------------

TEST(MessageParserPluginBaseTest, ManifestJsonIsStaticConstant) {
  const PJ_message_parser_vtable_t* vt = mockVtable();
  EXPECT_STREQ(vt->manifest_json, kMockManifest);
}

TEST(MessageParserPluginBaseTest, ParseFailsWhenWriteHostNotBound) {
  VtableDriver drv(mockVtable());

  const uint8_t payload[] = {'4', '2'};
  PJ_bytes_view_t bytes = {payload, sizeof(payload)};
  EXPECT_FALSE(drv.vt->parse(drv.ctx, 100, bytes));

  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("write host"), std::string::npos);
}

TEST(MessageParserPluginBaseTest, BindWriteHostRejectsNull) {
  VtableDriver drv(mockVtable());

  PJ_parser_write_host_t null_host = {nullptr, nullptr};
  EXPECT_FALSE(drv.vt->bind_write_host(drv.ctx, null_host));

  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("not bound"), std::string::npos);
}

TEST(MessageParserPluginBaseTest, BindAndParseRoundTrip) {
  VtableDriver drv(mockVtable());
  ParserWriteRecorder recorder;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&recorder)));

  const uint8_t payload[] = {'4', '2', '.', '5'};
  PJ_bytes_view_t bytes = {payload, sizeof(payload)};
  ASSERT_TRUE(drv.vt->parse(drv.ctx, 1000, bytes));

  EXPECT_EQ(recorder.append_record_calls, 1);
  EXPECT_EQ(recorder.last_timestamp, 1000);
  EXPECT_DOUBLE_EQ(recorder.last_value, 42.5);
}

TEST(MessageParserPluginBaseTest, BindSchemaStoresTypeInfo) {
  VtableDriver drv(mockVtable());

  const uint8_t schema_bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
  PJ_string_view_t type_name = {"my.MessageType", 14};
  PJ_bytes_view_t schema = {schema_bytes, sizeof(schema_bytes)};

  ASSERT_TRUE(drv.vt->bind_schema(drv.ctx, type_name, schema));

  auto* mock = static_cast<MockParser*>(drv.ctx);
  EXPECT_EQ(mock->bound_type_name_, "my.MessageType");
  EXPECT_EQ(mock->bound_schema_, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(MessageParserPluginBaseTest, DefaultBindSchemaIsNoOp) {
  // ThrowingParser doesn't override bindSchema — should succeed silently.
  VtableDriver drv(throwingVtable());

  const uint8_t schema_bytes[] = {1, 2, 3};
  PJ_string_view_t type_name = {"foo", 3};
  PJ_bytes_view_t schema = {schema_bytes, sizeof(schema_bytes)};

  EXPECT_TRUE(drv.vt->bind_schema(drv.ctx, type_name, schema));
}

TEST(MessageParserPluginBaseTest, ConfigRoundTrip) {
  VtableDriver drv(mockVtable());

  EXPECT_TRUE(drv.vt->load_config(drv.ctx, R"({"mode":"fast"})"));
  EXPECT_STREQ(drv.vt->save_config(drv.ctx), R"({"mode":"fast"})");
}

TEST(MessageParserPluginBaseTest, ExceptionSafetyInParse) {
  VtableDriver drv(throwingVtable());
  ParserWriteRecorder recorder;

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, makeWriteHost(&recorder)));

  const uint8_t payload[] = {'1'};
  PJ_bytes_view_t bytes = {payload, sizeof(payload)};

  // parse() throws — trampoline catches and returns false + stores error
  EXPECT_FALSE(drv.vt->parse(drv.ctx, 0, bytes));
  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("parse exploded"), std::string::npos);

  // No writes should have happened
  EXPECT_EQ(recorder.append_record_calls, 0);
}

TEST(MessageParserPluginBaseTest, AppendArrowIpcRoutesToWriteHost) {
  VtableDriver drv(mockVtable());

  bool arrow_ipc_called = false;
  std::vector<uint8_t> arrow_bytes;
  std::string arrow_ts_col;

  struct ArrowIpcParserWriteRecorder {
    bool* called;
    std::vector<uint8_t>* bytes;
    std::string* ts_col;

    static const char* getLastError(void*) {
      return nullptr;
    }

    static bool ensureField(void*, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
      *out_field = PJ_field_handle_t{{1}, 1};
      return true;
    }

    static bool appendRecord(void*, int64_t, const PJ_named_field_value_t*, size_t) {
      return true;
    }

    static bool appendBoundRecord(void*, int64_t, const PJ_bound_field_value_t*, size_t) {
      return true;
    }

    static bool appendArrowIpc(void* ctx, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column) {
      auto* self = static_cast<ArrowIpcParserWriteRecorder*>(ctx);
      *self->called = true;
      self->bytes->assign(ipc_stream.data, ipc_stream.data + ipc_stream.size);
      self->ts_col->assign(timestamp_column.data, timestamp_column.size);
      return true;
    }
  };

  ArrowIpcParserWriteRecorder arrow_recorder{&arrow_ipc_called, &arrow_bytes, &arrow_ts_col};
  static const PJ_parser_write_host_vtable_t arrow_vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .get_last_error = ArrowIpcParserWriteRecorder::getLastError,
      .ensure_field = ArrowIpcParserWriteRecorder::ensureField,
      .append_record = ArrowIpcParserWriteRecorder::appendRecord,
      .append_bound_record = ArrowIpcParserWriteRecorder::appendBoundRecord,
      .append_arrow_ipc = ArrowIpcParserWriteRecorder::appendArrowIpc,
  };
  PJ_parser_write_host_t arrow_host = {.ctx = &arrow_recorder, .vtable = &arrow_vtable};

  ASSERT_TRUE(drv.vt->bind_write_host(drv.ctx, arrow_host));

  // Call appendArrowIpc through the raw vtable.
  const uint8_t ipc_data[] = {0x41, 0x52, 0x52, 0x4F, 0x57, 0x31};  // "ARROW1"
  PJ_bytes_view_t ipc_bytes = {ipc_data, sizeof(ipc_data)};
  PJ_string_view_t ts_col = {"_timestamp", 10};
  ASSERT_TRUE(arrow_host.vtable->append_arrow_ipc(arrow_host.ctx, ipc_bytes, ts_col));

  EXPECT_TRUE(arrow_ipc_called);
  EXPECT_EQ(arrow_bytes, (std::vector<uint8_t>{0x41, 0x52, 0x52, 0x4F, 0x57, 0x31}));
  EXPECT_EQ(arrow_ts_col, "_timestamp");
}

TEST(MessageParserPluginBaseTest, NullConfigIsHandledGracefully) {
  VtableDriver drv(mockVtable());
  // nullptr config_json — loadConfig should handle gracefully (not crash)
  EXPECT_TRUE(drv.vt->load_config(drv.ctx, nullptr));
}

}  // namespace
