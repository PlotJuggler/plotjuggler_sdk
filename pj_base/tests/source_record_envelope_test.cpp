// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <limits>

#include "pj_base/sdk/source/record_envelope.hpp"

namespace PJ::sdk::source {
namespace {

nlohmann::json envelope() {
  return {
      {"kind", "example.pull"},
      {"v", 1},
      {"request", {{"topics", {"/imu"}}, {"range", {"0", "10"}}}},
      {"label", "Run"}};
}

TEST(SourceRecordEnvelope, ValidNestedShapeAndVerbatimVersion) {
  const auto bytes = envelope().dump();
  const auto parsed = parseSourceRecordEnvelope(bytes);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed, envelope());
  for (auto version : {std::uint64_t{0}, std::uint64_t{4294967297}, std::numeric_limits<std::uint64_t>::max()}) {
    auto value = envelope();
    value["v"] = version;
    const auto result = parseSourceRecordEnvelope(value.dump());
    ASSERT_TRUE(result);
    EXPECT_EQ((*result)["v"], version);  // The envelope never selects a provider schema version.
  }
  auto value = envelope();
  value.erase("label");
  EXPECT_TRUE(parseSourceRecordEnvelope(value.dump()));
}

TEST(SourceRecordEnvelope, MissingAndMistypedFields) {
  for (const char* field : {"kind", "v", "request"}) {
    auto value = envelope();
    value.erase(field);
    EXPECT_FALSE(parseSourceRecordEnvelope(value.dump())) << field;
  }
  for (const auto& value :
       {nlohmann::json(nullptr), nlohmann::json(1), nlohmann::json(true), nlohmann::json::array(),
        nlohmann::json::object()}) {
    auto record = envelope();
    record["kind"] = value;
    EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
    record = envelope();
    record["label"] = value;
    EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  }
  auto record = envelope();
  record["kind"] = "";
  EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  for (const auto& value :
       {nlohmann::json(nullptr), nlohmann::json(-1), nlohmann::json(1.0), nlohmann::json(true), nlohmann::json("1"),
        nlohmann::json::array()}) {
    record = envelope();
    record["v"] = value;
    EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  }
  for (const auto& value :
       {nlohmann::json(nullptr), nlohmann::json(1), nlohmann::json("request"), nlohmann::json(true),
        nlohmann::json::array()}) {
    record = envelope();
    record["request"] = value;
    EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  }
}

TEST(SourceRecordEnvelope, RefusalCategoriesAndBounds) {
  EXPECT_EQ(parseSourceRecordEnvelope("").error(), "source record descriptor is empty");
  EXPECT_EQ(parseSourceRecordEnvelope("{").error(), "source record refused: descriptor is not valid JSON");
  EXPECT_EQ(parseSourceRecordEnvelope("[]").error(), "source record refused: descriptor is not a JSON object");
  EXPECT_EQ(
      parseSourceRecordEnvelope(std::string(65537, ' ')).error(),
      "source record refused: descriptor exceeds the 65536-byte limit");
  EXPECT_TRUE(parseSourceRecordEnvelope(envelope().dump() + std::string(65536 - envelope().dump().size(), ' ')));
  auto record = envelope();
  record["label"] = std::string(4097, 'a');
  EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  record = envelope();
  record["request"]["topics"] = std::vector<int>(4097, 1);
  EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
  record = envelope();
  nlohmann::json nested = 0;
  for (int depth = 0; depth < 18; ++depth) {
    nested = nlohmann::json::array({nested});
  }
  record["request"]["nested"] = nested;
  EXPECT_FALSE(parseSourceRecordEnvelope(record.dump()));
}

TEST(SourceRecordEnvelope, CredentialKeysAtDepthAreRejectedExactlyLikeHost) {
  for (const char* key :
       {"api_key", "apikey", "token", "password", "secret", "credentials", "authorization", "cert_path"}) {
    auto record = envelope();
    record["request"]["nested"] = nlohmann::json::array({{{"deeper", {{key, "value"}}}}});
    EXPECT_EQ(
        parseSourceRecordEnvelope(record.dump()).error(),
        std::string("source record carries credential-shaped key: ") + key);
  }
  auto record = envelope();
  record["request"]["Token"] = "case-sensitive host policy";
  record["request"]["value"] = "token";
  EXPECT_TRUE(parseSourceRecordEnvelope(record.dump()));
}

TEST(SourceRecordEnvelope, FlatMcapCloudV1IsAnExplicitRejectedVector) {
  // Frozen pre-envelope shape. Adoption requires a versioned restore adapter,
  // never silently wrapping these identity bytes into a request object.
  constexpr auto kFlatV1 =
      R"({"v":1,"kind":"mcap-cloud-session","server_uri":"ws://localhost:8080","s3_keys":["a.mcap"],"topics":[],"start_ns":"0","end_ns":"0","include_latched":true,"display_name":"A"})";
  const auto result = parseSourceRecordEnvelope(kFlatV1);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), "source record refused: unknown field \"display_name\"");
}

}  // namespace
}  // namespace PJ::sdk::source
