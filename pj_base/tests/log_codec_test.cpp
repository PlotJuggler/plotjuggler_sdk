// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/log_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace PJ {
namespace {

using sdk::Log;

TEST(LogCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaLog, "PJ.Log");
}

TEST(LogCodecTest, EmptyBufferDeserializesAsError) {
  EXPECT_FALSE(deserializeLog(nullptr, 0).has_value());
}

TEST(LogCodecTest, RoundTrip) {
  Log in;
  in.timestamp_ns = 1'234'567'890LL;
  in.level = Log::Level::kWarning;
  in.message = "disk almost full";
  in.name = "/storage_monitor";

  const auto bytes = serializeLog(in);
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->level, Log::Level::kWarning);
  EXPECT_EQ(out->message, in.message);
  EXPECT_EQ(out->name, in.name);
}

TEST(LogCodecTest, EveryLevelRoundTrips) {
  for (auto level :
       {Log::Level::kUnknown, Log::Level::kDebug, Log::Level::kInfo, Log::Level::kWarning, Log::Level::kError,
        Log::Level::kFatal}) {
    Log in;
    in.level = level;
    in.message = "x";
    const auto bytes = serializeLog(in);
    auto out = deserializeLog(bytes.data(), bytes.size());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->level, level);
  }
}

}  // namespace
}  // namespace PJ
