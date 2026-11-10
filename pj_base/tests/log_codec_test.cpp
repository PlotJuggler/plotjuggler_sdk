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

TEST(LogCodecTest, SourceLocationRoundTrip) {
  Log in;
  in.timestamp_ns = 42;
  in.level = Log::Level::kError;
  in.message = "boom";
  in.name = "/node";
  in.file = "src/driver.cpp";
  in.function = "Driver::poll";
  in.line = 321;

  const auto bytes = serializeLog(in);
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, in);
}

TEST(LogCodecTest, PayloadWithoutSourceLocationDecodesToDefaults) {
  Log in;
  in.level = Log::Level::kInfo;
  in.message = "hello";
  in.name = "n";

  const auto bytes = serializeLog(in);
  // Empty / zero fields are not written: tags 5, 6 and 7 are absent.
  for (uint8_t b : bytes) {
    EXPECT_NE(b, 0x2A);  // field 5, length-delimited
    EXPECT_NE(b, 0x32);  // field 6, length-delimited
    EXPECT_NE(b, 0x38);  // field 7, varint
  }
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->file.empty());
  EXPECT_TRUE(out->function.empty());
  EXPECT_EQ(out->line, 0U);
  EXPECT_EQ(out->message, "hello");
}

TEST(LogCodecTest, UnknownFieldIsSkipped) {
  // field 8 (varint) = 1, then field 3 "hi", field 5 "a.cpp", field 7 = 9.
  const std::vector<uint8_t> bytes = {0x40, 0x01, 0x1A, 0x02, 'h', 'i',  0x2A, 0x05,
                                      'a',  '.',  'c',  'p',  'p', 0x38, 0x09};
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->message, "hi");
  EXPECT_EQ(out->file, "a.cpp");
  EXPECT_EQ(out->line, 9U);
}

TEST(LogCodecTest, WrongWireTypeForLineIsSkippedLikeAnyKnownField) {
  // Field 7 sent as length-delimited: the handler declines it and parseFields
  // skips it, as it does for every other known field. line stays 0.
  const std::vector<uint8_t> bytes = {0x1A, 0x02, 'h', 'i', 0x3A, 0x01, 0x07};
  auto out = deserializeLog(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->message, "hi");
  EXPECT_EQ(out->line, 0U);
}

}  // namespace
}  // namespace PJ
