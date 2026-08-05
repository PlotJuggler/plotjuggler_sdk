// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/semver.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PJ {
namespace {

[[nodiscard]] constexpr bool isAsciiDigit(char c) noexcept {
  return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool isAsciiLetter(char c) noexcept {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] bool isNumeric(std::string_view identifier) noexcept {
  for (const char c : identifier) {
    if (!isAsciiDigit(c)) {
      return false;
    }
  }
  return !identifier.empty();
}

[[nodiscard]] bool isValidIdentifier(std::string_view identifier) noexcept {
  if (identifier.empty()) {
    return false;
  }
  for (const char c : identifier) {
    if (!isAsciiDigit(c) && !isAsciiLetter(c) && c != '-') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Expected<std::vector<std::string>> parseIdentifiers(
    std::string_view text, std::string_view kind, bool reject_numeric_leading_zero) {
  if (text.empty()) {
    return unexpected(std::string(kind) + " identifiers must not be empty");
  }

  std::vector<std::string> identifiers;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t separator = text.find('.', begin);
    const size_t end = separator == std::string_view::npos ? text.size() : separator;
    const std::string_view identifier = text.substr(begin, end - begin);

    if (!isValidIdentifier(identifier)) {
      return unexpected(std::string(kind) + " contains an invalid identifier");
    }
    if (reject_numeric_leading_zero && identifier.size() > 1 && identifier.front() == '0' && isNumeric(identifier)) {
      return unexpected(std::string(kind) + " numeric identifiers must not contain leading zeros");
    }
    identifiers.emplace_back(identifier);

    if (separator == std::string_view::npos) {
      break;
    }
    begin = separator + 1;
  }
  return identifiers;
}

[[nodiscard]] Expected<std::vector<std::string>> parseCore(std::string_view text) {
  auto components = parseIdentifiers(text, "core version", true);
  if (!components) {
    return unexpected(components.error());
  }
  if (components->size() != 3) {
    return unexpected("core version must contain exactly three numeric components");
  }
  for (const auto& component : *components) {
    if (!isNumeric(component)) {
      return unexpected("core version components must be numeric");
    }
  }
  return components;
}

[[nodiscard]] std::strong_ordering compareNumeric(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() < rhs.size()) {
    return std::strong_ordering::less;
  }
  if (lhs.size() > rhs.size()) {
    return std::strong_ordering::greater;
  }
  if (lhs < rhs) {
    return std::strong_ordering::less;
  }
  if (lhs > rhs) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

[[nodiscard]] std::strong_ordering compareLexical(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs < rhs) {
    return std::strong_ordering::less;
  }
  if (lhs > rhs) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

}  // namespace

SemVer::SemVer(std::string major, std::string minor, std::string patch, std::vector<std::string> prerelease)
    : major_(std::move(major)),
      minor_(std::move(minor)),
      patch_(std::move(patch)),
      prerelease_(std::move(prerelease)) {}

Expected<SemVer> SemVer::parse(std::string_view text) {
  if (text.empty()) {
    return unexpected("semantic version must not be empty");
  }

  const size_t build_separator = text.find('+');
  const std::string_view version_and_prerelease = text.substr(0, build_separator);
  if (build_separator != std::string_view::npos) {
    const std::string_view build = text.substr(build_separator + 1);
    if (auto identifiers = parseIdentifiers(build, "build metadata", false); !identifiers) {
      return unexpected(identifiers.error());
    }
  }

  const size_t prerelease_separator = version_and_prerelease.find('-');
  const std::string_view core = version_and_prerelease.substr(0, prerelease_separator);
  auto components = parseCore(core);
  if (!components) {
    return unexpected(components.error());
  }

  std::vector<std::string> prerelease;
  if (prerelease_separator != std::string_view::npos) {
    auto identifiers = parseIdentifiers(version_and_prerelease.substr(prerelease_separator + 1), "pre-release", true);
    if (!identifiers) {
      return unexpected(identifiers.error());
    }
    prerelease = std::move(*identifiers);
  }

  return SemVer(
      std::move((*components)[0]), std::move((*components)[1]), std::move((*components)[2]), std::move(prerelease));
}

bool SemVer::isValid(std::string_view text) {
  return parse(text).has_value();
}

std::strong_ordering SemVer::compare(const SemVer& other) const noexcept {
  if (const auto major_order = compareNumeric(major_, other.major_); major_order != 0) {
    return major_order;
  }
  if (const auto minor_order = compareNumeric(minor_, other.minor_); minor_order != 0) {
    return minor_order;
  }
  if (const auto patch_order = compareNumeric(patch_, other.patch_); patch_order != 0) {
    return patch_order;
  }

  if (prerelease_.empty() && other.prerelease_.empty()) {
    return std::strong_ordering::equal;
  }
  if (prerelease_.empty()) {
    return std::strong_ordering::greater;
  }
  if (other.prerelease_.empty()) {
    return std::strong_ordering::less;
  }

  const size_t common_size = std::min(prerelease_.size(), other.prerelease_.size());
  for (size_t index = 0; index < common_size; ++index) {
    const std::string_view lhs = prerelease_[index];
    const std::string_view rhs = other.prerelease_[index];
    const bool lhs_numeric = isNumeric(lhs);
    const bool rhs_numeric = isNumeric(rhs);

    if (lhs_numeric != rhs_numeric) {
      return lhs_numeric ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    const auto identifier_order = lhs_numeric ? compareNumeric(lhs, rhs) : compareLexical(lhs, rhs);
    if (identifier_order != 0) {
      return identifier_order;
    }
  }

  if (prerelease_.size() < other.prerelease_.size()) {
    return std::strong_ordering::less;
  }
  if (prerelease_.size() > other.prerelease_.size()) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

}  // namespace PJ
