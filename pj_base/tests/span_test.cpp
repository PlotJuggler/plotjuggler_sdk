// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace PJ {
namespace {

TEST(SpanTest, ConstructFromPointerAndSize) {
  int values[] = {1, 2, 3};
  Span<int> span(values, 3);

  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
  EXPECT_EQ(span[0], 1);
  EXPECT_EQ(span[2], 3);
}

TEST(SpanTest, ConstructFromVector) {
  std::vector<int32_t> values = {10, 20, 30};
  Span<int32_t> span(values);

  EXPECT_EQ(span.size(), 3u);
  EXPECT_EQ(span.front(), 10);
  EXPECT_EQ(span.back(), 30);
}

TEST(SpanTest, ConstructConstFromConstVector) {
  const std::vector<int32_t> values = {7, 8};
  Span<const int32_t> span(values);

  EXPECT_EQ(span.size(), 2u);
  EXPECT_EQ(span[1], 8);
}

TEST(SpanTest, ConstructFromStdArray) {
  std::array<uint64_t, 2> values = {42, 99};
  Span<uint64_t> span(values);

  EXPECT_EQ(span.size(), 2u);
  EXPECT_EQ(span[0], 42u);
  EXPECT_EQ(span[1], 99u);
}

TEST(SpanTest, SubspanWorks) {
  int values[] = {5, 6, 7, 8};
  Span<int> span(values);

  auto sub = span.subspan(1, 2);
  EXPECT_EQ(sub.size(), 2u);
  EXPECT_EQ(sub[0], 6);
  EXPECT_EQ(sub[1], 7);
}

TEST(SpanTest, IterationWorks) {
  int values[] = {1, 2, 3, 4};
  Span<int> span(values);

  int sum = 0;
  for (int v : span) {
    sum += v;
  }

  EXPECT_EQ(sum, 10);
}

}  // namespace
}  // namespace PJ
