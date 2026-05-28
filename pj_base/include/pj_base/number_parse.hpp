#pragma once

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace PJ {

/// Parse the *entire* @p text as a number of type T, returning nullopt unless
/// the whole string is a valid, in-range value (empty input, trailing
/// characters, or overflow all yield nullopt).
///
/// Use this instead of std::from_chars when T may be floating-point: Apple
/// Clang's libc++ does not implement the std::from_chars floating-point
/// overloads, so those are routed through std::strto* here. Integral types go
/// straight to std::from_chars on every toolchain.
template <typename T>
[[nodiscard]] std::optional<T> parseNumber(std::string_view text) {
  static_assert(
      std::is_arithmetic_v<T> && !std::is_same_v<T, bool>,
      "parseNumber supports integral and floating-point types, not bool");
  if (text.empty()) {
    return std::nullopt;
  }
  if constexpr (std::is_floating_point_v<T>) {
    // std::strto* needs a null-terminated buffer and @p text may be a
    // non-terminated view, so copy it. Number strings are short — the
    // allocation is negligible for the config/settings paths this serves.
    const std::string buffer(text);
    const char* begin = buffer.c_str();
    char* last = nullptr;
    errno = 0;
    T out{};
    if constexpr (std::is_same_v<T, float>) {
      out = std::strtof(begin, &last);
    } else if constexpr (std::is_same_v<T, long double>) {
      out = std::strtold(begin, &last);
    } else {
      out = std::strtod(begin, &last);
    }
    if (errno != 0 || last != begin + buffer.size()) {
      return std::nullopt;
    }
    return out;
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
