// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/data_source_protocol.h"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/toolbox_protocol.h"

extern "C" PJ_DATA_SOURCE_EXPORT const uint32_t pj_plugin_abi_version = PJ_ABI_VERSION;

namespace {

void* create() noexcept {
  return reinterpret_cast<void*>(0x1);
}

void destroy(void*) noexcept {}

uint64_t capabilities(void*) noexcept {
  return 0;
}

bool bind(void*, PJ_service_registry_t, PJ_error_t*) noexcept {
  return true;
}

bool bindSchema(void*, PJ_string_view_t, PJ_bytes_view_t, PJ_error_t*) noexcept {
  return true;
}

bool saveConfig(void*, PJ_string_view_t* out_json, PJ_error_t*) noexcept {
  static constexpr const char* kJson = "{}";
  if (out_json != nullptr) {
    out_json->data = kJson;
    out_json->size = 2;
  }
  return true;
}

bool loadConfig(void*, PJ_string_view_t, PJ_error_t*) noexcept {
  return true;
}

bool ok(void*, PJ_error_t*) noexcept {
  return true;
}

void stop(void*) noexcept {}

PJ_data_source_state_t state(void*) noexcept {
  return PJ_DATA_SOURCE_STATE_IDLE;
}

PJ_borrowed_dialog_t dialog(void*) noexcept {
  return PJ_borrowed_dialog_t{nullptr, nullptr};
}

const void* extension(void*, PJ_string_view_t) noexcept {
  return nullptr;
}

}  // namespace

extern "C" PJ_DATA_SOURCE_EXPORT const PJ_data_source_vtable_t* PJ_get_data_source_vtable() noexcept {
  static const PJ_data_source_vtable_t vt = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_vtable_t),
      .create = create,
      .destroy = destroy,
      .manifest_json = R"({"id":"missing-source-slot","name":"Missing Source Slot","version":"1.0.0"})",
      .capabilities = capabilities,
      .bind = bind,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .start = nullptr,
      .stop = stop,
      .pause = ok,
      .resume = ok,
      .poll = ok,
      .current_state = state,
      .get_dialog = dialog,
      .get_plugin_extension = extension,
  };
  return &vt;
}

extern "C" PJ_MESSAGE_PARSER_EXPORT const PJ_message_parser_vtable_t* PJ_get_message_parser_vtable() noexcept {
  static const PJ_message_parser_vtable_t vt = {
      .protocol_version = PJ_MESSAGE_PARSER_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_message_parser_vtable_t),
      .create = create,
      .destroy = destroy,
      .manifest_json =
          R"({"id":"missing-parser-slot","name":"Missing Parser Slot","version":"1.0.0","encoding":["x"]})",
      .bind = bind,
      .bind_schema = bindSchema,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .parse = nullptr,
      .get_plugin_extension = extension,
      .classify_schema = nullptr,
  };
  return &vt;
}

extern "C" PJ_TOOLBOX_EXPORT const PJ_toolbox_vtable_t* PJ_get_toolbox_vtable() noexcept {
  static const PJ_toolbox_vtable_t vt = {
      .protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_toolbox_vtable_t),
      .create = create,
      .destroy = destroy,
      .manifest_json = R"({"id":"missing-toolbox-slot","name":"Missing Toolbox Slot","version":"1.0.0"})",
      .capabilities = capabilities,
      .bind = bind,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .get_dialog = dialog,
      .on_data_changed = nullptr,
      .get_plugin_extension = extension,
  };
  return &vt;
}
