#include "pj_datastore/colormap_registry_host.hpp"

#include <string_view>

#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_datastore/colormap_registry.hpp"

namespace PJ {

namespace {

std::string_view toStringView(PJ_string_view_t s) {
  return std::string_view(s.data, s.size);
}

bool registryRegisterMap(
    void* ctx, PJ_string_view_t name, const char* (*eval_fn)(double, void*), void* user_ctx,
    PJ_error_t* out_error) noexcept {
  if (ctx == nullptr || eval_fn == nullptr) {
    sdk::fillError(out_error, 2, "colormap", "null registry ctx or eval_fn");
    return false;
  }
  auto* reg = static_cast<ColorMapRegistry*>(ctx);
  try {
    reg->registerMap(toStringView(name), eval_fn, user_ctx);
  } catch (const std::exception& e) {
    sdk::fillError(out_error, 1, "colormap", std::string("registerMap threw: ") + e.what());
    return false;
  } catch (...) {
    sdk::fillError(out_error, 1, "colormap", "registerMap threw unknown exception");
    return false;
  }
  return true;
}

bool registryUnregisterMap(void* ctx, PJ_string_view_t name, PJ_error_t* out_error) noexcept {
  if (ctx == nullptr) {
    sdk::fillError(out_error, 2, "colormap", "null registry ctx");
    return false;
  }
  auto* reg = static_cast<ColorMapRegistry*>(ctx);
  try {
    reg->unregisterMap(toStringView(name));
  } catch (const std::exception& e) {
    sdk::fillError(out_error, 1, "colormap", std::string("unregisterMap threw: ") + e.what());
    return false;
  } catch (...) {
    sdk::fillError(out_error, 1, "colormap", "unregisterMap threw unknown exception");
    return false;
  }
  return true;
}

constexpr PJ_colormap_registry_vtable_t kRegistryVTable = {
    PJ_PLUGIN_DATA_API_VERSION,
    sizeof(PJ_colormap_registry_vtable_t),
    registryRegisterMap,
    registryUnregisterMap,
};

}  // namespace

PJ_colormap_registry_t makeColorMapRegistryHost(ColorMapRegistry& registry) {
  return PJ_colormap_registry_t{
      .ctx = &registry,
      .vtable = &kRegistryVTable,
  };
}

}  // namespace PJ
