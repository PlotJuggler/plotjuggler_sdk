// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// examples/mock_widget.cpp is compiled into this executable with PJ_STATIC_PLUGINS,
// so PJ_WIDGET_PLUGIN emits pj_static_get_widget_vtable_MockWidget instead of the
// dlsym-able entry point. A second plugin, ProbeWidget, is defined below: two static
// widget plugins in one binary must each get their own vtable.

// Stand-ins for the toolkit version macros a real widget plugin gets from its toolkit headers.
#ifndef QT_VERSION_MAJOR
#define QT_VERSION_MAJOR 6
#define QT_VERSION_MINOR 12
#define QT_VERSION_PATCH 3
#define QT_VERSION_STR "6.12.3"
#endif

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "mock_widget_state.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/host/widget_host_bridge.hpp"
#include "pj_plugins/host/widget_library.hpp"
#include "pj_plugins/sdk/widget_plugin_base.hpp"

const PJ_widget_vtable_t* pj_static_get_widget_vtable_MockWidget() noexcept;
const PJ_widget_vtable_t* pj_static_get_widget_vtable_ProbeWidget() noexcept;

namespace {

/// A widget whose behaviour the tests switch through its public members (reached via
/// WidgetHandle::context(), which is the instance itself).
class ProbeWidget : public PJ::WidgetPluginBase {
 public:
  std::vector<PJ::WidgetInput> inputs() const override {
    if (throw_in_inputs) {
      throw std::runtime_error("inputs exploded");
    }
    return next_inputs;
  }

  void* createWidget(void* /*parent*/) override {
    return this;
  }

  void onUpdate(PJ::Span<const PJ_widget_value_t> /*values*/, double time) override {
    if (throw_in_update) {
      throw std::runtime_error("update exploded");
    }
    if (query_with_throwing_callback) {
      auto total = widgetHost().queryHistory(0, 0, 0.0, time, 0, [this](const PJ_widget_value_t&) {
        ++history_callback_calls;
        throw std::runtime_error("callback exploded");
      });
      history_error = total ? std::string() : total.error();
    }
  }

  bool throw_in_inputs = false;
  bool throw_in_update = false;
  bool query_with_throwing_callback = false;
  int history_callback_calls = 0;
  std::string history_error;
  std::vector<PJ::WidgetInput> next_inputs = {PJ::WidgetInput("a", "A"), PJ::WidgetInput("b", "B")};
};

constexpr const char* kProbeManifest =
    R"({"id":"probe-widget","name":"Probe Widget","version":"1.0.0","widgets":[{"type":"probe","name":"Probe"}]})";

}  // namespace

PJ_WIDGET_PLUGIN_NAMED(ProbeWidget, ProbeWidget, kProbeManifest)

