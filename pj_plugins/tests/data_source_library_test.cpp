#include "pj_plugins/host/data_source_library.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/data_source_plugin_base.hpp"

#ifndef PJ_MOCK_DATA_SOURCE_PLUGIN_PATH
#error "PJ_MOCK_DATA_SOURCE_PLUGIN_PATH must be defined"
#endif

namespace {

struct MinimalWriteHost {
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

  static bool appendArrowIpc(void*, PJ_topic_handle_t, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }
};

struct MinimalRuntimeHost {
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

  static bool ensureParserBinding(void*, const PJ_parser_binding_request_t*, PJ_parser_binding_handle_t* out_handle) {
    *out_handle = PJ_parser_binding_handle_t{11};
    return true;
  }

  static bool pushRawMessage(void*, PJ_parser_binding_handle_t, int64_t, PJ_bytes_view_t) {
    return true;
  }

  static int showMessageBox(void*, PJ_message_box_type_t, PJ_string_view_t, PJ_string_view_t, int) {
    return PJ_MSG_BTN_OK;
  }

  static const char* listAvailableEncodings(void*) {
    return R"(["json","cbor","protobuf"])";
  }
};

PJ_source_write_host_t makeWriteHost() {
  static const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_source_write_host_vtable_t),
      .get_last_error = MinimalWriteHost::getLastError,
      .ensure_topic = MinimalWriteHost::ensureTopic,
      .ensure_field = MinimalWriteHost::ensureField,
      .append_record = MinimalWriteHost::appendRecord,
      .append_bound_record = MinimalWriteHost::appendBoundRecord,
      .append_arrow_ipc = MinimalWriteHost::appendArrowIpc,
  };
  return PJ_source_write_host_t{.ctx = reinterpret_cast<void*>(0x1), .vtable = &vtable};
}

PJ_data_source_runtime_host_t makeRuntimeHost() {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = MinimalRuntimeHost::getLastError,
      .report_message = MinimalRuntimeHost::reportMessage,
      .progress_start = MinimalRuntimeHost::progressStart,
      .progress_update = MinimalRuntimeHost::progressUpdate,
      .progress_finish = MinimalRuntimeHost::progressFinish,
      .is_stop_requested = MinimalRuntimeHost::isStopRequested,
      .notify_state = MinimalRuntimeHost::notifyState,
      .request_stop = MinimalRuntimeHost::requestStop,
      .ensure_parser_binding = MinimalRuntimeHost::ensureParserBinding,
      .push_raw_message = MinimalRuntimeHost::pushRawMessage,
      .show_message_box = MinimalRuntimeHost::showMessageBox,
      .list_available_encodings = nullptr,
  };
  return PJ_data_source_runtime_host_t{.ctx = reinterpret_cast<void*>(0x2), .vtable = &vtable};
}

PJ_data_source_runtime_host_t makeRuntimeHostWithEncodings() {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = MinimalRuntimeHost::getLastError,
      .report_message = MinimalRuntimeHost::reportMessage,
      .progress_start = MinimalRuntimeHost::progressStart,
      .progress_update = MinimalRuntimeHost::progressUpdate,
      .progress_finish = MinimalRuntimeHost::progressFinish,
      .is_stop_requested = MinimalRuntimeHost::isStopRequested,
      .notify_state = MinimalRuntimeHost::notifyState,
      .request_stop = MinimalRuntimeHost::requestStop,
      .ensure_parser_binding = MinimalRuntimeHost::ensureParserBinding,
      .push_raw_message = MinimalRuntimeHost::pushRawMessage,
      .show_message_box = MinimalRuntimeHost::showMessageBox,
      .list_available_encodings = MinimalRuntimeHost::listAvailableEncodings,
  };
  return PJ_data_source_runtime_host_t{.ctx = reinterpret_cast<void*>(0x3), .vtable = &vtable};
}

