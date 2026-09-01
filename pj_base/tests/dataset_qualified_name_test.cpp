// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/dataset_qualified_name.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace PJ::sdk {
namespace {

TEST(SplitDatasetQualifier, MatchesOnlyKnownSourceNames) {
  const std::vector<std::string> sources = {"run_a.mcap", "run_b.mcap"};
  const auto hit = splitDatasetQualifier("run_a.mcap:/speed/value", sources);
  EXPECT_TRUE(hit.qualified);
  EXPECT_EQ(hit.dataset_source, "run_a.mcap");
  EXPECT_EQ(hit.bare, "/speed/value");

  // A ':' whose prefix is no loaded source is part of the name, not a qualifier.
  const std::vector<std::string> other = {"other.mcap"};
  const auto miss = splitDatasetQualifier("run_a.mcap:/speed/value", other);
  EXPECT_FALSE(miss.qualified);
  EXPECT_TRUE(miss.dataset_source.empty());
  EXPECT_EQ(miss.bare, "run_a.mcap:/speed/value");
}

TEST(SplitDatasetQualifier, StreamStyleNamesNeedNoEscaping) {
  const std::vector<std::string> sources = {"[stream] UDP Server"};
  const auto split = splitDatasetQualifier("[stream] UDP Server:/udp/data/value", sources);
  EXPECT_TRUE(split.qualified);
  EXPECT_EQ(split.dataset_source, "[stream] UDP Server");
  EXPECT_EQ(split.bare, "/udp/data/value");
}

TEST(SplitDatasetQualifier, LongestKnownNameWins) {
  const std::vector<std::string> sources = {"a", "a:b"};
  const auto split = splitDatasetQualifier("a:b:/t/f", sources);
  EXPECT_TRUE(split.qualified);
  EXPECT_EQ(split.dataset_source, "a:b");
  EXPECT_EQ(split.bare, "/t/f");
}

TEST(SplitDatasetQualifier, EmptyAndDegenerateInputs) {
  const std::vector<std::string> sources = {"", "run_a.mcap"};
  // An empty source name never qualifies anything.
  EXPECT_FALSE(splitDatasetQualifier(":/speed/value", sources).qualified);
  // The bare name alone (no ':' after a known source) stays bare.
  EXPECT_FALSE(splitDatasetQualifier("run_a.mcap", sources).qualified);
  // A qualifier with nothing after the ':' splits to an empty bare name.
  const auto empty_bare = splitDatasetQualifier("run_a.mcap:", sources);
  EXPECT_TRUE(empty_bare.qualified);
  EXPECT_EQ(empty_bare.bare, "");
  // No datasets loaded: nothing can qualify.
  EXPECT_FALSE(splitDatasetQualifier("run_a.mcap:/speed/value", {}).qualified);
}

TEST(QualifiedSeriesName, RoundTripsThroughSplit) {
  const std::vector<std::string> sources = {"run_a.mcap", "session 12.mcap"};
  const std::string qualified = qualifiedSeriesName("session 12.mcap", "/imu/accel/x");
  EXPECT_EQ(qualified, "session 12.mcap:/imu/accel/x");

  const auto split = splitDatasetQualifier(qualified, sources);
  EXPECT_TRUE(split.qualified);
  EXPECT_EQ(split.dataset_source, "session 12.mcap");
  EXPECT_EQ(split.bare, "/imu/accel/x");
}

}  // namespace
}  // namespace PJ::sdk
