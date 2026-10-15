// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

// Stand-ins for the toolkit version macros a real widget plugin gets from its toolkit headers.
#define QT_VERSION_MAJOR 6
#define QT_VERSION_MINOR 12
#define QT_VERSION_PATCH 0
#define QT_VERSION_STR "6.12.0"

#include <pj_base/sdk/data_source_plugin_base.hpp>
#include <pj_base/sdk/toolbox_plugin_base.hpp>
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/message_parser_plugin_base.hpp>
#include <pj_plugins/sdk/widget_plugin_base.hpp>

namespace namespaced_plugin {

class Source : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kStopped;
  }
};

class Parser : public PJ::MessageParserPluginBase {};

class Toolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;
  }
};

class Dialog : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return kManifest;
  }
  std::string ui_content() const override {
    return "";
  }
  std::string widget_data() override {
    return "{}";
  }

  static constexpr const char* kManifest = R"({"id":"namespaced-dialog","name":"Namespaced Dialog","version":"1.0.0"})";
};

class Widget : public PJ::WidgetPluginBase {
 public:
  std::vector<PJ::WidgetInput> inputs() const override {
    return {};
  }
  void* createWidget(void*) override {
    return this;
  }
  void onUpdate(PJ::Span<const PJ_widget_value_t>, double) override {}

  static constexpr const char* kManifest = R"({"id":"namespaced-widget","name":"Namespaced Widget","version":"1.0.0",)"
                                           R"("widgets":[{"type":"w","name":"W"}]})";
};

}  // namespace namespaced_plugin

PJ_DATA_SOURCE_PLUGIN_NAMED(
    namespaced_plugin::Source, NamespacedSource,
    R"({"id":"namespaced-source","name":"Namespaced Source","version":"1.0.0"})")
PJ_MESSAGE_PARSER_PLUGIN_NAMED(
    namespaced_plugin::Parser, NamespacedParser,
    R"({"id":"namespaced-parser","name":"Namespaced Parser","version":"1.0.0","encoding":["test"]})")
PJ_TOOLBOX_PLUGIN_NAMED(
    namespaced_plugin::Toolbox, NamespacedToolbox,
    R"({"id":"namespaced-toolbox","name":"Namespaced Toolbox","version":"1.0.0"})")
PJ_DIALOG_PLUGIN_NAMED(namespaced_plugin::Dialog, NamespacedDialog, namespaced_plugin::Dialog::kManifest)
PJ_WIDGET_PLUGIN_NAMED(namespaced_plugin::Widget, NamespacedWidget, namespaced_plugin::Widget::kManifest)

TEST(StaticNamespacedPluginMacrosTest, EmitCallableNamedGetters) {
  EXPECT_NE(pj_static_get_data_source_vtable_NamespacedSource(), nullptr);
  EXPECT_NE(pj_static_get_message_parser_vtable_NamespacedParser(), nullptr);
  EXPECT_NE(pj_static_get_toolbox_vtable_NamespacedToolbox(), nullptr);
  EXPECT_NE(pj_static_get_dialog_vtable_NamespacedDialog(), nullptr);
  EXPECT_NE(pj_static_get_widget_vtable_NamespacedWidget(), nullptr);
  EXPECT_EQ(PJ::dialogVtableFor<namespaced_plugin::Dialog>(), pj_static_get_dialog_vtable_NamespacedDialog());
}
