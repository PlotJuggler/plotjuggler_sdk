// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <new>
#include <nlohmann/json.hpp>
#include <string>

#include "pj_plugins/dialog_protocol.h"

extern "C" PJ_DIALOG_EXPORT const uint32_t pj_plugin_abi_version = PJ_ABI_VERSION;

namespace {

struct State {
  int legacy_calls = 0;
  std::string widget_data = R"({"legacy_calls":0})";
};

void* create() noexcept {
  return new (std::nothrow) State;
}

void destroy(void* context) noexcept {
  delete static_cast<State*>(context);
}

const char* getManifest(void*) noexcept {
  return R"({"id":"old-file-picker-dispatcher","name":"Old File Picker Dispatcher","version":"1.0.0"})";
}

const char* getUiContent(void*) noexcept {
  return "<ui/>";
}

const char* getWidgetData(void* context) noexcept {
  return static_cast<State*>(context)->widget_data.c_str();
}

// Snapshot of the pre-structured dispatcher behavior: it knows only the two
// legacy string keys and ignores every other event member.
bool onWidgetEvent(void* context, const char*, const char* event_json, PJ_error_t*) noexcept {
  try {
    auto event = nlohmann::json::parse(event_json, nullptr, false);
    if (event.is_discarded() || !event.is_object()) {
      return false;
    }
    const char* callback = nullptr;
    auto selected = event.find("file_selected");
    if (selected != event.end() && selected->is_string()) {
      callback = "file_selected";
    } else {
      selected = event.find("folder_selected");
      if (selected != event.end() && selected->is_string()) {
        callback = "folder_selected";
      }
    }
    if (callback == nullptr) {
      return false;
    }

    auto* state = static_cast<State*>(context);
    ++state->legacy_calls;
    state->widget_data =
        nlohmann::json{{"legacy_calls", state->legacy_calls}, {"callback", callback}, {"path", *selected}}.dump();
    return true;
  } catch (...) {
    return false;
  }
}

bool onTick(void*, PJ_error_t*) noexcept {
  return false;
}

void onAccepted(void*, const char*) noexcept {}

void onRejected(void*) noexcept {}

bool saveConfig(void* context, PJ_string_view_t* out_json, PJ_error_t*) noexcept {
  const auto& json = static_cast<State*>(context)->widget_data;
  if (out_json != nullptr) {
    out_json->data = json.data();
    out_json->size = json.size();
  }
  return true;
}

bool loadConfig(void*, PJ_string_view_t, PJ_error_t*) noexcept {
  return false;
}

}  // namespace

extern "C" PJ_DIALOG_EXPORT const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept {
  static const PJ_dialog_vtable_t vtable = {
      .protocol_version = PJ_DIALOG_PROTOCOL_VERSION,
      .struct_size = PJ_DIALOG_MIN_VTABLE_SIZE,
      .create = create,
      .destroy = destroy,
      .get_manifest = getManifest,
      .get_ui_content = getUiContent,
      .get_widget_data = getWidgetData,
      .on_widget_event = onWidgetEvent,
      .on_tick = onTick,
      .on_accepted = onAccepted,
      .on_rejected = onRejected,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .manifest_json = nullptr,
      .set_host_info = nullptr,
  };
  return &vtable;
}