namespace {

ProbeWidget& probeOf(const PJ::WidgetHandle& handle) {
  return *static_cast<ProbeWidget*>(static_cast<PJ::WidgetPluginBase*>(handle.context()));
}

PJ::WidgetHandle makeProbe() {
  auto library = PJ::WidgetLibrary::loadStatic(pj_static_get_widget_vtable_ProbeWidget());
  EXPECT_TRUE(library) << library.error();
  auto handle = library->createHandle("probe");
  EXPECT_TRUE(handle) << handle.error();
  return std::move(*handle);
}

PJ_widget_input_t g_corrupt_input{};
constexpr std::size_t kImpossibleLength = std::numeric_limits<std::size_t>::max() - 1;

// A plugin whose string views no std::string could hold: the handle must refuse them before any
// pointer arithmetic, so that a corrupt plugin is reported instead of read out of bounds.
bool corruptInputs(
    void* /*ctx*/, const PJ_widget_input_t** out_inputs, uint32_t* out_count, PJ_error_t* /*out_error*/) noexcept {
  g_corrupt_input.name = PJ_string_view_t{"x", kImpossibleLength};
  g_corrupt_input.kind = PJ_WIDGET_INPUT_NUMBER;
  *out_inputs = &g_corrupt_input;
  *out_count = 1;
  return true;
}

// Inputs written directly at the C level, for shapes the C++ base cannot produce (flag bits).
std::vector<PJ_widget_input_t> g_raw_inputs;
bool rawInputs(
    void* /*ctx*/, const PJ_widget_input_t** out_inputs, uint32_t* out_count, PJ_error_t* /*out_error*/) noexcept {
  *out_inputs = g_raw_inputs.data();
  *out_count = static_cast<uint32_t>(g_raw_inputs.size());
  return true;
}

PJ_widget_input_t rawInput(const char* name, uint32_t kind, uint32_t flags) {
  PJ_widget_input_t input{};
  input.name = PJ_string_view_t{name, std::char_traits<char>::length(name)};
  input.kind = static_cast<PJ_widget_input_kind_t>(kind);
  input.flags = flags;
  return input;
}

bool corruptSaveConfig(void* /*ctx*/, PJ_string_view_t* out_json, PJ_error_t* /*out_error*/) noexcept {
  *out_json = PJ_string_view_t{"{}", kImpossibleLength};
  return true;
}

TEST(WidgetStaticLibraryTest, HandleRefusesViewsNoPluginCanProduce) {
  PJ_widget_vtable_t corrupt = *pj_static_get_widget_vtable_MockWidget();
  corrupt.inputs = &corruptInputs;
  corrupt.save_config = &corruptSaveConfig;
  auto library = PJ::WidgetLibrary::loadStatic(&corrupt);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  auto inputs = handle->inputs();
  ASSERT_FALSE(inputs);
  EXPECT_NE(inputs.error().find("invalid input name string view"), std::string::npos) << inputs.error();
  std::string json;
  EXPECT_THROW((void)handle->saveConfig(json), std::invalid_argument);
}

TEST(WidgetStaticLibraryTest, LoadStaticWrapsTheVtableWithoutDlopen) {
  auto library = PJ::WidgetLibrary::loadStatic(pj_static_get_widget_vtable_MockWidget());
  ASSERT_TRUE(library) << library.error();
  EXPECT_EQ(library->path(), "static://");

  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();
  auto inputs = handle->inputs();
  ASSERT_TRUE(inputs) << inputs.error();
  ASSERT_EQ(inputs->size(), 1u);
  auto widget = handle->createQWidget(nullptr);
  ASSERT_TRUE(widget) << widget.error();
  EXPECT_NE(*widget, nullptr);
  EXPECT_FALSE(library->createHandle("no_such_widget"));
}

TEST(WidgetStaticLibraryTest, LoadStaticRejectsNullAndWrongProtocol) {
  EXPECT_FALSE(PJ::WidgetLibrary::loadStatic(nullptr));

  PJ_widget_vtable_t bad = *pj_static_get_widget_vtable_MockWidget();
  bad.protocol_version = PJ_WIDGET_PLUGIN_PROTOCOL_VERSION + 1;
  EXPECT_FALSE(PJ::WidgetLibrary::loadStatic(&bad));

  PJ_widget_vtable_t small = *pj_static_get_widget_vtable_MockWidget();
  small.struct_size = static_cast<uint32_t>(PJ_WIDGET_MIN_VTABLE_SIZE - 1);
  EXPECT_FALSE(PJ::WidgetLibrary::loadStatic(&small));

  PJ_widget_vtable_t incomplete = *pj_static_get_widget_vtable_MockWidget();
  incomplete.inputs = nullptr;
  auto result = PJ::WidgetLibrary::loadStatic(&incomplete);
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("missing required slot: inputs"), std::string::npos);
}

// --- Several static plugins in one binary -----------------------------------

TEST(WidgetStaticLibraryTest, EachStaticPluginGetsItsOwnVtable) {
  const PJ_widget_vtable_t* mock = pj_static_get_widget_vtable_MockWidget();
  const PJ_widget_vtable_t* probe = pj_static_get_widget_vtable_ProbeWidget();
  ASSERT_NE(mock, nullptr);
  ASSERT_NE(probe, nullptr);
  EXPECT_NE(mock, probe);
  EXPECT_NE(std::string(mock->manifest_json), std::string(probe->manifest_json));
  EXPECT_NE(std::string(mock->manifest_json).find("mock-widget"), std::string::npos);
  EXPECT_NE(std::string(probe->manifest_json).find("probe-widget"), std::string::npos);
  EXPECT_NE(mock->create, probe->create);

  auto mock_library = PJ::WidgetLibrary::loadStatic(mock);
  ASSERT_TRUE(mock_library) << mock_library.error();
  EXPECT_TRUE(mock_library->createHandle("gauge"));
  auto probe_handle = makeProbe();
  ASSERT_TRUE(probe_handle.valid());
  EXPECT_EQ(probeOf(probe_handle).next_inputs.size(), 2u);
}

// --- inputs() error channel ---------------------------------------------------

