// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// The strict fail-closed origin parse that trust and credential-release
// decisions ride on: every rejected shape must stay rejected (a parse that
// starts accepting userinfo or bracketed IPv6 silently widens what a secret
// can be released to).

#include <gtest/gtest.h>

#include "pj_base/sdk/descriptor_import/origin.hpp"

namespace {

using PJ::sdk::descriptor_import::Origin;
using PJ::sdk::descriptor_import::originAllowed;
using PJ::sdk::descriptor_import::originKey;
using PJ::sdk::descriptor_import::OriginPolicy;
using PJ::sdk::descriptor_import::parseOrigin;
using PJ::sdk::descriptor_import::parseOriginList;
using PJ::sdk::descriptor_import::sameOrigin;

OriginPolicy grpcPolicy() {
  OriginPolicy policy;
  policy.allowed_schemes = {"grpc", "grpc+tls"};
  return policy;
}

TEST(DescriptorImportOrigin, ParsesCanonicalTlsUri) {
  const auto origin = parseOrigin("grpc+tls://demo.mosaico.dev:6726", grpcPolicy());
  ASSERT_TRUE(origin.has_value());
  EXPECT_EQ(origin->scheme, "grpc+tls");
  EXPECT_EQ(origin->host, "demo.mosaico.dev");
  EXPECT_EQ(origin->port, 6726);
  EXPECT_EQ(originKey(*origin), "grpc+tls://demo.mosaico.dev:6726");
}

TEST(DescriptorImportOrigin, LowercasesSchemeAndHostAndIgnoresPath) {
  const auto origin = parseOrigin("GRPC+TLS://Demo.Mosaico.DEV:6726/some/path", grpcPolicy());
  ASSERT_TRUE(origin.has_value());
  EXPECT_EQ(origin->scheme, "grpc+tls");
  EXPECT_EQ(origin->host, "demo.mosaico.dev");
  EXPECT_EQ(origin->port, 6726);
}

TEST(DescriptorImportOrigin, PlaintextSchemeIsDistinctFromTls) {
  ASSERT_TRUE(parseOrigin("grpc://host:1", grpcPolicy()).has_value());
  EXPECT_FALSE(sameOrigin("grpc://host:1", "grpc+tls://host:1", grpcPolicy()));
}

TEST(DescriptorImportOrigin, RejectedShapes) {
  const auto policy = grpcPolicy();
  EXPECT_FALSE(parseOrigin("", policy).has_value());
  EXPECT_FALSE(parseOrigin("demo.mosaico.dev:6726", policy).has_value());          // no scheme
  EXPECT_FALSE(parseOrigin("https://demo.mosaico.dev:6726", policy).has_value());  // scheme not allowed
  EXPECT_FALSE(parseOrigin("grpc+tls://demo.mosaico.dev", policy).has_value());    // port required
  EXPECT_FALSE(parseOrigin("grpc+tls://demo.mosaico.dev:", policy).has_value());
  EXPECT_FALSE(parseOrigin("grpc+tls://:6726", policy).has_value());  // empty host
  EXPECT_FALSE(parseOrigin("grpc+tls://user@host:6726", policy).has_value());
  EXPECT_FALSE(parseOrigin("grpc+tls://host:6726?query=1", policy).has_value());
  EXPECT_FALSE(parseOrigin("grpc+tls://host:6726#frag", policy).has_value());
  EXPECT_FALSE(parseOrigin("grpc+tls://[::1]:6726", policy).has_value());  // IPv6 unsupported
  EXPECT_FALSE(parseOrigin("grpc+tls://host:0", policy).has_value());      // port range
  EXPECT_FALSE(parseOrigin("grpc+tls://host:65536", policy).has_value());
  EXPECT_FALSE(parseOrigin("grpc+tls://host:12ab", policy).has_value());
  EXPECT_FALSE(parseOrigin("://host:1", policy).has_value());  // empty scheme
}

TEST(DescriptorImportOrigin, EmptySchemeListFailsClosed) {
  // A provider must name the schemes its transport speaks; an empty policy
  // accepts nothing.
  const OriginPolicy none;
  EXPECT_FALSE(parseOrigin("grpc://host:1", none).has_value());
  EXPECT_FALSE(parseOrigin("wss://host:443", none).has_value());
}

TEST(DescriptorImportOrigin, DefaultPortsApplyOnlyToListedSchemes) {
  OriginPolicy policy;
  policy.allowed_schemes = {"https", "http"};
  policy.default_ports = {{"https", 443}};
  const auto tls = parseOrigin("https://example.org/path", policy);
  ASSERT_TRUE(tls.has_value());
  EXPECT_EQ(tls->port, 443);
  EXPECT_EQ(tls->host, "example.org");
  // Explicit port wins over the default.
  EXPECT_EQ(parseOrigin("https://example.org:8443", policy)->port, 8443);
  // http has no default: an explicit port stays required.
  EXPECT_FALSE(parseOrigin("http://example.org", policy).has_value());
}

TEST(DescriptorImportOrigin, SameOriginComparisons) {
  const auto policy = grpcPolicy();
  EXPECT_TRUE(sameOrigin("grpc+tls://Host:6726", "grpc+tls://host:6726/path", policy));
  EXPECT_FALSE(sameOrigin("grpc+tls://host:6726", "grpc+tls://host:6727", policy));
  EXPECT_FALSE(sameOrigin("grpc+tls://a:1", "grpc+tls://b:1", policy));
  // Rejected shapes never match, not even themselves.
  EXPECT_FALSE(sameOrigin("host:1", "host:1", policy));
}

TEST(DescriptorImportOrigin, ListParsingDropsUnparsableEntries) {
  const auto policy = grpcPolicy();
  const auto list = parseOriginList(" grpc+tls://a:1, junk ;grpc://B:2\n\tgrpc+tls://c", policy);
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(originKey(list[0]), "grpc+tls://a:1");
  EXPECT_EQ(originKey(list[1]), "grpc://b:2");
  EXPECT_TRUE(parseOriginList("", policy).empty());
}

TEST(DescriptorImportOrigin, AllowlistMatchesExactOriginOnly) {
  const auto policy = grpcPolicy();
  const auto allow = parseOriginList("grpc+tls://demo.mosaico.dev:6726", policy);
  EXPECT_TRUE(originAllowed("GRPC+TLS://demo.mosaico.dev:6726/x", allow, policy));
  EXPECT_FALSE(originAllowed("grpc://demo.mosaico.dev:6726", allow, policy));  // scheme differs
  EXPECT_FALSE(originAllowed("grpc+tls://demo.mosaico.dev:6727", allow, policy));
  EXPECT_FALSE(originAllowed("not a uri", allow, policy));
  EXPECT_FALSE(originAllowed("grpc+tls://demo.mosaico.dev:6726", {}, policy));
}

}  // namespace
