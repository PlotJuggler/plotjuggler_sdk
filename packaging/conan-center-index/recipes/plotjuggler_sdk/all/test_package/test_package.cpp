#include <cstdlib>
#include <pj_base/number_parse.hpp>

#ifdef PJ_TEST_WITH_HOST
#include <pj_plugins/host/plugin_runtime_catalog.hpp>
#endif

// parseNumber<double> dispatches to PJ::detail::parseDoubleImpl, which is
// compiled out-of-line into libpj_base.a. Calling it therefore verifies that
// the static archive is actually linked through the plugin_sdk component, not
// merely that the headers are on the include path.
int main() {
  const auto value = PJ::parseNumber<double>("1.5");
  if (!value || *value != 1.5) {
    return EXIT_FAILURE;
  }

#ifdef PJ_TEST_WITH_HOST
  // Constructing an empty catalog (default, disk-less) forces the plugin_host
  // archives — and the dlopen-backed loader, hence libdl — onto the link line,
  // validating the plugin_host component's lib list and system_libs wiring.
  const PJ::PluginRuntimeCatalog catalog;
  (void)catalog;
#endif

  return EXIT_SUCCESS;
}