TEST(WidgetStaticLibraryTest, ThrowingInputsFailsAndKeepsThePreviousArray) {
  auto handle = makeProbe();
  ASSERT_TRUE(handle.valid());
  const PJ_widget_vtable_t* vt = handle.vtable();

  const PJ_widget_input_t* first = nullptr;
  uint32_t first_count = 0;
  PJ_error_t err{};
  ASSERT_TRUE(vt->inputs(handle.context(), &first, &first_count, &err)) << err.message;
  ASSERT_EQ(first_count, 2u);

  probeOf(handle).throw_in_inputs = true;
  const PJ_widget_input_t* second = nullptr;
  uint32_t second_count = 77;
  PJ_error_t failure{};
  EXPECT_FALSE(vt->inputs(handle.context(), &second, &second_count, &failure));
  EXPECT_NE(std::string(failure.message).find("inputs exploded"), std::string::npos) << failure.message;
  EXPECT_EQ(second, nullptr) << "outputs are not written on failure";
  EXPECT_EQ(second_count, 77u);
  // The array of the previous successful call is still readable.
  EXPECT_EQ(std::string(first[0].name.data, first[0].name.size), "a");
  EXPECT_EQ(std::string(first[1].label.data, first[1].label.size), "B");

  auto through_handle = handle.inputs();
  ASSERT_FALSE(through_handle);
  EXPECT_NE(through_handle.error().find("inputs exploded"), std::string::npos) << through_handle.error();

  probeOf(handle).throw_in_inputs = false;
  auto recovered = handle.inputs();
  ASSERT_TRUE(recovered) << recovered.error();
  EXPECT_EQ(recovered->size(), 2u);
}

// --- Host-side validation of plugin data ------------------------------------

TEST(WidgetStaticLibraryTest, HandleRefusesEmptyOrDuplicateInputNames) {
  auto handle = makeProbe();
  ASSERT_TRUE(handle.valid());

  probeOf(handle).next_inputs = {PJ::WidgetInput("a"), PJ::WidgetInput("")};
  auto empty = handle.inputs();
  ASSERT_FALSE(empty);
  EXPECT_NE(empty.error().find("empty name"), std::string::npos) << empty.error();

  probeOf(handle).next_inputs = {PJ::WidgetInput("a"), PJ::WidgetInput("b"), PJ::WidgetInput("a")};
  auto duplicate = handle.inputs();
  ASSERT_FALSE(duplicate);
  EXPECT_NE(duplicate.error().find("'a' twice"), std::string::npos) << duplicate.error();
}

TEST(WidgetStaticLibraryTest, HandleRefusesUnknownKindButIgnoresUnknownFlagBits) {
  PJ_widget_vtable_t raw = *pj_static_get_widget_vtable_MockWidget();
  raw.inputs = &rawInputs;
  auto library = PJ::WidgetLibrary::loadStatic(&raw);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();

  g_raw_inputs = {
      rawInput("value", PJ_WIDGET_INPUT_NUMBER, PJ_WIDGET_INPUT_MULTIPLE | (1u << 7) | (1u << 30)),
      rawInput("single", PJ_WIDGET_INPUT_TEXT, 1u << 5)};
  auto inputs = handle->inputs();
  ASSERT_TRUE(inputs) << inputs.error();
  ASSERT_EQ(inputs->size(), 2u);
  EXPECT_TRUE((*inputs)[0].multiple);
  EXPECT_FALSE((*inputs)[1].multiple);
  EXPECT_EQ((*inputs)[1].kind, PJ::WidgetInputKind::kText);

  g_raw_inputs = {rawInput("value", PJ_WIDGET_INPUT_NUMBER, 0), rawInput("future", 3, 0)};
  auto unknown = handle->inputs();
  ASSERT_FALSE(unknown);
  EXPECT_NE(unknown.error().find("unknown kind 3"), std::string::npos) << unknown.error();

  g_raw_inputs.clear();
}

const PJ_widget_build_info_t* nullBuildInfo() noexcept {
  return nullptr;
}

const PJ_widget_build_info_t* truncatedBuildInfo() noexcept {
  static const PJ_widget_build_info_t info = [] {
    PJ_widget_build_info_t truncated = *pj_static_get_widget_vtable_MockWidget()->build_info();
    truncated.struct_size = static_cast<uint32_t>(offsetof(PJ_widget_build_info_t, qt_version_str));
    return truncated;
  }();
  return &info;
}

const PJ_widget_build_info_t* corruptViewBuildInfo() noexcept {
  static const PJ_widget_build_info_t info = [] {
    PJ_widget_build_info_t corrupt = *pj_static_get_widget_vtable_MockWidget()->build_info();
    corrupt.qt_version_str = PJ_string_view_t{"6.12.3", kImpossibleLength};
    return corrupt;
  }();
  return &info;
}

