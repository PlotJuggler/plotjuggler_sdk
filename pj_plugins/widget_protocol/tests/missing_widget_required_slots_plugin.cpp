// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/widget_protocol.h"

extern "C" PJ_WIDGET_EXPORT const uint32_t pj_plugin_abi_version = PJ_ABI_VERSION;

namespace {

void* create(PJ_string_view_t) noexcept {
  return reinterpret_cast<void*>(0x1);
}

void destroy(void*) noexcept {}

bool bind(void*, PJ_service_registry_t, PJ_error_t*) noexcept {
  return true;
}

uint32_t inputs(void*, const PJ_widget_input_t** out) noexcept {
  *out = nullptr;
  return 0;
}

PJ_qwidget_t createQWidget(void*, PJ_qwidget_t, PJ_error_t*) noexcept {
  return nullptr;
}

void onUpdate(void*, const PJ_widget_value_t*, uint32_t, double) noexcept {}

bool saveConfig(void*, PJ_string_view_t*, PJ_error_t*) noexcept {
  return true;
}

bool loadConfig(void*, PJ_string_view_t, PJ_error_t*) noexcept {
  return true;
}

const void* getPluginExtension(void*, PJ_string_view_t) noexcept {
  return nullptr;
}

}  // namespace

// build_info is deliberately left null: a required slot.
extern "C" PJ_WIDGET_EXPORT const PJ_widget_vtable_t* PJ_get_widget_vtable() noexcept {
  static const PJ_widget_vtable_t vt = {
      .protocol_version = PJ_WIDGET_PLUGIN_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_widget_vtable_t),
      .create = create,
      .destroy = destroy,
      .manifest_json =
          R"({"id":"missing-widget-slot","name":"Missing Widget Slot","version":"1.0.0","widgets":[{"type":"w","name":"W"}]})",
      .build_info = nullptr,
      .bind = bind,
      .inputs = inputs,
      .create_qwidget = createQWidget,
      .on_update = onUpdate,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .get_plugin_extension = getPluginExtension,
  };
  return &vt;
}
