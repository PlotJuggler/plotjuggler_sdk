// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/version.hpp"

#include <gtest/gtest.h>

#include <string>

namespace PJ {
namespace {

TEST(SdkVersionTest, ExposesConfiguredProjectVersion) {
  const std::string expected = std::to_string(PJ_SDK_VERSION_MAJOR) + "." + std::to_string(PJ_SDK_VERSION_MINOR) + "." +
                               std::to_string(PJ_SDK_VERSION_PATCH);
  EXPECT_EQ(sdkVersion(), expected);
}

TEST(SdkVersionTest, AtLeastUsesLexicographicIntegerComparison) {
  static_assert(PJ_SDK_VERSION_AT_LEAST(PJ_SDK_VERSION_MAJOR, PJ_SDK_VERSION_MINOR, PJ_SDK_VERSION_PATCH));
  static_assert(!PJ_SDK_VERSION_AT_LEAST(PJ_SDK_VERSION_MAJOR + 1, 0, 0));

  EXPECT_TRUE(PJ_SDK_VERSION_AT_LEAST(0, 0, 0));
}

}  // namespace
}  // namespace PJ
