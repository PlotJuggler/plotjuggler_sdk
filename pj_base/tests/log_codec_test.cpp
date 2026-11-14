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
  in.source = "/storage_monitor";

  const auto bytes = serializeLog(in);
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->timestamp_ns, in.timestamp_ns);
  EXPECT_EQ(out->level, Log::Level::kWarning);
  EXPECT_EQ(out->message, in.message);
  EXPECT_EQ(out->source, in.source);
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

TEST(LogCodecTest, CategoryRoundTrips) {
  Log in;
  in.timestamp_ns = 42;
  in.level = Log::Level::kError;
  in.message = "boom";
  in.source = "/node";
  in.category = "driver";

  const auto bytes = serializeLog(in);
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, in);
}

TEST(LogCodecTest, EmptyCategoryIsNotWritten) {
  Log in;
  in.level = Log::Level::kInfo;
  in.message = "hello";
  in.source = "n";

  const auto bytes = serializeLog(in);
  for (uint8_t b : bytes) {
    EXPECT_NE(b, 0x3A);  // field 7, length-delimited
  }
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->category.empty());
  EXPECT_EQ(out->message, "hello");
}

TEST(LogCodecTest, UnknownFieldIsSkipped) {
  // field 8 (varint) = 1, then field 3 "hi", field 7 "net".
  const std::vector<uint8_t> bytes = {0x40, 0x01, 0x1A, 0x02, 'h', 'i', 0x3A, 0x03, 'n', 'e', 't'};
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->message, "hi");
  EXPECT_EQ(out->category, "net");
}

TEST(LogCodecTest, WrongWireTypeForCategoryIsSkippedLikeAnyKnownField) {
  // Field 7 sent as a varint: the handler declines it and parseFields skips it,
  // as it does for every other known field. category stays empty.
  const std::vector<uint8_t> bytes = {0x1A, 0x02, 'h', 'i', 0x38, 0x07};
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->message, "hi");
  EXPECT_TRUE(out->category.empty());
}

// foxglove.Log uses field 5 for `file` (string) and field 6 for `line`
// (fixed32). PJ.Log omits both, so a Foxglove-encoded log decodes with them
// skipped: the file path must not turn up as the category.
TEST(LogCodecTest, FoxgloveFileAndLineAreNotTheCategory) {
  const std::vector<uint8_t> bytes = {0x1A, 0x02, 'h',  'i',         // message = "hi"
                                      0x2A, 0x03, 'a',  '.',  'c',   // file = "a.c"
                                      0x35, 0x2A, 0x00, 0x00, 0x00,  // line = 42
                                      0x22, 0x01, 'n'};              // name / source = "n"
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->message, "hi");
  EXPECT_EQ(out->source, "n");
  EXPECT_TRUE(out->category.empty());
}

}  // namespace
}  // namespace PJ
