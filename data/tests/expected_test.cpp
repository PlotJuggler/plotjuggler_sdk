#include "PJ/base/expected.hpp"

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
  Expected<int, std::string> result = unexpected(std::string("boom"));

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
  Expected<int, std::string> result = unexpected(std::string("err"));

  ASSERT_FALSE(result.has_value());
  result.error().append("or");
  EXPECT_EQ(result.error(), "error");
}

}  // namespace
}  // namespace PJ
