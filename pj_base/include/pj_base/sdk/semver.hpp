#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <compare>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ {

/// A parsed Semantic Versioning 2.0.0 version.
///
/// Build metadata is validated during parsing and excluded from precedence as
/// required by SemVer. Equality intentionally follows precedence equivalence,
/// so versions that differ only in build metadata compare equal.
class SemVer {
 public:
  /// Parse one concrete MAJOR.MINOR.PATCH[-prerelease][+build] version.
  [[nodiscard]] static Expected<SemVer> parse(std::string_view text);

  /// Return whether @p text is one concrete, valid SemVer 2.0.0 version.
  [[nodiscard]] static bool isValid(std::string_view text);

  /// Compare precedence, returning less, equal, or greater.
  [[nodiscard]] std::strong_ordering compare(const SemVer& other) const noexcept;

  [[nodiscard]] std::strong_ordering operator<=>(const SemVer& other) const noexcept {
    return compare(other);
  }

  [[nodiscard]] bool operator==(const SemVer& other) const noexcept {
    return compare(other) == std::strong_ordering::equal;
  }

 private:
  SemVer(std::string major, std::string minor, std::string patch, std::vector<std::string> prerelease);

  std::string major_;
  std::string minor_;
  std::string patch_;
  std::vector<std::string> prerelease_;
};

}  // namespace PJ
