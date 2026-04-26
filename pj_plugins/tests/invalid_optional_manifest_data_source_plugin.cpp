#include "pj_base/data_source_protocol.h"

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

bool save(void*, PJ_string_view_t* out_json, PJ_error_t*) noexcept {
  static constexpr const char* kJson = "{}";
  if (out_json != nullptr) {
    out_json->data = kJson;
    out_json->size = 2;
  }
  return true;
}

bool load(void*, PJ_string_view_t, PJ_error_t*) noexcept {
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
      PJ_DATA_SOURCE_PROTOCOL_VERSION,
      sizeof(PJ_data_source_vtable_t),
      create,
      destroy,
      R"({"id":"invalid-optional-source","name":"Invalid Optional Source","version":"1.0.0","description":42})",
      capabilities,
      bind,
      save,
      load,
      ok,
      stop,
      ok,
      ok,
      ok,
      state,
      dialog,
      extension,
  };
  return &vt;
}
