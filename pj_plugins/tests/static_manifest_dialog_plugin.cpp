#include "pj_plugins/dialog_protocol.h"

extern "C" PJ_DIALOG_EXPORT const uint32_t pj_plugin_abi_version = PJ_ABI_VERSION;

namespace {

constexpr const char* kStaticManifest =
    R"({"id":"static-manifest-dialog","name":"Static Manifest Dialog","version":"1.0.0"})";

void* create() noexcept {
  return nullptr;
}

void destroy(void*) noexcept {}

const char* getManifest(void*) noexcept {
  return R"({"id":"runtime-dialog","name":"Runtime Dialog","version":"1.0.0"})";
}

const char* getUiContent(void*) noexcept {
  return "";
}

const char* getWidgetData(void*) noexcept {
  return "{}";
}

bool onWidgetEvent(void*, const char*, const char*, PJ_error_t*) noexcept {
  return false;
}

bool onTick(void*, PJ_error_t*) noexcept {
  return false;
}

void onAccepted(void*, const char*) noexcept {}

void onRejected(void*) noexcept {}

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

}  // namespace

extern "C" PJ_DIALOG_EXPORT const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept {
  static const PJ_dialog_vtable_t vt = {
      .protocol_version = PJ_DIALOG_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_dialog_vtable_t),
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
      .manifest_json = kStaticManifest,
  };
  return &vt;
}
