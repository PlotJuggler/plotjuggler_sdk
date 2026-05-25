// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_plugins/sdk/dialog_plugin_base.hpp>
#include <string>
#include <string_view>

class LegacyMacroDialog : public PJ::DialogPluginBase {
 public:
  std::string manifest() const override {
    return R"({"id":"legacy-macro-dialog","name":"Legacy Macro Dialog","version":"1.0.0"})";
  }

  std::string ui_content() const override {
    return "";
  }

  std::string widget_data() override {
    return "{}";
  }

  bool onWidgetEvent(std::string_view, std::string_view) override {
    return false;
  }
};

PJ_DIALOG_PLUGIN(LegacyMacroDialog)
