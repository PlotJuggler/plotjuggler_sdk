// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/widget_library.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "detail/library_loader.hpp"
#include "mock_widget_state.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/host/widget_host_bridge.hpp"
#include "pj_plugins/sdk/widget_plugin_base.hpp"

#ifndef PJ_MOCK_WIDGET_PLUGIN_PATH
#error "PJ_MOCK_WIDGET_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH
#error "PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH must be defined"
#endif

namespace {

const pj_mock::WidgetState* stateOf(const PJ::WidgetHandle& handle) {
  return static_cast<const pj_mock::WidgetState*>(
      handle.getPluginExtension(std::string(pj_mock::kWidgetStateExtensionId)));
}

PJ_widget_value_t numberValue(uint32_t input, uint32_t slot, double time, double number) {
  PJ_widget_value_t value{};
  value.input = input;
  value.slot = slot;
  value.kind = PJ_WIDGET_VALUE_NUMBER;
  value.time = time;
  value.number = number;
  return value;
}

TEST(WidgetLibraryTest, LoadsSharedLibraryAndValidatesVtable) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_TRUE(library->valid());
  const PJ_widget_vtable_t* vt = library->vtable();
  EXPECT_EQ(vt->protocol_version, static_cast<uint32_t>(PJ_WIDGET_PLUGIN_PROTOCOL_VERSION));
  EXPECT_GE(vt->struct_size, PJ_WIDGET_MIN_VTABLE_SIZE);
  EXPECT_EQ(vt->struct_size, sizeof(PJ_widget_vtable_t));
  EXPECT_NE(std::string(vt->manifest_json).find("mock-widget"), std::string::npos);
}

TEST(WidgetLibraryTest, LibraryStaysMappedAfterEverythingIsDestroyed) {
  const PJ_widget_vtable_t* vt = nullptr;
  std::string manifest_before;
  uint32_t qt_minor_before = 0;
  std::string path;
  {
    auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    vt = library->vtable();
    manifest_before = vt->manifest_json;
    qt_minor_before = vt->build_info()->qt_minor;
    path = library->path();
    auto a = library->createHandle("gauge");
    auto b = library->createHandle("gauge");
    ASSERT_TRUE(a) << a.error();
    ASSERT_TRUE(b) << b.error();
  }  // handles, then library, destroyed
  ASSERT_NE(vt, nullptr);
  EXPECT_EQ(std::string(vt->manifest_json), manifest_before);
  ASSERT_NE(vt->build_info(), nullptr);
  EXPECT_EQ(vt->build_info()->qt_minor, qt_minor_before);
#if defined(__linux__)
  std::ifstream maps("/proc/self/maps");
  std::string line;
  bool found = false;
  while (std::getline(maps, line)) {
    if (line.find(path) != std::string::npos) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << path << " is no longer mapped";
#endif
}

TEST(WidgetLibraryTest, BuildInfoIsReadableBeforeCreate) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  const PJ_widget_build_info_t* info = library->buildInfo();
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->struct_size, sizeof(PJ_widget_build_info_t));
  EXPECT_EQ(info->qt_major, 6u);
  EXPECT_EQ(info->qt_minor, 12u);
  EXPECT_EQ(info->qt_patch, 3u);
  EXPECT_EQ(std::string(info->qt_version_str.data, info->qt_version_str.size), "6.12.3");
  const std::string compiler(info->compiler.data, info->compiler.size);
#if defined(_MSC_VER)
  EXPECT_EQ(compiler, "msvc");
#elif defined(__clang__)
  EXPECT_EQ(compiler, "clang");
#else
  EXPECT_EQ(compiler, "gcc");
#endif
#if defined(_DEBUG)
  EXPECT_EQ(info->flags & PJ_WIDGET_BUILD_DEBUG, static_cast<uint32_t>(PJ_WIDGET_BUILD_DEBUG));
#else
  EXPECT_EQ(info->flags & PJ_WIDGET_BUILD_DEBUG, 0u) << "only the MSVC debug runtime sets the bit";
#endif
}

TEST(WidgetLibraryTest, RejectsMissingRequiredVtableSlot) {
  auto library = PJ::WidgetLibrary::load(PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(library);
  EXPECT_NE(library.error().find("Widget vtable missing required slot: build_info"), std::string::npos);
}

TEST(WidgetLibraryTest, UnknownTypeIsRefusedAndKnownTypesAreIndependent) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_FALSE(library->createHandle("no_such_widget"));

  auto first = library->createHandle("gauge");
  auto second = library->createHandle("gauge");
  ASSERT_TRUE(first) << first.error();
  ASSERT_TRUE(second) << second.error();
  EXPECT_NE(first->context(), second->context());
}

