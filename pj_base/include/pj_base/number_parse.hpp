#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace PJ {

namespace detail {
// Floating-point parsing is defined out-of-line in number_parse.cpp and
// backed by fast_float. The implementation is a private dependency of
// pj_base — no fast_float header is visible from this file or any other
// installed pj_base header. Each helper returns nullopt unless the *entire*
// input is a valid, in-range value (no trailing characters, no overflow).
[[nodiscard]] std::optional<float> parseFloatImpl(std::string_view text);
[[nodiscard]] std::optional<double> parseDoubleImpl(std::string_view text);
[[nodiscard]] std::optional<long double> parseLongDoubleImpl(std::string_view text);
}  // namespace detail

/// Parse the *entire* @p text as a number of type T, returning nullopt unless
/// the whole string is a valid, in-range value (empty input, trailing
/// characters, or overflow all yield nullopt).
///
/// Floating-point parsing is locale-independent: "1.5" always parses as 1.5
/// regardless of the active C/C++ locale. (std::strto* respects LC_NUMERIC
/// and silently mis-parses numbers under non-C locales — fast_float backs
/// this branch precisely to remove that footgun.)
///
/// Integer parsing uses std::from_chars in base 10.
template <typename T>
[[nodiscard]] std::optional<T> parseNumber(std::string_view text) {
  static_assert(
      std::is_arithmetic_v<T> && !std::is_same_v<T, bool>,
      "parseNumber supports integral and floating-point types, not bool");
  if (text.empty()) {
    return std::nullopt;
  }
  if constexpr (std::is_floating_point_v<T>) {
    if constexpr (std::is_same_v<T, float>) {
      return detail::parseFloatImpl(text);
    } else if constexpr (std::is_same_v<T, double>) {
      return detail::parseDoubleImpl(text);
    } else {
      return detail::parseLongDoubleImpl(text);
    }
  } else {
    T out{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc{} || ptr != end) {
      return std::nullopt;
    }
    return out;
  }
}

}  // namespace PJ
