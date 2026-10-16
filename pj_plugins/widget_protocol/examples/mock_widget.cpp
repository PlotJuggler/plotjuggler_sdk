// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Toolkit-free Widget plugin used by the SDK tests. createWidget() returns the
// address of a member instead of a real widget: the SDK never dereferences it.

// A real plugin gets these from the toolkit headers it includes; a plugin
// translation unit expanding PJ_WIDGET_PLUGIN must have them.
#ifndef QT_VERSION_MAJOR
#define QT_VERSION_MAJOR 6
#define QT_VERSION_MINOR 12
#define QT_VERSION_PATCH 3
#define QT_VERSION_STR "6.12.3"
#endif

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mock_widget_manifest.hpp"
#include "mock_widget_state.hpp"
#include "pj_plugins/sdk/widget_plugin_base.hpp"

namespace {

class MockWidget : public PJ::WidgetPluginBase {
 public:
  explicit MockWidget(std::string_view type) : type_(type) {
    if (type_ != "gauge" && type_ != "table") {
      throw std::invalid_argument("unknown widget type");
    }
  }

  std::vector<PJ::WidgetInput> inputs() const override {
    std::vector<PJ::WidgetInput> result;
    if (type_ == "gauge") {
      result.emplace_back("value", "Value", "V");
    } else {
      result.emplace_back("series", "", "", PJ::WidgetInputKind::kNumber, true);
    }
    if (config_.find("extra_input") != std::string::npos) {
      result.emplace_back("state", "", "", PJ::WidgetInputKind::kText);
    }
    return result;
  }

  void* createWidget(void* parent) override {
    state_.parent = parent;
    ++state_.widgets_created;
    return &fake_widget_;
  }

  void onUpdate(PJ::Span<const PJ_widget_value_t> values, double time) override {
    ++state_.updates;
    state_.last_count = static_cast<uint32_t>(values.size());
    state_.last_time = time;
    state_.last_number = 0.0;
    state_.last_kind = PJ_WIDGET_VALUE_NONE;
    // One entry per wire: dispatch on the input index, never on the position in the array.
    for (const PJ_widget_value_t& value : values) {
      if (value.input == 0 && value.slot == 0) {
        state_.last_number = value.number;
        state_.last_kind = value.kind;
      }
    }
    if (config_.find("history") != std::string::npos && widgetHostBound()) {
      state_.history_samples = 0;
      const bool stop_early = config_.find("history_stop") != std::string::npos;
      auto total = widgetHost().queryHistory(0, 0, 0.0, time, 100, [this, stop_early](const PJ_widget_value_t& value) {
        ++state_.history_samples;
        state_.history_last = value.number;
        return !stop_early;
      });
      state_.history_ok = total.has_value();
      state_.history_total = total.has_value() ? *total : 0;
    }
  }

  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    if (config_json.find("reject") != std::string_view::npos) {
      return PJ::unexpected("config rejected");
    }
    config_ = std::string(config_json);
    if (config_.find("extra_input") != std::string::npos && widgetHostBound()) {
      widgetHost().notifyInputsChanged();
      widgetHost().reportWidgetMessage(PJ::WidgetMessageLevel::kInfo, "inputs changed");
    }
    return PJ::okStatus();
  }

  const void* pluginExtension(std::string_view id) override {
    return id == pj_mock::kWidgetStateExtensionId ? &state_ : nullptr;
  }

 private:
  std::string type_;
  std::string config_ = "{}";
  pj_mock::WidgetState state_;
  char fake_widget_ = 0;
};

}  // namespace

PJ_WIDGET_PLUGIN(MockWidget, kMockWidgetManifest)
