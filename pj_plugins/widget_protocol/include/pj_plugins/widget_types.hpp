/**
 * @file widget_types.hpp
 * @brief C++ value types shared by the Widget plugin SDK and the host loader.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "pj_plugins/widget_protocol.h"

namespace PJ {

/// Compiler family recorded in PJ_widget_build_info_t::compiler. Plugin and host both call this, so
/// the two sides agree by construction.
[[nodiscard]] constexpr const char* widgetCompilerFamily() {
#if defined(_MSC_VER)
  return "msvc";  // clang-cl uses the MSVC ABI and runtime, so it reports as msvc
#elif defined(__clang__)
  return "clang";
#else
  return "gcc";
#endif
}

/// True when this translation unit links the MSVC debug C/C++ runtime (`_DEBUG`, which loads the
/// `Qt6*d.dll` libraries). Always false with GCC/Clang: there a debug build links the same Qt as a
/// release build, so the flavour does not matter.
[[nodiscard]] constexpr bool widgetBuiltAsDebug() {
#if defined(_DEBUG)
  return true;
#else
  return false;
#endif
}

/// Kind of series an input accepts. Mirrors PJ_widget_input_kind_t.
enum class WidgetInputKind : uint32_t {
  kNumber = PJ_WIDGET_INPUT_NUMBER,
  kText = PJ_WIDGET_INPUT_TEXT,
  kObject = PJ_WIDGET_INPUT_OBJECT,
};

/// One input of a widget. `name` is its stable identity (the host matches wirings by name, never by
/// position), so it must not change while the widget lives unless the input is meant to be a new one.
struct WidgetInput {
  WidgetInput() = default;
  explicit WidgetInput(
      std::string input_name, std::string input_label = {}, std::string input_unit = {},
      WidgetInputKind input_kind = WidgetInputKind::kNumber, bool input_multiple = false)
      : name(std::move(input_name)),
        label(std::move(input_label)),
        unit(std::move(input_unit)),
        kind(input_kind),
        multiple(input_multiple) {}

  std::string name;
  std::string label;  ///< display text; empty = the host shows `name`
  std::string unit;
  WidgetInputKind kind = WidgetInputKind::kNumber;
  bool multiple = false;  ///< true: any number of series, one slot each; false: exactly one (PJ_WIDGET_INPUT_MULTIPLE)
};

/// Severity of a message sent with WidgetHostView::reportWidgetMessage. Mirrors PJ_widget_message_level_t.
enum class WidgetMessageLevel : uint32_t {
  kInfo = PJ_WIDGET_MESSAGE_INFO,
  kWarning = PJ_WIDGET_MESSAGE_WARNING,
  kError = PJ_WIDGET_MESSAGE_ERROR,
};

}  // namespace PJ
