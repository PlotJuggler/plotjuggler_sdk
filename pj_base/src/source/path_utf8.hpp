#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <string>

namespace PJ {
namespace sdk {
namespace source {
namespace detail {

/// The path as UTF-8, for error text and name checks.
///
/// Use this instead of `path.string()`: on Windows that narrows through the
/// active code page and throws `std::system_error` for a name the code page
/// cannot represent, so a function meant to return an error throws instead.
/// On POSIX the result is the same bytes `string()` returns.
[[nodiscard]] inline std::string pathUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

}  // namespace detail
}  // namespace source
}  // namespace sdk
}  // namespace PJ