TEST(WidgetLibraryTest, InputsRoundTripPerType) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();

  auto gauge = library->createHandle("gauge");
  ASSERT_TRUE(gauge) << gauge.error();
  auto gauge_inputs = gauge->inputs();
  ASSERT_TRUE(gauge_inputs) << gauge_inputs.error();
  ASSERT_EQ(gauge_inputs->size(), 1u);
  EXPECT_EQ((*gauge_inputs)[0].name, "value");
  EXPECT_EQ((*gauge_inputs)[0].label, "Value");
  EXPECT_EQ((*gauge_inputs)[0].unit, "V");
  EXPECT_EQ((*gauge_inputs)[0].kind, PJ::WidgetInputKind::kNumber);
  EXPECT_FALSE((*gauge_inputs)[0].multiple);

  auto table = library->createHandle("table");
  ASSERT_TRUE(table) << table.error();
  auto table_inputs = table->inputs();
  ASSERT_TRUE(table_inputs) << table_inputs.error();
  ASSERT_EQ(table_inputs->size(), 1u);
  EXPECT_EQ((*table_inputs)[0].name, "series");
  EXPECT_TRUE((*table_inputs)[0].multiple);
}

TEST(WidgetLibraryTest, InputArrayCarriesMultipleAsAFlagBit) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  for (const auto& [type, flags] :
       {std::pair<const char*, uint32_t>{"gauge", PJ_WIDGET_INPUT_FLAG_NONE},
        std::pair<const char*, uint32_t>{"table", PJ_WIDGET_INPUT_MULTIPLE}}) {
    auto handle = library->createHandle(type);
    ASSERT_TRUE(handle) << handle.error();
    const PJ_widget_input_t* array = nullptr;
    uint32_t count = 0;
    PJ_error_t err{};
    ASSERT_TRUE(handle->vtable()->inputs(handle->context(), &array, &count, &err)) << err.message;
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(array[0].flags, flags) << type;
  }
}

TEST(WidgetLibraryTest, CreateQWidgetPassesParentAndReturnsPluginOwnedPointer) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  int fake_parent = 0;
  auto widget = handle->createQWidget(&fake_parent);
  ASSERT_TRUE(widget) << widget.error();
  EXPECT_NE(*widget, nullptr);
  EXPECT_EQ(stateOf(*handle)->parent, &fake_parent);
  EXPECT_EQ(stateOf(*handle)->widgets_created, 1u);
}

TEST(WidgetLibraryTest, OnUpdateDeliversValuesAndTime) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("table");
  ASSERT_TRUE(handle) << handle.error();

  std::vector<PJ_widget_value_t> values = {numberValue(0, 0, 41.5, 12.6), numberValue(0, 1, 42.0, 3.0)};
  handle->onUpdate(values, 42.1);
  const auto* state = stateOf(*handle);
  EXPECT_EQ(state->updates, 1u);
  EXPECT_EQ(state->last_count, 2u);
  EXPECT_DOUBLE_EQ(state->last_time, 42.1);
  EXPECT_DOUBLE_EQ(state->last_number, 12.6);
  EXPECT_EQ(state->last_kind, PJ_WIDGET_VALUE_NUMBER);

  handle->onUpdate({}, 43.0);
  EXPECT_EQ(state->updates, 2u);
  EXPECT_EQ(state->last_count, 0u);
  EXPECT_EQ(state->last_kind, PJ_WIDGET_VALUE_NONE);
}

TEST(WidgetLibraryTest, ConfigRoundTripAndRejection) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  ASSERT_TRUE(handle->loadConfig(R"({"min":5,"mode":"bar"})"));
  std::string saved;
  ASSERT_TRUE(handle->saveConfig(saved));
  EXPECT_EQ(saved, R"({"min":5,"mode":"bar"})");

  auto rejected = handle->loadConfig(R"({"reject":true})");
  ASSERT_FALSE(rejected);
  EXPECT_NE(rejected.error().find("config rejected"), std::string::npos);
}

TEST(WidgetLibraryTest, BindFailsWithoutHostService) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  PJ::ServiceRegistryBuilder empty;
  EXPECT_FALSE(handle->bind(empty.view()));
}

