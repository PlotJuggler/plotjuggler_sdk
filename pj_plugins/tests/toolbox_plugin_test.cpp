#include <gtest/gtest.h>

#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/toolbox_plugin_base.hpp"
#include "pj_plugins/host/toolbox_library.hpp"

#ifndef PJ_MOCK_TOOLBOX_PLUGIN_PATH
#error "PJ_MOCK_TOOLBOX_PLUGIN_PATH must be defined"
#endif

namespace {

struct MinimalToolboxHost {
  int create_data_source_calls = 0;
  int append_record_calls = 0;

  static const char* getLastError(void*) {
    return nullptr;
  }

  static bool createDataSource(void* ctx, PJ_string_view_t, PJ_data_source_handle_t* out_source) {
    auto* self = static_cast<MinimalToolboxHost*>(ctx);
    ++self->create_data_source_calls;
    *out_source = PJ_data_source_handle_t{1};
    return true;
  }

  static bool ensureTopic(void*, PJ_data_source_handle_t, PJ_string_view_t, PJ_topic_handle_t* out_topic) {
    *out_topic = PJ_topic_handle_t{1};
    return true;
  }

  static bool ensureField(
      void*, PJ_topic_handle_t, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field) {
    *out_field = PJ_field_handle_t{PJ_topic_handle_t{1}, 1};
    return true;
  }

  static bool appendRecord(void* ctx, PJ_topic_handle_t, int64_t, const PJ_named_field_value_t*, size_t) {
    auto* self = static_cast<MinimalToolboxHost*>(ctx);
    ++self->append_record_calls;
    return true;
  }

  static bool appendBoundRecord(void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t) {
    return true;
  }

  static bool appendArrowIpc(void*, PJ_topic_handle_t, PJ_bytes_view_t, PJ_string_view_t) {
    return true;
  }

  static bool acquireCatalogSnapshot(void*, PJ_catalog_snapshot_t*) {
    return false;
  }

  static bool readSeries(void*, PJ_field_handle_t, PJ_materialized_series_t*) {
    return false;
  }
};

struct MinimalRuntimeHost {
  int notify_data_changed_calls = 0;

  static const char* getLastError(void*) {
    return nullptr;
  }

  static void reportMessage(void*, PJ_toolbox_message_level_t, PJ_string_view_t) {}

  static void notifyDataChanged(void* ctx) {
    auto* self = static_cast<MinimalRuntimeHost*>(ctx);
    ++self->notify_data_changed_calls;
  }
};

PJ_toolbox_host_t makeToolboxHost(MinimalToolboxHost* recorder) {
  static const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .get_last_error = MinimalToolboxHost::getLastError,
      .create_data_source = MinimalToolboxHost::createDataSource,
      .ensure_topic = MinimalToolboxHost::ensureTopic,
      .ensure_field = MinimalToolboxHost::ensureField,
      .append_record = MinimalToolboxHost::appendRecord,
      .append_bound_record = MinimalToolboxHost::appendBoundRecord,
      .append_arrow_ipc = MinimalToolboxHost::appendArrowIpc,
      .acquire_catalog_snapshot = MinimalToolboxHost::acquireCatalogSnapshot,
      .read_series = MinimalToolboxHost::readSeries,
  };
  return PJ_toolbox_host_t{.ctx = recorder, .vtable = &vtable};
}

PJ_toolbox_runtime_host_t makeRuntimeHost(MinimalRuntimeHost* recorder) {
  static const PJ_toolbox_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t),
      .get_last_error = MinimalRuntimeHost::getLastError,
      .report_message = MinimalRuntimeHost::reportMessage,
      .notify_data_changed = MinimalRuntimeHost::notifyDataChanged,
  };
  return PJ_toolbox_runtime_host_t{.ctx = recorder, .vtable = &vtable};
}

TEST(ToolboxPluginTest, LoadsSharedLibraryAndValidatesVtable) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  EXPECT_EQ(library->vtable()->protocol_version, static_cast<uint32_t>(PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION));
  EXPECT_GE(library->vtable()->struct_size, sizeof(PJ_toolbox_vtable_t));
}

TEST(ToolboxPluginTest, CreatesHandleAndVerifiesManifest) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto handle = library->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.manifest().find("Mock Toolbox"), std::string::npos);
}

TEST(ToolboxPluginTest, BindHostsAndConfigRoundTrip) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  MinimalToolboxHost toolbox_recorder;
  MinimalRuntimeHost runtime_recorder;

  ASSERT_TRUE(handle.bindToolboxHost(makeToolboxHost(&toolbox_recorder)));
  ASSERT_TRUE(handle.bindRuntimeHost(makeRuntimeHost(&runtime_recorder)));

  ASSERT_TRUE(handle.loadConfig(R"({"key":"value"})"));
  EXPECT_EQ(handle.saveConfig(), R"({"key":"value"})");
}

TEST(ToolboxPluginTest, DialogContextNonNullWhenHasDialog) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  EXPECT_NE(handle.capabilities() & PJ_TOOLBOX_CAPABILITY_HAS_DIALOG, 0u);
  EXPECT_NE(handle.dialogContext(), nullptr);
}

