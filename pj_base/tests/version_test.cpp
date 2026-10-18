// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/version.hpp"

#include <gtest/gtest.h>

#include <string>

#include "pj_base/sdk/semver.hpp"

namespace PJ {
namespace {

TEST(SdkVersionTest, ExposesConfiguredProjectVersion) {
  const std::string core = std::to_string(PJ_SDK_VERSION_MAJOR) + "." + std::to_string(PJ_SDK_VERSION_MINOR) + "." +
                           std::to_string(PJ_SDK_VERSION_PATCH);
  const std::string version(sdkVersion());

  // A development pre-release (X.Y.Z-dev.N) keeps its suffix in sdkVersion() only.
  ASSERT_EQ(version.rfind(core, 0), 0U) << version;
  const std::string suffix = version.substr(core.size());
  EXPECT_TRUE(suffix.empty() || suffix.front() == '-') << version;

  const auto parsed = SemVer::parse(version);
  ASSERT_TRUE(parsed.has_value()) << version;
  const auto release = SemVer::parse(core);
  ASSERT_TRUE(release.has_value());
  EXPECT_EQ(suffix.empty(), *parsed == *release) << version;
}

TEST(SdkVersionTest, AtLeastUsesLexicographicIntegerComparison) {
  static_assert(PJ_SDK_VERSION_AT_LEAST(PJ_SDK_VERSION_MAJOR, PJ_SDK_VERSION_MINOR, PJ_SDK_VERSION_PATCH));
  static_assert(!PJ_SDK_VERSION_AT_LEAST(PJ_SDK_VERSION_MAJOR + 1, 0, 0));

  EXPECT_TRUE(PJ_SDK_VERSION_AT_LEAST(0, 0, 0));
}

}  // namespace
}  // namespace PJ
