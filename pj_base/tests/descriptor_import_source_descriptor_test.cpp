// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Allowlist, bounds, canonical bytes and identity. The Mosaico vectors below
// are the contract the first consumer pinned before this code moved into
// the SDK: the SDK must reproduce those bytes and identities verbatim.

#include <gtest/gtest.h>

#include <string>

#include "pj_base/sdk/descriptor_import/source_descriptor.hpp"

namespace {

using PJ::sdk::descriptor_import::canonicalSourceDescriptorJson;
using PJ::sdk::descriptor_import::IdentityScheme;
using PJ::sdk::descriptor_import::parseSourceDescriptor;
using PJ::sdk::descriptor_import::sha256Hex;
using PJ::sdk::descriptor_import::sourceDescriptorIdentity;
using PJ::sdk::descriptor_import::SourceDescriptorPolicy;

SourceDescriptorPolicy mosaicoPolicy() {
  SourceDescriptorPolicy policy;
  policy.identity_fields = {"v", "kind", "server_uri", "sequence", "topics", "start_ns", "end_ns"};
  policy.presentation_fields = {"display_name"};
  policy.identity = IdentityScheme{"mosaico:v1:sha256/128:", 32};
  return policy;
}

struct Vector {
  const char* descriptor;
  const char* canonical;
  const char* identity;
};

// docs/source-descriptor-vectors.json of the Mosaico connector.
const Vector kVectors[] = {
    {R"({"v":1,"kind":"mosaico-sequence","server_uri":"grpc+tls://demo.mosaico.dev:6726","sequence":"indoor_45_11_davis","topics":["/dvs/imu"],"start_ns":"0","end_ns":"0","display_name":"IMU only"})",
     R"({"end_ns":"0","kind":"mosaico-sequence","sequence":"indoor_45_11_davis","server_uri":"grpc+tls://demo.mosaico.dev:6726","start_ns":"0","topics":["/dvs/imu"],"v":1})",
     "mosaico:v1:sha256/128:8311e64d0bc59a28f97bc7070d5e1b3f"},
    {R"({"v":1,"kind":"mosaico-sequence","server_uri":"grpc+tls://demo.mosaico.dev:6726","sequence":"bonirob_2016-04-20-15-42-15_7","topics":["/camera/image_raw","/gps","/imu"],"start_ns":"1461159735713519717","end_ns":"1461159795713519718","display_name":"Bonirob run 7"})",
     R"({"end_ns":"1461159795713519718","kind":"mosaico-sequence","sequence":"bonirob_2016-04-20-15-42-15_7","server_uri":"grpc+tls://demo.mosaico.dev:6726","start_ns":"1461159735713519717","topics":["/camera/image_raw","/gps","/imu"],"v":1})",
     "mosaico:v1:sha256/128:1fda8e70c5503fa9d259b914268321a9"},
    {R"({"v":1,"kind":"mosaico-sequence","server_uri":"grpc://localhost:6726","sequence":"seq","topics":["/a"],"start_ns":"5","end_ns":"9","display_name":"RENAMED"})",
     R"({"end_ns":"9","kind":"mosaico-sequence","sequence":"seq","server_uri":"grpc://localhost:6726","start_ns":"5","topics":["/a"],"v":1})",
     "mosaico:v1:sha256/128:6bf67ce6da74addf1d300d28be18849f"},
};

TEST(SourceDescriptor, ReproducesTheMosaicoVectors) {
  const auto policy = mosaicoPolicy();
  for (const Vector& v : kVectors) {
    const auto parsed = parseSourceDescriptor(v.descriptor, policy);
    ASSERT_TRUE(parsed) << parsed.error();
    EXPECT_EQ(canonicalSourceDescriptorJson(*parsed, policy), v.canonical);
    EXPECT_EQ(sourceDescriptorIdentity(*parsed, policy), v.identity);
    EXPECT_EQ(policy.identity.identityFor(v.canonical), v.identity);
    // The canonical form itself parses and is a fixed point.
    const auto twin = parseSourceDescriptor(v.canonical, policy);
    ASSERT_TRUE(twin) << twin.error();
    EXPECT_EQ(canonicalSourceDescriptorJson(*twin, policy), v.canonical);
  }
}

TEST(SourceDescriptor, PresentationFieldsDoNotChangeIdentity) {
  const auto policy = mosaicoPolicy();
  auto a = *parseSourceDescriptor(kVectors[2].descriptor, policy);
  auto b = a;
  b["display_name"] = "something else";
  EXPECT_EQ(sourceDescriptorIdentity(a, policy), sourceDescriptorIdentity(b, policy));
  b["topics"] = {"/b"};
  EXPECT_NE(sourceDescriptorIdentity(a, policy), sourceDescriptorIdentity(b, policy));
}

TEST(SourceDescriptor, RejectsUnknownFieldsEvenWhenOtherwiseValid) {
  const auto policy = mosaicoPolicy();
  for (const char* smuggled : {"api_key", "cert_path", "allow_insecure", "future_field"}) {
    std::string json = std::string(kVectors[2].descriptor);
    json.insert(1, std::string("\"") + smuggled + "\":\"x\",");
    const auto parsed = parseSourceDescriptor(json, policy);
    ASSERT_FALSE(parsed) << smuggled;
    EXPECT_NE(parsed.error().find("unknown field"), std::string::npos) << parsed.error();
  }
}

TEST(SourceDescriptor, RejectsMalformedRootsAndOversizedText) {
  auto policy = mosaicoPolicy();
  EXPECT_FALSE(parseSourceDescriptor("not json", policy));
  EXPECT_FALSE(parseSourceDescriptor("[1,2]", policy));
  EXPECT_FALSE(parseSourceDescriptor("\"text\"", policy));
  policy.max_descriptor_bytes = 8;
  const auto too_big = parseSourceDescriptor(R"({"v":1,"kind":"x"})", policy);
  ASSERT_FALSE(too_big);
  EXPECT_NE(too_big.error().find("8-byte limit"), std::string::npos);
}

TEST(SourceDescriptor, EnforcesStringContainerAndDepthBounds) {
  auto policy = mosaicoPolicy();
  policy.max_string_bytes = 8;  // keys are bounded too: "topics" (6) fits, "toolongvalue" (12) does not
  policy.max_container_entries = 2;
  EXPECT_FALSE(parseSourceDescriptor(R"({"kind":"toolongvalue"})", policy));
  EXPECT_FALSE(parseSourceDescriptor(R"({"topics":["a","b","c"]})", policy));
  EXPECT_TRUE(parseSourceDescriptor(R"({"topics":["a","b"]})", policy));
  // Nested strings are bounded too.
  EXPECT_FALSE(parseSourceDescriptor(R"({"topics":[["toolongvalue"]]})", policy));
  // A key over the limit is rejected even when its value is fine.
  auto tight = policy;
  tight.max_string_bytes = 4;
  const auto long_key = parseSourceDescriptor(R"({"topics":["a"]})", tight);
  ASSERT_FALSE(long_key);
  EXPECT_NE(long_key.error().find("key"), std::string::npos) << long_key.error();
  // Depth: 18 nested arrays inside an identity field exceed the 16-level cap.
  std::string deep = R"({"topics":)";
  for (int i = 0; i < 18; ++i) {
    deep += "[";
  }
  for (int i = 0; i < 18; ++i) {
    deep += "]";
  }
  deep += "}";
  policy.max_container_entries = 4096;
  const auto too_deep = parseSourceDescriptor(deep, policy);
  ASSERT_FALSE(too_deep);
  EXPECT_NE(too_deep.error().find("deeper"), std::string::npos) << too_deep.error();
}

TEST(IdentitySchemeTest, DigestValidatesShapeExactly) {
  const IdentityScheme scheme{"mosaico:v1:sha256/128:", 32};
  const std::string hex(32, 'a');
  EXPECT_EQ(scheme.digestOf("mosaico:v1:sha256/128:" + hex), hex);
  EXPECT_FALSE(scheme.digestOf("").has_value());
  EXPECT_FALSE(scheme.digestOf("mosaico:v1:sha256/128:short").has_value());
  EXPECT_FALSE(scheme.digestOf("mcap-cloud:v1:sha256/128:" + hex).has_value());
  EXPECT_FALSE(scheme.digestOf("mosaico:v1:sha256/128:" + std::string(32, 'A')).has_value());
  EXPECT_FALSE(scheme.digestOf("mosaico:v1:sha256/128:" + std::string(31, 'a') + "g").has_value());
  EXPECT_FALSE(scheme.digestOf("mosaico:v1:sha256/128:" + hex + "x").has_value());
}

TEST(IdentitySchemeTest, MintedIdentitiesAlwaysParseUnderTheSameScheme) {
  // Out-of-range digest widths are normalized identically on both sides —
  // with a 128-bit FLOOR, so a config typo can never mint a trivially
  // collidable (e.g. 8-bit) content address.
  for (const std::size_t requested : {std::size_t{0}, std::size_t{3}, std::size_t{33}, std::size_t{200}}) {
    const IdentityScheme scheme{"p:", requested};
    const std::string identity = scheme.identityFor("payload");
    const auto digest = scheme.digestOf(identity);
    ASSERT_TRUE(digest.has_value()) << "requested " << requested;
    EXPECT_EQ(digest->size(), scheme.hexChars());
    EXPECT_GE(scheme.hexChars(), 32u);
    EXPECT_LE(scheme.hexChars(), 64u);
  }
  EXPECT_EQ((IdentityScheme{"p:", 0}).hexChars(), 32u);    // floor: 128 bits
  EXPECT_EQ((IdentityScheme{"p:", 48}).hexChars(), 48u);   // in range: kept
  EXPECT_EQ((IdentityScheme{"p:", 33}).hexChars(), 32u);   // odd rounds down
  EXPECT_EQ((IdentityScheme{"p:", 200}).hexChars(), 64u);  // cap: sha256 width
}

TEST(SourceDescriptor, Sha256NistVectors) {
  EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(
      sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  // One-million 'a' (block-boundary coverage).
  EXPECT_EQ(sha256Hex(std::string(1'000'000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  EXPECT_EQ(sha256Hex("abc", 32), "ba7816bf8f01cfea414140de5dae2223");
  // The 128-bit floor applies here too: sub-32 requests widen, never narrow.
  EXPECT_EQ(sha256Hex("abc", 3), "ba7816bf8f01cfea414140de5dae2223");
  EXPECT_EQ(sha256Hex("abc", 0), "ba7816bf8f01cfea414140de5dae2223");
  EXPECT_EQ(sha256Hex("abc", 33), "ba7816bf8f01cfea414140de5dae2223");  // odd rounds down
  EXPECT_EQ(sha256Hex("abc", 200).size(), 64u);
}

}  // namespace