TEST(WidgetLibraryTest, SettingChangeReconcilesInputsThroughHostService) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  int inputs_changed_calls = 0;
  std::vector<std::string> messages;
  PJ::WidgetHostBridge bridge;
  bridge.inputs_changed = [&] { ++inputs_changed_calls; };
  bridge.report_message = [&](PJ::WidgetMessageLevel, std::string_view message) { messages.emplace_back(message); };
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
  ASSERT_TRUE(handle->bind(registry.view()));

  auto before = handle->inputs();
  ASSERT_TRUE(before) << before.error();
  ASSERT_EQ(before->size(), 1u);
  ASSERT_TRUE(handle->loadConfig(R"({"extra_input":true})"));
  EXPECT_EQ(inputs_changed_calls, 1);
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0], "inputs changed");

  auto inputs = handle->inputs();
  ASSERT_TRUE(inputs) << inputs.error();
  ASSERT_EQ(inputs->size(), 2u);
  EXPECT_EQ((*inputs)[0].name, "value");  // a surviving name keeps its identity
  EXPECT_EQ((*inputs)[1].name, "state");
  EXPECT_EQ((*inputs)[1].kind, PJ::WidgetInputKind::kText);
}

TEST(WidgetLibraryTest, HistoryDeliversThroughSinkInsideTheCall) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  PJ::WidgetHistoryQuery seen;
  PJ::WidgetHostBridge bridge;
  bridge.history = [&](const PJ::WidgetHistoryQuery& query, const PJ::WidgetValueSink& sink) -> PJ::Expected<uint32_t> {
    seen = query;
    for (double t : {1.0, 2.0, 3.0}) {
      if (!sink(numberValue(query.input, query.slot, t, t * 10.0))) {
        break;
      }
    }
    return 3u;
  };
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
  ASSERT_TRUE(handle->bind(registry.view()));
  ASSERT_TRUE(handle->loadConfig(R"({"history":true})"));

  std::vector<PJ_widget_value_t> values = {numberValue(0, 0, 3.0, 30.0)};
  handle->onUpdate(values, 3.0);
  const auto* state = stateOf(*handle);
  EXPECT_TRUE(state->history_ok);
  EXPECT_EQ(state->history_samples, 3u);
  EXPECT_EQ(state->history_total, 3u);
  EXPECT_DOUBLE_EQ(state->history_last, 30.0);
  EXPECT_EQ(seen.input, 0u);
  EXPECT_DOUBLE_EQ(seen.to, 3.0);
  EXPECT_EQ(seen.max_samples, 100u);
}

TEST(WidgetLibraryTest, HistoryReportsTheTotalBeforeTheCap) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  // The range holds 1000 samples; the host decimates to the plugin's cap of 100.
  PJ::WidgetHostBridge bridge;
  bridge.history = [&](const PJ::WidgetHistoryQuery& query, const PJ::WidgetValueSink& sink) -> PJ::Expected<uint32_t> {
    for (uint32_t i = 0; i < query.max_samples; ++i) {
      if (!sink(numberValue(query.input, query.slot, i * 10.0, i))) {
        break;
      }
    }
    return 1000u;
  };
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
  ASSERT_TRUE(handle->bind(registry.view()));
  ASSERT_TRUE(handle->loadConfig(R"({"history":true})"));

  std::vector<PJ_widget_value_t> values = {numberValue(0, 0, 3.0, 30.0)};
  handle->onUpdate(values, 3.0);
  const auto* state = stateOf(*handle);
  EXPECT_TRUE(state->history_ok);
  EXPECT_EQ(state->history_samples, 100u);
  EXPECT_EQ(state->history_total, 1000u);
}

TEST(WidgetLibraryTest, HistorySinkStopsTheDeliveryEvenIfTheHostKeepsGoing) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  int sink_calls = 0;
  int sink_continues = 0;
  PJ::WidgetHostBridge bridge;
  // A sloppy host that ignores the sink's false: the bridge must still stop calling the plugin.
  bridge.history = [&](const PJ::WidgetHistoryQuery& query, const PJ::WidgetValueSink& sink) -> PJ::Expected<uint32_t> {
    for (double t : {1.0, 2.0, 3.0, 4.0}) {
      ++sink_calls;
      sink_continues += sink(numberValue(query.input, query.slot, t, t)) ? 1 : 0;
    }
    return 4u;
  };
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
  ASSERT_TRUE(handle->bind(registry.view()));
  ASSERT_TRUE(handle->loadConfig(R"({"history":true,"history_stop":true})"));

  std::vector<PJ_widget_value_t> values = {numberValue(0, 0, 4.0, 4.0)};
  handle->onUpdate(values, 4.0);
  const auto* state = stateOf(*handle);
  EXPECT_TRUE(state->history_ok) << "stopping early is a success";
  EXPECT_EQ(state->history_samples, 1u) << "the plugin callback ran after it asked to stop";
  EXPECT_DOUBLE_EQ(state->history_last, 1.0);
  EXPECT_EQ(state->history_total, 4u);
  EXPECT_EQ(sink_calls, 4);
  EXPECT_EQ(sink_continues, 0);
}

