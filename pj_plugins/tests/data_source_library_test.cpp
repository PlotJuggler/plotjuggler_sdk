// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/data_source_library.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"

#ifndef PJ_MOCK_DATA_SOURCE_PLUGIN_PATH
#error "PJ_MOCK_DATA_SOURCE_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

namespace {

// Fake source-write host (v4: all trampolines noexcept, append_arrow_stream).
bool fwsEnsureTopic(void*, PJ_string_view_t, PJ_topic_handle_t* out, PJ_error_t*) noexcept {
  *out = PJ_topic_handle_t{1};
  return true;
}
bool fwsEnsureField(
    void*, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out,
    PJ_error_t*) noexcept {
  *out = PJ_field_handle_t{topic, 1};
  return true;
}
bool fwsAppendRecord(void*, PJ_topic_handle_t, int64_t, const PJ_named_field_value_t*, uint64_t, PJ_error_t*) noexcept {
  return true;
}
bool fwsAppendBoundRecord(
    void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, uint64_t, PJ_error_t*) noexcept {
  return true;
}
bool fwsAppendArrowStream(
    void*, PJ_topic_handle_t, struct ArrowArrayStream* stream, PJ_string_view_t, PJ_error_t*) noexcept {
  // Stub: consume ownership by releasing the stream (success path contract).
  if (stream != nullptr && stream->release != nullptr) {
    stream->release(stream);
  }
  return true;
}

PJ_source_write_host_t makeSourceWriteHost() {
  static const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .ensure_topic = fwsEnsureTopic,
      .ensure_field = fwsEnsureField,
      .append_record = fwsAppendRecord,
      .append_bound_record = fwsAppendBoundRecord,
      .append_arrow_stream = fwsAppendArrowStream,
  };
  return PJ_source_write_host_t{.ctx = reinterpret_cast<void*>(0x1), .vtable = &vtable};
}

// Fake runtime host (v4: every slot noexcept).
void rhReportMessage(void*, PJ_data_source_message_level_t, PJ_string_view_t) noexcept {}
bool rhProgressStart(void*, PJ_string_view_t, uint64_t, bool, PJ_error_t*) noexcept {
  return true;
}
bool rhProgressUpdate(void*, uint64_t) noexcept {
  return true;
}
void rhProgressFinish(void*) noexcept {}
bool rhIsStopRequested(void*) noexcept {
  return false;
}
void rhNotifyState(void*, PJ_data_source_state_t) noexcept {}
void rhRequestStop(void*, PJ_data_source_state_t, PJ_string_view_t) noexcept {}
bool rhEnsureParserBinding(
    void*, const PJ_parser_binding_request_t*, PJ_parser_binding_handle_t* out, PJ_error_t*) noexcept {
  *out = PJ_parser_binding_handle_t{11};
  return true;
}
bool rhPushRawMessage(void*, PJ_parser_binding_handle_t, int64_t, PJ_bytes_view_t, PJ_error_t*) noexcept {
  return true;
}
int rhShowMessageBox(void*, PJ_message_box_type_t, PJ_string_view_t, PJ_string_view_t, int) noexcept {
  return PJ_MSG_BTN_OK;
}
const char* rhListEncodings(void*) noexcept {
  return R"(["json","cbor","protobuf"])";
}

PJ_data_source_runtime_host_t makeRuntimeHost(bool with_encodings) {
  static const PJ_data_source_runtime_host_vtable_t with_vt = {
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
      .push_raw_message = rhPushRawMessage,
      .show_message_box = rhShowMessageBox,
      .list_available_encodings = rhListEncodings,
      .push_message_v2 = nullptr,
  };
  static const PJ_data_source_runtime_host_vtable_t no_enc_vt = {
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
      .push_raw_message = rhPushRawMessage,
      .show_message_box = rhShowMessageBox,
      .list_available_encodings = nullptr,
      .push_message_v2 = nullptr,
  };
  return PJ_data_source_runtime_host_t{
      .ctx = reinterpret_cast<void*>(0x2),
      .vtable = with_encodings ? &with_vt : &no_enc_vt,
  };
}

TEST(DataSourceLibraryTest, LoadsSharedPluginAndDrivesInstance) {
  auto library = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, PJ_DATA_SOURCE_PROTOCOL_VERSION);

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock DataSource"), std::string::npos);

  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::SourceWriteHostService>(makeSourceWriteHost());
  reg.registerService<PJ::sdk::DataSourceRuntimeHostService>(makeRuntimeHost(false));

  ASSERT_TRUE(handle.bind(reg.view()));
  ASSERT_TRUE(handle.loadConfig(R"({"delegated":true})"));
  EXPECT_TRUE(handle.start());
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kRunning);
  handle.stop();
  EXPECT_EQ(handle.currentState(), PJ::DataSourceState::kStopped);
}

TEST(DataSourceLibraryTest, RejectsMissingRequiredVtableSlot) {
  auto library = PJ::DataSourceLibrary::load(PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(library);
  EXPECT_NE(library.error().find("DataSource vtable missing required slot: start"), std::string::npos);
}

TEST(DataSourceLibraryTest, BindFailsWithEmptyRegistry) {
  auto library = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  ASSERT_TRUE(library);
  auto handle = library->createHandle();

  PJ::ServiceRegistryBuilder empty;
  auto status = handle.bind(empty.view());
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("pj.source_write.v1"), std::string::npos);
}

TEST(DataSourceLibraryTest, HandleKeepsSharedLibraryLoadedAfterLibraryObjectDies) {
  std::unique_ptr<PJ::DataSourceHandle> handle;
  {
    auto library = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    handle = std::make_unique<PJ::DataSourceHandle>(library->createHandle());
    ASSERT_TRUE(handle->valid());
  }

  EXPECT_NE(handle->manifest().find("Mock DataSource"), std::string::npos);
  EXPECT_NE(handle->capabilities(), 0u);
  handle.reset();
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsEmptyWhenNullptr) {
  PJ::DataSourceRuntimeHostView view(makeRuntimeHost(false));
  EXPECT_TRUE(view.listAvailableEncodings().empty());
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsJsonArray) {
  PJ::DataSourceRuntimeHostView view(makeRuntimeHost(true));
  EXPECT_EQ(view.listAvailableEncodings(), R"(["json","cbor","protobuf"])");
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsEmptyForInvalidView) {
  PJ::DataSourceRuntimeHostView view;
  EXPECT_FALSE(view.valid());
  EXPECT_TRUE(view.listAvailableEncodings().empty());
}

}  // namespace
