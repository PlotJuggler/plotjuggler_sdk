#pragma once

#include "pj_base/plugin_data_api.h"

namespace PJ {

class ColorMapRegistry;

/// Wrap a `ColorMapRegistry` as a C ABI `PJ_colormap_registry_t` so it can be
/// bound into a toolbox plugin via `bind_colormap_registry`.
///
/// The returned fat pointer references `registry` by address; the registry
/// must outlive every plugin instance it is bound to. The vtable itself is a
/// static singleton — safe to share across plugins and threads.
[[nodiscard]] PJ_colormap_registry_t makeColorMapRegistryHost(ColorMapRegistry& registry);

}  // namespace PJ