TEST(ToolboxPluginTest, BindRejectsNullHosts) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  EXPECT_FALSE(handle.bindToolboxHost(PJ_toolbox_host_t{}));
  EXPECT_FALSE(handle.bindRuntimeHost(PJ_toolbox_runtime_host_t{}));
}

TEST(ToolboxPluginTest, ReadTransformWriteFlowAndNotifyDataChanged) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  MinimalToolboxHost toolbox_recorder;
  MinimalRuntimeHost runtime_recorder;

  ASSERT_TRUE(handle.bindToolboxHost(makeToolboxHost(&toolbox_recorder)));
  ASSERT_TRUE(handle.bindRuntimeHost(makeRuntimeHost(&runtime_recorder)));

  // Loading config with "apply_transform" triggers the data-plane flow
  ASSERT_TRUE(handle.loadConfig(R"({"apply_transform":true})"));

  EXPECT_EQ(toolbox_recorder.create_data_source_calls, 1);
  EXPECT_EQ(toolbox_recorder.append_record_calls, 1);
  EXPECT_EQ(runtime_recorder.notify_data_changed_calls, 1);
}

TEST(ToolboxPluginTest, OnDataChangedReachesPluginAndTriggersNotify) {
  auto library = PJ::ToolboxLibrary::load(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();

  MinimalRuntimeHost runtime_recorder;
  ASSERT_TRUE(handle.bindRuntimeHost(makeRuntimeHost(&runtime_recorder)));

  const auto required = offsetof(PJ_toolbox_vtable_t, on_data_changed) + sizeof(void*);
  EXPECT_GE(library->vtable()->struct_size, required);
  EXPECT_NE(library->vtable()->on_data_changed, nullptr);

  handle.onDataChanged();
  handle.onDataChanged();
  EXPECT_EQ(runtime_recorder.notify_data_changed_calls, 2);
}

TEST(ToolboxPluginTest, OnDataChangedIsNoOpWhenHandleInvalid) {
  PJ::ToolboxHandle handle{nullptr};
  EXPECT_FALSE(handle.valid());
  handle.onDataChanged();  // Must not crash.
}

// Exception safety: use vtableWithCreate directly to test trampoline catch paths.
namespace {

class ThrowingToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    throw std::runtime_error("capabilities exploded");
  }
  std::string saveConfig() const override {
    throw std::runtime_error("save exploded");
  }
  PJ::Status loadConfig(std::string_view) override {
    throw std::runtime_error("load exploded");
  }
  void* dialogContext() override {
    throw std::runtime_error("dialog exploded");
  }
};

const PJ_toolbox_vtable_t* throwingVtable() {
  static const PJ_toolbox_vtable_t* vt = PJ::ToolboxPluginBase::vtableWithCreate(
      []() -> void* { return new ThrowingToolbox(); }, R"({"name":"Thrower","version":"0.0.1"})");
  return vt;
}

struct VtableDriver {
  const PJ_toolbox_vtable_t* vt;
  void* ctx;

  explicit VtableDriver(const PJ_toolbox_vtable_t* vtable) : vt(vtable), ctx(vt->create()) {}
  ~VtableDriver() {
    vt->destroy(ctx);
  }
  VtableDriver(const VtableDriver&) = delete;
  VtableDriver& operator=(const VtableDriver&) = delete;
};

}  // namespace

TEST(ToolboxPluginTest, ExceptionsSafelyCaughtAcrossAbi) {
  VtableDriver drv(throwingVtable());

  // capabilities: exception → returns 0
  EXPECT_EQ(drv.vt->capabilities(drv.ctx), 0u);
  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("capabilities exploded"), std::string::npos);

  // save_config: exception → returns "{}"
  EXPECT_STREQ(drv.vt->save_config(drv.ctx), "{}");

  // load_config: exception → returns false
  EXPECT_FALSE(drv.vt->load_config(drv.ctx, "{}"));

  // get_dialog_context: exception → returns nullptr
  EXPECT_EQ(drv.vt->get_dialog_context(drv.ctx), nullptr);
}

namespace {

class ThrowingOnDataChanged : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;
  }
  void onDataChanged() override {
    throw std::runtime_error("on_data_changed exploded");
  }
};

const PJ_toolbox_vtable_t* throwingOnDataChangedVtable() {
  static const PJ_toolbox_vtable_t* vt = PJ::ToolboxPluginBase::vtableWithCreate(
      []() -> void* { return new ThrowingOnDataChanged(); },
      R"({"name":"ThrowOnDataChanged","version":"0.0.1"})");
  return vt;
}

}  // namespace

TEST(ToolboxPluginTest, OnDataChangedExceptionsSafelyCaught) {
  VtableDriver drv(throwingOnDataChangedVtable());
  ASSERT_NE(drv.vt->on_data_changed, nullptr);
  drv.vt->on_data_changed(drv.ctx);  // Must not propagate.

  const char* err = drv.vt->get_last_error(drv.ctx);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("on_data_changed exploded"), std::string::npos);
}

}  // namespace
