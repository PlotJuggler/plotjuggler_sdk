// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/widget_qt_modules.h"

#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#ifndef PJ_WIDGET_QT_MODULES_JSON_PATH
#error "PJ_WIDGET_QT_MODULES_JSON_PATH must be defined"
#endif

namespace {

// The installed JSON is generated from the X-macro at configure time; this proves
// the generation kept the same modules, versions and order.
TEST(WidgetQtModulesTest, InstalledJsonMatchesTheHeaderList) {
  std::vector<std::pair<std::string, std::string>> expected;
#define PJ_TEST_COLLECT(name, since) expected.emplace_back(#name, since);
  PJ_WIDGET_QT_MODULES(PJ_TEST_COLLECT)
#undef PJ_TEST_COLLECT

  std::ifstream file(PJ_WIDGET_QT_MODULES_JSON_PATH);
  ASSERT_TRUE(file.is_open()) << PJ_WIDGET_QT_MODULES_JSON_PATH;
  const auto json = nlohmann::json::parse(file);
  EXPECT_EQ(json.at("schema_version"), 1);
  const auto& modules = json.at("modules");
  ASSERT_EQ(modules.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(modules[i].at("name"), expected[i].first);
    EXPECT_EQ(modules[i].at("since"), expected[i].second);
  }
}

TEST(WidgetQtModulesTest, CanonicalListHoldsTheV1Modules) {
  std::vector<std::string> names;
#define PJ_TEST_COLLECT(name, since) names.emplace_back(#name);
  PJ_WIDGET_QT_MODULES(PJ_TEST_COLLECT)
#undef PJ_TEST_COLLECT
  const std::vector<std::string> v1 = {"Core",    "Gui",    "Widgets",       "Svg",    "Xml",
                                       "Network", "OpenGL", "OpenGLWidgets", "UiTools"};
  EXPECT_EQ(names, v1);
}

}  // namespace