TEST(WidgetLibraryTest, HistoryOutTotalMayBeNullAndIsZeroOnFailure) {
  PJ::WidgetHostBridge bridge;
  bridge.history = [&](const PJ::WidgetHistoryQuery& query, const PJ::WidgetValueSink&) -> PJ::Expected<uint32_t> {
    if (query.input != 0) {
      return PJ::unexpected(std::string("input not wired"));
    }
    return 7u;
  };
  const PJ_widget_host_t host = bridge.host();
  PJ_error_t err{};
  EXPECT_TRUE(host.vtable->query_history(host.ctx, 0, 0, 0.0, 1.0, 0, nullptr, nullptr, nullptr, &err));

  uint32_t total = 99;
  EXPECT_TRUE(host.vtable->query_history(host.ctx, 0, 0, 0.0, 1.0, 0, nullptr, nullptr, &total, &err));
  EXPECT_EQ(total, 7u);

  total = 99;
  EXPECT_FALSE(host.vtable->query_history(host.ctx, 1, 0, 0.0, 1.0, 0, nullptr, nullptr, &total, &err));
  EXPECT_EQ(total, 0u);
  EXPECT_NE(std::string(err.message).find("input not wired"), std::string::npos);
}

TEST(WidgetLibraryTest, HistoryFailureReachesThePlugin) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  PJ::WidgetHostBridge bridge;  // history left unset: the host does not support it
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
  ASSERT_TRUE(handle->bind(registry.view()));
  ASSERT_TRUE(handle->loadConfig(R"({"history":true})"));

  std::vector<PJ_widget_value_t> values = {numberValue(0, 0, 3.0, 30.0)};
  handle->onUpdate(values, 3.0);
  EXPECT_FALSE(stateOf(*handle)->history_ok);
}

// loadFromHandle pins a handle only once it passed validation: a rejected library's handle is released
// with the caller's last reference, an accepted one never is.
std::shared_ptr<void> observedHandle(const char* path, const std::shared_ptr<bool>& released) {
  auto raw = PJ::detail::loadLibraryHandle(path);
  if (!raw) {
    ADD_FAILURE() << raw.error();
    return {};
  }
  return std::shared_ptr<void>(*raw, [released](void* handle) {
    *released = true;
    PJ::detail::closeLibraryHandle(handle);
  });
}

TEST(WidgetLibraryTest, LibraryFailingValidationIsNotPinned) {
  auto released = std::make_shared<bool>(false);
  auto handle = observedHandle(PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH, released);
  ASSERT_NE(handle, nullptr);
  auto library = PJ::WidgetLibrary::loadFromHandle(handle, PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH);
  ASSERT_FALSE(library);
  EXPECT_NE(library.error().find("missing required slot: build_info"), std::string::npos) << library.error();
  handle.reset();
  EXPECT_TRUE(*released) << "a library that failed validation stayed pinned";
}

TEST(WidgetLibraryTest, LibraryPassingValidationIsPinned) {
  auto released = std::make_shared<bool>(false);
  auto handle = observedHandle(PJ_MOCK_WIDGET_PLUGIN_PATH, released);
  ASSERT_NE(handle, nullptr);
  {
    auto library = PJ::WidgetLibrary::loadFromHandle(handle, PJ_MOCK_WIDGET_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
  }
  handle.reset();
  EXPECT_FALSE(*released) << "an accepted library was released";
}

TEST(WidgetLibraryTest, HandleKeepsSharedLibraryLoadedAfterLibraryObjectDies) {
  std::unique_ptr<PJ::WidgetHandle> handle;
  {
    auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
    ASSERT_TRUE(library) << library.error();
    auto created = library->createHandle("gauge");
    ASSERT_TRUE(created) << created.error();
    handle = std::make_unique<PJ::WidgetHandle>(std::move(*created));
  }
  ASSERT_TRUE(handle->valid());
  EXPECT_NE(handle->manifest().find("Mock Widget"), std::string::npos);
  handle->reset();
  EXPECT_FALSE(handle->valid());
  handle->reset();  // idempotent
}

TEST(WidgetLibraryTest, GetPluginExtensionReturnsNullForUnknownId) {
  auto library = PJ::WidgetLibrary::load(PJ_MOCK_WIDGET_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();
  EXPECT_NE(stateOf(*handle), nullptr);
  EXPECT_EQ(handle->getPluginExtension("pj.nonexistent.v1"), nullptr);
}

}  // namespace
