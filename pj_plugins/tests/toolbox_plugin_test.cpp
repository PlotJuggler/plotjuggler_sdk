// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/host/toolbox_library.hpp"

#ifndef PJ_MOCK_TOOLBOX_PLUGIN_PATH
#error "PJ_MOCK_TOOLBOX_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

namespace {

struct ToolboxState {
  int create_data_source_calls = 0;
  int append_record_calls = 0;
};
struct RuntimeState {
  int notify_data_changed_calls = 0;
};

bool tbCreate(void* ctx, PJ_string_view_t, PJ_data_source_handle_t* out, PJ_error_t*) noexcept {
  auto* s = static_cast<ToolboxState*>(ctx);
  ++s->create_data_source_calls;
  *out = PJ_data_source_handle_t{1};
  return true;
}
bool tbEnsureTopic(void*, PJ_data_source_handle_t, PJ_string_view_t, PJ_topic_handle_t* out, PJ_error_t*) noexcept {
  *out = PJ_topic_handle_t{1};
  return true;
}
bool tbEnsureField(
    void*, PJ_topic_handle_t topic, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out,
    PJ_error_t*) noexcept {
  *out = PJ_field_handle_t{topic, 1};
  return true;
}
bool tbAppendRecord(
    void* ctx, PJ_topic_handle_t, int64_t, const PJ_named_field_value_t*, size_t, PJ_error_t*) noexcept {
  auto* s = static_cast<ToolboxState*>(ctx);
  ++s->append_record_calls;
  return true;
}
bool tbAppendBoundRecord(
    void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t, PJ_error_t*) noexcept {
  return true;
}
bool tbAppendArrowStream(
    void*, PJ_topic_handle_t, struct ArrowArrayStream* stream, PJ_string_view_t, PJ_error_t*) noexcept {
  if (stream != nullptr && stream->release != nullptr) {
    stream->release(stream);
  }
  return true;
}
bool tbCatalog(void*, PJ_catalog_snapshot_t*, PJ_error_t*) noexcept {
  return false;
}
bool tbReadSeriesArrow(void*, PJ_field_handle_t, struct ArrowSchema*, struct ArrowArray*, PJ_error_t*) noexcept {
  return false;
}

PJ_toolbox_host_t makeToolboxHost(ToolboxState* state) {
  static const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .create_data_source = tbCreate,
      .ensure_topic = tbEnsureTopic,
      .ensure_field = tbEnsureField,
      .append_record = tbAppendRecord,
      .append_bound_record = tbAppendBoundRecord,
      .append_arrow_stream = tbAppendArrowStream,
      .acquire_catalog_snapshot = tbCatalog,
      .read_series_arrow = tbReadSeriesArrow,
      // Tail slots — left null because this mock host doesn't exercise the
      // object-topic surface. ToolboxHostView::registerObjectTopic /
      // pushOwnedObject return `unexpected("older host")` for null slots.
      .register_object_topic = nullptr,
      .push_owned_object = nullptr,
  };
  return PJ_toolbox_host_t{.ctx = state, .vtable = &vtable};
}

void rhReportMessage(void*, PJ_toolbox_message_level_t, PJ_string_view_t) noexcept {}
void rhNotifyDataChanged(void* ctx) noexcept {
  auto* s = static_cast<RuntimeState*>(ctx);
  ++s->notify_data_changed_calls;
}

PJ_toolbox_runtime_host_t makeRuntimeHost(RuntimeState* state) {
  static const PJ_toolbox_runtime_host_vtable_t vtable = {
      .protocol_version = 1,
      .struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t),
      .report_message = rhReportMessage,
      .notify_data_changed = rhNotifyDataChanged,
  };
  return PJ_toolbox_runtime_host_t{.ctx = state, .vtable = &vtable};
}

TEST(ToolboxPluginTest, LoadsSharedLibraryAndValidatesVtable) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, static_cast<uint32_t>(PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION));
}

TEST(ToolboxPluginTest, RejectsMissingRequiredVtableSlot) {
  auto library = PJ::ToolboxLibrary::load(PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(library);
  EXPECT_NE(library.error().find("Toolbox vtable missing required slot: on_data_changed"), std::string::npos);
}

TEST(ToolboxPluginTest, BindHostsAndConfigRoundTrip) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  ToolboxState tb_state;
  RuntimeState rt_state;
  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::ToolboxHostService>(makeToolboxHost(&tb_state));
  reg.registerService<PJ::sdk::ToolboxRuntimeHostService>(makeRuntimeHost(&rt_state));

  ASSERT_TRUE(handle.bind(reg.view()));

  ASSERT_TRUE(handle.loadConfig(R"({"key":"value"})"));
  std::string saved;
  ASSERT_TRUE(handle.saveConfig(saved));
  EXPECT_EQ(saved, R"({"key":"value"})");
}

TEST(ToolboxPluginTest, BindFailsWithoutMandatoryServices) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  PJ::ServiceRegistryBuilder empty;
  auto status = handle.bind(empty.view());
  EXPECT_FALSE(status);
}

TEST(ToolboxPluginTest, HandleKeepsSharedLibraryLoadedAfterLibraryObjectDies) {
  std::unique_ptr<PJ::ToolboxHandle> handle;
  {
    auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    handle = std::make_unique<PJ::ToolboxHandle>(library->createHandle());
    ASSERT_TRUE(handle->valid());
  }

  EXPECT_NE(handle->manifest().find("Mock Toolbox"), std::string::npos);
  EXPECT_EQ(handle->capabilities(), 0u);
  handle.reset();
}

TEST(ToolboxPluginTest, ReadTransformWriteFlowAndNotifyDataChanged) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  ToolboxState tb_state;
  RuntimeState rt_state;
  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::ToolboxHostService>(makeToolboxHost(&tb_state));
  reg.registerService<PJ::sdk::ToolboxRuntimeHostService>(makeRuntimeHost(&rt_state));
  ASSERT_TRUE(handle.bind(reg.view()));

  ASSERT_TRUE(handle.loadConfig(R"({"apply_transform":true})"));

  EXPECT_EQ(tb_state.create_data_source_calls, 1);
  EXPECT_EQ(tb_state.append_record_calls, 1);
  EXPECT_EQ(rt_state.notify_data_changed_calls, 1);
}

TEST(ToolboxPluginTest, OnDataChangedReachesPluginAndTriggersNotify) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  ToolboxState tb_state;
  RuntimeState rt_state;
  PJ::ServiceRegistryBuilder reg;
  reg.registerService<PJ::sdk::ToolboxHostService>(makeToolboxHost(&tb_state));
  reg.registerService<PJ::sdk::ToolboxRuntimeHostService>(makeRuntimeHost(&rt_state));
  ASSERT_TRUE(handle.bind(reg.view()));

  handle.onDataChanged();
  handle.onDataChanged();
  EXPECT_EQ(rt_state.notify_data_changed_calls, 2);
}

TEST(ToolboxPluginTest, OnDataChangedIsNoOpWhenHandleInvalid) {
  PJ::ToolboxHandle handle{nullptr};
  EXPECT_FALSE(handle.valid());
  handle.onDataChanged();
}

TEST(ToolboxPluginTest, GetPluginExtensionReturnsKnownIdAndNullForUnknown) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  // Id kept in sync with mock_toolbox.cpp:pj_mock::kMockDiagnosticsExtensionId.
  EXPECT_NE(handle.getPluginExtension("pj.experimental.mock_diagnostics/draft-1"), nullptr);
  EXPECT_EQ(handle.getPluginExtension("pj.nonexistent.v1"), nullptr);
}

}  // namespace
