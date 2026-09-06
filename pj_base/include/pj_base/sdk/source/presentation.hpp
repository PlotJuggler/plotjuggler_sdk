// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <string_view>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk::source {

struct SourcePresentation {
  std::string display_name;
  std::string fallback_name;
  std::string origin;
};

/// HOST contract: source_presentation/v1/ + unpadded base64url(identity UTF-8).
/// A '/' in an identity must never become a settings-group separator.
[[nodiscard]] std::string sourcePresentationSettingsGroup(std::string_view identity);

/// Main-thread, best-effort settings writes, skipping unchanged values and empty
/// identities/origins. Empty display_name uses fallback_name. Strips C0/C1,
/// zero-width, bidi, U+061C, U+2060..2064 and U+FEFF controls, then caps at 200
/// UTF-16 units without splitting code points. Supply valid UTF-8 descriptor text.
void recordSourcePresentation(SettingsView settings, std::string_view identity, const SourcePresentation& presentation);

}  // namespace PJ::sdk::source
