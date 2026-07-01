#include <cstdlib>
#include <pj_base/number_parse.hpp>

// parseNumber<double> dispatches to PJ::detail::parseDoubleImpl, which is
// compiled out-of-line into libpj_base.a. Calling it therefore verifies that
// the static archive is actually linked through the plugin_sdk component, not
// merely that the headers are on the include path.
int main() {
  const auto value = PJ::parseNumber<double>("1.5");
  return (value && *value == 1.5) ? EXIT_SUCCESS : EXIT_FAILURE;
}
