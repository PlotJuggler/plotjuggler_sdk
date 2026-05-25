// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/expected.hpp"

#include <gtest/gtest.h>

#include <string>

namespace PJ {
namespace {

TEST(ExpectedTest, HoldsValue) {
  Expected<int, std::string> result = 42;

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedTest, HoldsError) {
  Expected<int, std::string> result = unexpected("boom");

  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(static_cast<bool>(result));
  EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, MutableAccessToValue) {
  Expected<std::string, int> result = std::string("abc");

  ASSERT_TRUE(result.has_value());
  result.value().push_back('d');
  EXPECT_EQ(result.value(), "abcd");
}

TEST(ExpectedTest, MutableAccessToError) {
  Expected<int, std::string> result = unexpected("err");

  ASSERT_FALSE(result.has_value());
  result.error().append("or");
  EXPECT_EQ(result.error(), "error");
}

TEST(ExpectedTest, AllowsValueAndErrorToUseSameType) {
  Expected<std::string> value_result = std::string("value");
  ASSERT_TRUE(value_result.has_value());
  EXPECT_EQ(value_result.value(), "value");

  Expected<std::string> error_result = unexpected("error");
  ASSERT_FALSE(error_result.has_value());
  EXPECT_EQ(error_result.error(), "error");
}

}  // namespace
}  // namespace PJ
