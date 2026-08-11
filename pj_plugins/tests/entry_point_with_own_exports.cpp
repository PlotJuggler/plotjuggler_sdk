// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/data_source_protocol.h"
#include "pj_base/plugin_abi_export.hpp"

#if defined(_WIN32)
#define PJ_FIXTURE_IMPORT __declspec(dllimport)
#else
#define PJ_FIXTURE_IMPORT
#endif

extern "C" PJ_FIXTURE_IMPORT int pj_entry_point_donor_marker() noexcept;

namespace {

[[maybe_unused]] const int kKeepDonorDependency = pj_entry_point_donor_marker();

void* create() noexcept {
  return reinterpret_cast<void*>(0x1);
}

void destroy(void*) noexcept {}

uint64_t capabilities(void*) noexcept {
  return 0;
}

bool ok(void*, PJ_service_registry_t, PJ_error_t*) noexcept {
  return true;
}

bool saveConfig(void*, PJ_string_view_t* out_json, PJ_error_t*) noexcept {
  static constexpr char kJson[] = "{}";
  if (out_json != nullptr) {
    out_json->data = kJson;
    out_json->size = 2;
  }
  return true;
}

bool loadConfig(void*, PJ_string_view_t, PJ_error_t*) noexcept {
  return true;
}

bool action(void*, PJ_error_t*) noexcept {
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
  static const PJ_data_source_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_vtable_t),
      .create = create,
      .destroy = destroy,
      .manifest_json = R"({"id":"entry-point-candidate","name":"Entry Point Candidate","version":"1.0.0"})",
      .capabilities = capabilities,
      .bind = ok,
      .save_config = saveConfig,
      .load_config = loadConfig,
      .start = action,
      .stop = stop,
      .pause = action,
      .resume = action,
      .poll = action,
      .current_state = state,
      .get_dialog = dialog,
      .get_plugin_extension = extension,
  };
  return &vtable;
}
