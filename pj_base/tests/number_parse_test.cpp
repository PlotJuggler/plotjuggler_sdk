// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/number_parse.hpp"

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace PJ {
namespace {

TEST(ParseNumber, IntegerSuccess) {
  EXPECT_EQ(parseNumber<int>("0"), 0);
  EXPECT_EQ(parseNumber<int>("42"), 42);
  EXPECT_EQ(parseNumber<int>("-7"), -7);
  EXPECT_EQ(parseNumber<std::int32_t>("-2147483648"), std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(parseNumber<std::int32_t>("2147483647"), std::numeric_limits<std::int32_t>::max());
  EXPECT_EQ(parseNumber<std::uint64_t>("18446744073709551615"), std::numeric_limits<std::uint64_t>::max());
}

TEST(ParseNumber, IntegerFailure) {
  EXPECT_FALSE(parseNumber<int>("").has_value());
  EXPECT_FALSE(parseNumber<int>("hello").has_value());
  EXPECT_FALSE(parseNumber<int>("42xyz").has_value());
  EXPECT_FALSE(parseNumber<int>("  42").has_value());
  EXPECT_FALSE(parseNumber<int>("42 ").has_value());
  EXPECT_FALSE(parseNumber<int>("-").has_value());
  // std::from_chars rejects a leading '+' for integers (it accepts only '-').
  EXPECT_FALSE(parseNumber<int>("+13").has_value());
  // Floats are not integers.
  EXPECT_FALSE(parseNumber<int>("1.5").has_value());
  // Out-of-range.
  EXPECT_FALSE(parseNumber<std::int8_t>("200").has_value());
  EXPECT_FALSE(parseNumber<std::uint32_t>("-1").has_value());
}

TEST(ParseNumber, FloatSuccess) {
  EXPECT_DOUBLE_EQ(*parseNumber<double>("0"), 0.0);
  EXPECT_DOUBLE_EQ(*parseNumber<double>("1.5"), 1.5);
  EXPECT_DOUBLE_EQ(*parseNumber<double>("-2.5e3"), -2500.0);
  EXPECT_DOUBLE_EQ(*parseNumber<double>(".5"), 0.5);
  EXPECT_DOUBLE_EQ(*parseNumber<double>("5."), 5.0);
  EXPECT_FLOAT_EQ(*parseNumber<float>("3.14"), 3.14f);
  EXPECT_TRUE(std::isinf(*parseNumber<double>("inf")));
  EXPECT_TRUE(std::isnan(*parseNumber<double>("nan")));
}

TEST(ParseNumber, FloatFailure) {
  EXPECT_FALSE(parseNumber<double>("").has_value());
  EXPECT_FALSE(parseNumber<double>("hello").has_value());
  EXPECT_FALSE(parseNumber<double>("1.5xyz").has_value());
  EXPECT_FALSE(parseNumber<double>("  1.5").has_value());
  EXPECT_FALSE(parseNumber<double>("1.5 ").has_value());
  // Comma is NOT a valid decimal mark — locale-independent.
  EXPECT_FALSE(parseNumber<double>("1,5").has_value());
}

TEST(ParseNumber, FloatLocaleIndependent) {
  // Regression: std::strto* respects LC_NUMERIC and would parse "1.5" as
  // 1 under de_DE.UTF-8. parseNumber<double> is locale-independent because
  // fast_float backs the float path.
  const char* prev = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = prev != nullptr ? std::string(prev) : std::string("C");
  // Try common spellings; if none of them are installed, the test still
  // passes because the assertion below should hold under "C" too — but
  // the locale regression we care about only triggers when a non-C
  // LC_NUMERIC is actually active.
  const char* candidates[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252"};
  bool changed = false;
  for (const char* loc : candidates) {
    if (std::setlocale(LC_NUMERIC, loc) != nullptr) {
      changed = true;
      break;
    }
  }
  EXPECT_DOUBLE_EQ(*parseNumber<double>("1.5"), 1.5);
  EXPECT_DOUBLE_EQ(*parseNumber<double>("-2.5e3"), -2500.0);
  EXPECT_FALSE(parseNumber<double>("1,5").has_value());
  std::setlocale(LC_NUMERIC, saved.c_str());
  // We don't fail the test if no de_DE locale was installed — but we do
  // want to be loud about it in CI so the regression check is meaningful.
  if (!changed) {
    GTEST_LOG_(WARNING) << "No German locale installed; locale regression check ran under C only.";
  }
}

TEST(ParseNumber, LongDoubleSmoke) {
  // long double falls back to strtold (fast_float does not implement it).
  // Still must obey the whole-string contract.
  auto v = parseNumber<long double>("1.5");
  ASSERT_TRUE(v.has_value());
  EXPECT_NEAR(static_cast<double>(*v), 1.5, 1e-9);
  EXPECT_FALSE(parseNumber<long double>("1.5xyz").has_value());
  EXPECT_FALSE(parseNumber<long double>("").has_value());
}

}  // namespace
}  // namespace PJ
