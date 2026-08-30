/**
 * @file text_utils.hpp
 * @brief Locale-independent text helpers plugin-side code keeps needing:
 *        ASCII lowercase and a strict port parse.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace PJ {
namespace sdk {

/// ASCII-only lowercase (locale-independent), e.g. for case-insensitive
/// scheme, encoding or extension matching.
[[nodiscard]] inline std::string lowerAscii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

/// Strict decimal port parse: the whole text must be digits, 1..65535. No
/// sign, no whitespace, no leading '+'.
[[nodiscard]] inline std::optional<uint16_t> parsePort(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  uint32_t value = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [next, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || next != end || value == 0 || value > 65535) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(value);
}

}  // namespace sdk
}  // namespace PJ
