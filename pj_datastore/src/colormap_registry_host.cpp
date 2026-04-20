#include "pj_datastore/colormap_registry_host.hpp"

#include <string_view>

#include "pj_datastore/colormap_registry.hpp"

namespace PJ {

namespace {

std::string_view toStringView(PJ_string_view_t s) {
  return std::string_view(s.data, s.size);
}

bool registryRegisterMap(void* ctx, PJ_string_view_t name,
                         const char* (*eval_fn)(double, void*), void* user_ctx) {
  auto* reg = static_cast<ColorMapRegistry*>(ctx);
  reg->registerMap(toStringView(name), eval_fn, user_ctx);
  return true;
}

bool registryUnregisterMap(void* ctx, PJ_string_view_t name) {
  auto* reg = static_cast<ColorMapRegistry*>(ctx);
  reg->unregisterMap(toStringView(name));
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
