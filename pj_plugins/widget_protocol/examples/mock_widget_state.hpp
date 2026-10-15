// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

#include "pj_plugins/widget_protocol.h"

namespace pj_mock {

/// Extension id under which mock_widget exposes its WidgetState to the tests.
inline constexpr std::string_view kWidgetStateExtensionId = "pj.experimental.mock_widget_state/draft-1";

/// What the mock widget observed; read by the tests through get_plugin_extension.
struct WidgetState {
  void* parent = nullptr;
  uint32_t widgets_created = 0;
  uint32_t updates = 0;
  uint32_t last_count = 0;
  double last_time = 0.0;
  double last_number = 0.0;
  PJ_widget_value_kind_t last_kind = PJ_WIDGET_VALUE_NONE;
  uint32_t history_samples = 0;
  double history_last = 0.0;
  bool history_ok = false;
};

}  // namespace pj_mock
