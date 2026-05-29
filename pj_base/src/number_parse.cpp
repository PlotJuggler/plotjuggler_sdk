// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/number_parse.hpp"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <system_error>

#include "fast_float/fast_float.h"

namespace PJ::detail {

namespace {
template <typename T>
[[nodiscard]] std::optional<T> parseWithFastFloat(std::string_view text) {
  T out{};
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = fast_float::from_chars(begin, end, out);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return out;
}
}  // namespace

std::optional<float> parseFloatImpl(std::string_view text) {
  return parseWithFastFloat<float>(text);
}

std::optional<double> parseDoubleImpl(std::string_view text) {
  return parseWithFastFloat<double>(text);
}

// fast_float does not implement long double, so this branch falls back to
// std::strtold. strtold is locale-dependent (LC_NUMERIC), but long double
// has no in-tree call sites today; the fallback exists only to keep the
// template's compile-time surface complete.
std::optional<long double> parseLongDoubleImpl(std::string_view text) {
  const std::string buffer(text);
  const char* begin = buffer.c_str();
  char* last = nullptr;
  errno = 0;
  const long double out = std::strtold(begin, &last);
  if (errno != 0 || last != begin + buffer.size()) {
    return std::nullopt;
  }
  return out;
}

}  // namespace PJ::detail
