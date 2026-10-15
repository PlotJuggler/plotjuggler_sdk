// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// examples/mock_widget.cpp is compiled into this executable with PJ_STATIC_PLUGINS,
// so PJ_WIDGET_PLUGIN emits pj_static_get_widget_vtable_MockWidget instead of the
// dlsym-able entry point.

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

#include "mock_widget_state.hpp"
#include "pj_plugins/host/widget_library.hpp"

const PJ_widget_vtable_t* pj_static_get_widget_vtable_MockWidget() noexcept;

namespace {

PJ_widget_input_t g_corrupt_input{};
constexpr std::size_t kImpossibleLength = std::numeric_limits<std::size_t>::max() - 1;

// A plugin whose string views no std::string could hold: the handle must refuse them before any
// pointer arithmetic, so that a corrupt plugin is reported instead of read out of bounds.
uint32_t corruptInputs(void* /*ctx*/, const PJ_widget_input_t** out_inputs) noexcept {
  g_corrupt_input.struct_size = sizeof(PJ_widget_input_t);
  g_corrupt_input.name = PJ_string_view_t{"x", kImpossibleLength};
  g_corrupt_input.kind = PJ_WIDGET_INPUT_NUMBER;
  *out_inputs = &g_corrupt_input;
  return 1;
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

  EXPECT_THROW((void)handle->inputs(), std::invalid_argument);
  std::string json;
  EXPECT_THROW((void)handle->saveConfig(json), std::invalid_argument);
}

TEST(WidgetStaticLibraryTest, LoadStaticWrapsTheVtableWithoutDlopen) {
  auto library = PJ::WidgetLibrary::loadStatic(pj_static_get_widget_vtable_MockWidget());
  ASSERT_TRUE(library) << library.error();
  EXPECT_EQ(library->path(), "static://");

  auto handle = library->createHandle("gauge");
  ASSERT_TRUE(handle) << handle.error();
  ASSERT_EQ(handle->inputs().size(), 1u);
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

}  // namespace
