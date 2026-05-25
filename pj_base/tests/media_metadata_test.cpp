// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/media_metadata.hpp"

#include <gtest/gtest.h>

#include <string>

namespace PJ::sdk {
namespace {

TEST(MediaMetadataBuilderTest, EmptyBuilderEmitsEmptyObject) {
  EXPECT_EQ(MediaMetadataBuilder().build(), "{}");
}

TEST(MediaMetadataBuilderTest, SingleKeyRoundtrip) {
  EXPECT_EQ(MediaMetadataBuilder().mediaClass("image").build(), R"({"media_class":"image"})");
  EXPECT_EQ(MediaMetadataBuilder().encoding("jpeg").build(), R"({"encoding":"jpeg"})");
  EXPECT_EQ(
      MediaMetadataBuilder().schema("sensor_msgs/CompressedImage").build(),
      R"({"schema":"sensor_msgs/CompressedImage"})");
}

TEST(MediaMetadataBuilderTest, AllThreeKeysInCanonicalOrder) {
  const auto json = MediaMetadataBuilder().mediaClass("video").encoding("h264").schema("PJ.VideoFrame").build();
  EXPECT_EQ(json, R"({"media_class":"video","encoding":"h264","schema":"PJ.VideoFrame"})");
}

TEST(MediaMetadataBuilderTest, EmptyKeysAreOmitted) {
  const auto json = MediaMetadataBuilder().mediaClass("image").schema("").build();
  EXPECT_EQ(json, R"({"media_class":"image"})");
}

TEST(MediaMetadataBuilderTest, ExtraStringIsQuoted) {
  const auto json = MediaMetadataBuilder().mediaClass("image").extraString("source", "camera_0").build();
  EXPECT_EQ(json, R"({"media_class":"image","source":"camera_0"})");
}

TEST(MediaMetadataBuilderTest, ExtraRawJsonIsPassedThrough) {
  const auto json = MediaMetadataBuilder().mediaClass("video").extra("width", "1920").extra("height", "1080").build();
  EXPECT_EQ(json, R"({"media_class":"video","width":1920,"height":1080})");
}

TEST(MediaMetadataBuilderTest, EscapesQuotesAndBackslashes) {
  // Use ordinary escaped string literals (not raw strings) because the
  // MSVC preprocessor on the CI runner mishandles raw-string tokenization
  // when the body contains `"` and `\` — it drops out of raw-string mode
  // and reinterprets the tail as a user-defined literal suffix. Escaped
  // literals carry identical bytes and are accepted by every compiler.
  const auto json = MediaMetadataBuilder().schema("weird\"name\\with").build();
  EXPECT_EQ(json, "{\"schema\":\"weird\\\"name\\\\with\"}");
}

TEST(MediaMetadataBuilderTest, EscapesControlChars) {
  std::string schema;
  schema.push_back('\n');
  schema.push_back('\t');
  schema.push_back('\x01');
  const auto json = MediaMetadataBuilder().schema(schema).build();
  // Short escapes for \n and \t; hex escape for other control chars.
  std::string expected = "{\"schema\":\"\\n\\t\\u0001\"}";
  EXPECT_EQ(json, expected);
}

TEST(MediaMetadataBuilderTest, MultipleExtrasChain) {
  const auto json =
      MediaMetadataBuilder().mediaClass("image").extraString("frame_id", "base_link").extra("fps", "30.0").build();
  EXPECT_EQ(json, R"({"media_class":"image","frame_id":"base_link","fps":30.0})");
}

}  // namespace
}  // namespace PJ::sdk