TEST(WidgetStaticLibraryTest, LoaderRefusesInvalidBuildInfo) {
  ASSERT_TRUE(PJ::WidgetLibrary::loadStatic(pj_static_get_widget_vtable_MockWidget()));

  PJ_widget_vtable_t null_info = *pj_static_get_widget_vtable_MockWidget();
  null_info.build_info = &nullBuildInfo;
  auto null_result = PJ::WidgetLibrary::loadStatic(&null_info);
  ASSERT_FALSE(null_result);
  EXPECT_NE(null_result.error().find("build_info returned null"), std::string::npos) << null_result.error();

  PJ_widget_vtable_t truncated = *pj_static_get_widget_vtable_MockWidget();
  truncated.build_info = &truncatedBuildInfo;
  auto truncated_result = PJ::WidgetLibrary::loadStatic(&truncated);
  ASSERT_FALSE(truncated_result);
  EXPECT_NE(truncated_result.error().find("smaller than the v1 layout"), std::string::npos) << truncated_result.error();

  PJ_widget_vtable_t corrupt = *pj_static_get_widget_vtable_MockWidget();
  corrupt.build_info = &corruptViewBuildInfo;
  auto corrupt_result = PJ::WidgetLibrary::loadStatic(&corrupt);
  ASSERT_FALSE(corrupt_result);
  EXPECT_NE(corrupt_result.error().find("build_info version"), std::string::npos) << corrupt_result.error();
}

// --- Exceptions in onUpdate and in the history callback ----------------------

struct BoundProbe {
  PJ::WidgetHostBridge bridge;
  PJ::ServiceRegistryBuilder registry;
  std::vector<std::pair<PJ::WidgetMessageLevel, std::string>> messages;
  PJ::WidgetHandle handle = makeProbe();

  BoundProbe() {
    bridge.report_message = [this](PJ::WidgetMessageLevel level, std::string_view message) {
      messages.emplace_back(level, std::string(message));
    };
    registry.registerService<PJ::sdk::WidgetHostService>(bridge.host());
    auto status = handle.bind(registry.view());
    EXPECT_TRUE(status) << status.error();
  }
};

TEST(WidgetStaticLibraryTest, ThrowingOnUpdateIsReportedOncePerFailureRun) {
  BoundProbe probe;
  ProbeWidget& widget = probeOf(probe.handle);

  widget.throw_in_update = true;
  for (int tick = 0; tick < 5; ++tick) {
    probe.handle.onUpdate({}, tick);
  }
  ASSERT_EQ(probe.messages.size(), 1u) << "a failing tick must not flood the host";
  EXPECT_EQ(probe.messages[0].first, PJ::WidgetMessageLevel::kError);
  EXPECT_NE(probe.messages[0].second.find("update exploded"), std::string::npos) << probe.messages[0].second;

  widget.throw_in_update = false;
  probe.handle.onUpdate({}, 5.0);  // success re-arms the report
  EXPECT_EQ(probe.messages.size(), 1u);

  widget.throw_in_update = true;
  probe.handle.onUpdate({}, 6.0);
  probe.handle.onUpdate({}, 7.0);
  EXPECT_EQ(probe.messages.size(), 2u);
}

TEST(WidgetStaticLibraryTest, ThrowingHistoryCallbackIsAnError) {
  BoundProbe probe;
  int host_sink_calls = 0;
  probe.bridge.history = [&](const PJ::WidgetHistoryQuery& query,
                             const PJ::WidgetValueSink& sink) -> PJ::Expected<uint32_t> {
    for (double t : {1.0, 2.0, 3.0}) {
      PJ_widget_value_t value{};
      value.input = query.input;
      value.kind = PJ_WIDGET_VALUE_NUMBER;
      value.time = t;
      ++host_sink_calls;
      if (!sink(value)) {
        break;
      }
    }
    return 3u;
  };
  ProbeWidget& widget = probeOf(probe.handle);
  widget.query_with_throwing_callback = true;
  probe.handle.onUpdate({}, 3.0);

  EXPECT_EQ(widget.history_callback_calls, 1) << "the delivery stops at the throw";
  EXPECT_EQ(host_sink_calls, 1);
  EXPECT_NE(widget.history_error.find("history callback threw: callback exploded"), std::string::npos)
      << widget.history_error;
  EXPECT_TRUE(probe.messages.empty()) << "onUpdate itself did not throw";
}

}  // namespace