// Make a runtime host with a smaller struct_size to simulate an older host
PJ_data_source_runtime_host_t makeOldRuntimeHostWithoutEncodings() {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      // Lie about struct_size to simulate an older host without list_available_encodings
      .struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, list_available_encodings),
      .get_last_error = MinimalRuntimeHost::getLastError,
      .report_message = MinimalRuntimeHost::reportMessage,
      .progress_start = MinimalRuntimeHost::progressStart,
      .progress_update = MinimalRuntimeHost::progressUpdate,
      .progress_finish = MinimalRuntimeHost::progressFinish,
      .is_stop_requested = MinimalRuntimeHost::isStopRequested,
      .notify_state = MinimalRuntimeHost::notifyState,
      .request_stop = MinimalRuntimeHost::requestStop,
      .ensure_parser_binding = MinimalRuntimeHost::ensureParserBinding,
      .push_raw_message = MinimalRuntimeHost::pushRawMessage,
      .show_message_box = MinimalRuntimeHost::showMessageBox,
      .list_available_encodings = MinimalRuntimeHost::listAvailableEncodings,  // ignored due to struct_size
  };
  return PJ_data_source_runtime_host_t{.ctx = reinterpret_cast<void*>(0x4), .vtable = &vtable};
}

TEST(DataSourceLibraryTest, LoadsSharedPluginAndDrivesInstance) {
  auto library = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, PJ_DATA_SOURCE_PROTOCOL_VERSION);

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock DataSource"), std::string::npos);

  ASSERT_TRUE(handle.bindWriteHost(makeWriteHost()));
  ASSERT_TRUE(handle.bindRuntimeHost(makeRuntimeHost()));
  ASSERT_TRUE(handle.loadConfig(R"({"delegated":true})"));
  EXPECT_TRUE(handle.start());
  EXPECT_EQ(handle.currentState(), PJ_DATA_SOURCE_STATE_RUNNING);
  handle.stop();
  EXPECT_EQ(handle.currentState(), PJ_DATA_SOURCE_STATE_STOPPED);
}

// ---------------------------------------------------------------------------
// listAvailableEncodings tests
// ---------------------------------------------------------------------------

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsEmptyWhenNullptr) {
  auto host = makeRuntimeHost();  // has list_available_encodings = nullptr
  PJ::DataSourceRuntimeHostView view(host);

  auto encodings = view.listAvailableEncodings();
  EXPECT_TRUE(encodings.empty());
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsJsonReturnsJsonArray) {
  auto host = makeRuntimeHostWithEncodings();
  PJ::DataSourceRuntimeHostView view(host);

  auto json = view.listAvailableEncodingsJson();
  EXPECT_FALSE(json.empty());
  EXPECT_EQ(json, R"(["json","cbor","protobuf"])");
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsParsedVector) {
  auto host = makeRuntimeHostWithEncodings();
  PJ::DataSourceRuntimeHostView view(host);

  auto encodings = view.listAvailableEncodings();
  ASSERT_EQ(encodings.size(), 3u);
  EXPECT_EQ(encodings[0], "json");
  EXPECT_EQ(encodings[1], "cbor");
  EXPECT_EQ(encodings[2], "protobuf");
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsEmptyForOldHost) {
  // Simulate an older host that doesn't have the list_available_encodings field
  // (struct_size is smaller than the offset of that field)
  auto host = makeOldRuntimeHostWithoutEncodings();
  PJ::DataSourceRuntimeHostView view(host);

  auto encodings = view.listAvailableEncodings();
  EXPECT_TRUE(encodings.empty());
}

TEST(RuntimeHostViewTest, ListAvailableEncodingsReturnsEmptyForInvalidView) {
  PJ::DataSourceRuntimeHostView view;  // default constructed = invalid
  EXPECT_FALSE(view.valid());

  auto encodings = view.listAvailableEncodings();
  EXPECT_TRUE(encodings.empty());
}

}  // namespace
