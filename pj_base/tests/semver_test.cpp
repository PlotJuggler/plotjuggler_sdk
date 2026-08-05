// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/semver.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <utility>
#include <vector>

namespace PJ {
namespace {

SemVer parseOrDie(std::string_view text) {
  auto parsed = SemVer::parse(text);
  if (!parsed) {
    ADD_FAILURE() << text << ": " << parsed.error();
    parsed = SemVer::parse("0.0.0");
  }
  return std::move(*parsed);
}

TEST(SemVerTest, ParsesConcreteVersions) {
  for (const std::string_view version : {
           "0.0.0",
           "1.2.3",
           "1.2.3-alpha",
           "1.2.3-alpha.1",
           "1.2.3+linux-x86-64",
           "1.2.3-alpha.1+build.007",
           "123456789012345678901234567890.2.3",
       }) {
    EXPECT_TRUE(SemVer::isValid(version)) << version;
    auto parsed = SemVer::parse(version);
    EXPECT_TRUE(parsed.has_value()) << version << ": " << (parsed ? "" : parsed.error());
  }
}

TEST(SemVerTest, OrdersCoreVersionsNumerically) {
  const std::vector<std::string_view> ordered = {"0.9.9", "0.10.0", "1.0.0",
                                                 "2.0.0", "10.0.0", "123456789012345678901234567890.0.0"};

  for (size_t lhs = 0; lhs < ordered.size(); ++lhs) {
    for (size_t rhs = lhs + 1; rhs < ordered.size(); ++rhs) {
      EXPECT_LT(parseOrDie(ordered[lhs]), parseOrDie(ordered[rhs])) << ordered[lhs] << " vs " << ordered[rhs];
    }
  }
}

TEST(SemVerTest, ImplementsSpecificationPrecedenceChain) {
  const std::vector<std::string_view> ordered = {
      "1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
      "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0",
  };

  for (size_t index = 1; index < ordered.size(); ++index) {
    EXPECT_LT(parseOrDie(ordered[index - 1]), parseOrDie(ordered[index]))
        << ordered[index - 1] << " vs " << ordered[index];
  }
}

TEST(SemVerTest, ReleaseHasHigherPrecedenceThanPrerelease) {
  EXPECT_LT(parseOrDie("0.21.0-pre"), parseOrDie("0.21.0"));
}

TEST(SemVerTest, NumericPrereleaseIdentifiersCompareNumerically) {
  EXPECT_LT(parseOrDie("1.0.0-alpha.2"), parseOrDie("1.0.0-alpha.11"));
  EXPECT_LT(parseOrDie("1.0.0-999999999999999999999999999999"), parseOrDie("1.0.0-1000000000000000000000000000000"));
}

TEST(SemVerTest, NumericPrereleaseIdentifiersSortBeforeAlphanumericIdentifiers) {
  EXPECT_LT(parseOrDie("1.0.0-1"), parseOrDie("1.0.0-alpha"));
}

TEST(SemVerTest, ShorterPrereleaseSortsFirstWhenPrefixMatches) {
  EXPECT_LT(parseOrDie("1.0.0-alpha"), parseOrDie("1.0.0-alpha.1"));
}

TEST(SemVerTest, BuildMetadataDoesNotAffectPrecedenceOrEquality) {
  const auto linux = parseOrDie("0.21.0+linux");
  const auto windows = parseOrDie("0.21.0+windows");

  EXPECT_EQ(linux.compare(windows), std::strong_ordering::equal);
  EXPECT_EQ(linux, windows);
}

TEST(SemVerTest, RejectsAnythingOutsideSemVerGrammar) {
  for (const std::string_view version : {
           "",
           "1",
           "1.0",
           "1.0.0.0",
           "v1.0.0",
           " 1.0.0",
           "1.0.0 ",
           "01.0.0",
           "1.01.0",
           "1.0.01",
           "1.0.0-",
           "1.0.0+",
           "1.0.0-alpha..1",
           "1.0.0+build..1",
           "1.0.0-01",
           "1.0.0-alpha_1",
           "1.0.0+build_1",
           "1.0.0+meta+again",
           "^1.0.0",
           ">=1.0.0",
           "1.x.0",
           "*",
       }) {
    EXPECT_FALSE(SemVer::isValid(version)) << version;
    auto parsed = SemVer::parse(version);
    ASSERT_FALSE(parsed.has_value()) << version;
    EXPECT_FALSE(parsed.error().empty()) << version;
  }
}

}  // namespace
}  // namespace PJ
