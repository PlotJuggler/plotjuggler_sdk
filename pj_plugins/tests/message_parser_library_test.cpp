#include "pj_plugins/host/message_parser_library.hpp"

#include <gtest/gtest.h>

#include <string>

#include "pj_base/plugin_data_api.h"

#ifndef PJ_MOCK_JSON_PARSER_PLUGIN_PATH
#error "PJ_MOCK_JSON_PARSER_PLUGIN_PATH must be defined"
#endif

namespace {

struct MinimalParserWriteHost {
  int append_record_calls = 0;
  int64_t last_timestamp = 0;
  double last_value = 0.0;

  static const char* getLastError(void*) { return nullptr; }

  static bool ensureField(void*, PJ_string_view_t, PJ_primitive_type_t,
                           PJ_field_handle_t* out_field) {
    *out_field = PJ_field_handle_t{{1}, 1};
    return true;
  }

  static bool appendRecord(void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields,
                            size_t field_count) {
    auto* self = static_cast<MinimalParserWriteHost*>(ctx);
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

  static bool appendArrowIpc(void*, PJ_bytes_view_t, PJ_string_view_t) { return true; }
};

PJ_parser_write_host_t makeWriteHost(MinimalParserWriteHost* recorder) {
  static const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .get_last_error = MinimalParserWriteHost::getLastError,
      .ensure_field = MinimalParserWriteHost::ensureField,
      .append_record = MinimalParserWriteHost::appendRecord,
      .append_bound_record = MinimalParserWriteHost::appendBoundRecord,
      .append_arrow_ipc = MinimalParserWriteHost::appendArrowIpc,
  };
  return PJ_parser_write_host_t{.ctx = recorder, .vtable = &vtable};
}

TEST(MessageParserLibraryTest, LoadMockPlugin) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, PJ_MESSAGE_PARSER_PROTOCOL_VERSION);
}

TEST(MessageParserLibraryTest, ManifestRoundTrip) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock JSON Parser"), std::string::npos);
  EXPECT_NE(handle.manifest().find("\"encoding\":\"json\""), std::string::npos);
}

TEST(MessageParserLibraryTest, BindAndParse) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  MinimalParserWriteHost recorder;

  ASSERT_TRUE(handle.bindWriteHost(makeWriteHost(&recorder)));

  const uint8_t payload[] = {'3', '.', '1', '4'};
  ASSERT_TRUE(handle.parse(999, payload));

  EXPECT_EQ(recorder.append_record_calls, 1);
  EXPECT_EQ(recorder.last_timestamp, 999);
  EXPECT_DOUBLE_EQ(recorder.last_value, 3.14);
}

TEST(MessageParserLibraryTest, SaveLoadConfig) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();

  // Default config
  EXPECT_EQ(handle.saveConfig(), "{}");

  // Load and save round-trip (mock accepts any config)
  ASSERT_TRUE(handle.loadConfig(R"({"format":"compact"})"));
}

TEST(MessageParserLibraryTest, BindSchemaOptional) {
  auto library = PJ::MessageParserLibrary::load(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  MinimalParserWriteHost recorder;

  ASSERT_TRUE(handle.bindWriteHost(makeWriteHost(&recorder)));

  // Parse works without calling bindSchema
  const uint8_t payload[] = {'7'};
  ASSERT_TRUE(handle.parse(100, payload));
  EXPECT_EQ(recorder.append_record_calls, 1);
  EXPECT_DOUBLE_EQ(recorder.last_value, 7.0);
}

TEST(MessageParserLibraryTest, LoadNonexistentFails) {
  auto result = PJ::MessageParserLibrary::load("/nonexistent_path/fake_plugin.so");
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error().empty());
}

}  // namespace
