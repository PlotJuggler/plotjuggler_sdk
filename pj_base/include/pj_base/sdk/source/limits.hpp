// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace PJ::sdk::source {

/// Strict unsigned decimal environment ceiling (at most 20 digits). Missing,
/// empty, signed, whitespace, junk or overflow uses fallback_value, never disables
/// the ceiling. A literal "0" is accepted. Unit conversion/saturation is the caller's job.
[[nodiscard]] std::uint64_t envLimit(const char* name, std::uint64_t fallback_value);

/// The tighter ceiling; zero means that side imposes no ceiling.
[[nodiscard]] constexpr std::uint64_t minNonzero(std::uint64_t first, std::uint64_t second) noexcept {
  return first == 0 ? second : (second == 0 || first < second ? first : second);
}

}  // namespace PJ::sdk::source
