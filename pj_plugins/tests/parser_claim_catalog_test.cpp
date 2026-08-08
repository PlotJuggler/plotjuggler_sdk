// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_claim_catalog.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_module_abi.h"
#include "pj_base/parser_route_claims_protocol.h"

namespace PJ {
namespace {

DiagnosticSink recordInto(std::vector<Diagnostic>& diagnostics) {
  return [&diagnostics](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); };
}

ParserClaim scalarClaim(std::string provider = "provider", std::string claim_id = "claim") {
  return ParserClaim{
      .encoding = "protobuf",
      .type_name = "foxglove.PointCloud",
      .route_flags = PJ_PARSER_ROUTE_FLAG_SCALAR_V1,
      .object_type = std::nullopt,
      .schema_digests = {},
      .provider_id = std::move(provider),
      .claim_id = std::move(claim_id),
      .priority = 0,
      .provenance = ParserClaimProvenance::kFolderDrop,
  };
}

ParserClaim objectClaim(std::string type_name = "sensor_msgs/msg/PointCloud2") {
  return ParserClaim{
      .encoding = "ros2msg",
      .type_name = std::move(type_name),
      .route_flags = PJ_PARSER_ROUTE_FLAG_OBJECT_V1,
      .object_type = sdk::BuiltinObjectType::kPointCloud,
      .schema_digests = {},
      .provider_id = "provider",
      .claim_id = "object",
      .priority = 0,
      .provenance = ParserClaimProvenance::kFolderDrop,
  };
}

void expectAdmissionRejected(ParserClaim claim, std::string_view diagnostic_fragment) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));

  const auto result = catalog.admitClaims({std::move(claim)}, ParserClaimProvenance::kMarketplace, 17);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find(diagnostic_fragment), std::string::npos) << result.error();
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().level, DiagnosticLevel::kError);
  EXPECT_NE(diagnostics.front().message.find(diagnostic_fragment), std::string::npos);
  EXPECT_TRUE(catalog.claims().empty());
}

TEST(ParserClaimCatalog, AdmissionRejectsEveryBoundedPriorityViolation) {
  for (const int32_t priority : {-1001, 1001}) {
    SCOPED_TRACE(priority);
    auto claim = scalarClaim();
    claim.priority = priority;
    expectAdmissionRejected(std::move(claim), "[-1000,1000]");
  }

  ParserClaimCatalog catalog;
  auto low = scalarClaim("provider", "low");
  low.priority = -1000;
  auto high = scalarClaim("provider", "high");
  high.priority = 1000;
  EXPECT_TRUE(catalog.admitClaims({low, high}, ParserClaimProvenance::kBundled, 1).has_value());
}

TEST(ParserClaimCatalog, AdmissionRejectsWildcardObjectClaims) {
  auto claim = objectClaim("*");
  expectAdmissionRejected(std::move(claim), "wildcard");
}

TEST(ParserClaimCatalog, AdmissionRejectsObjectTypeWithoutObjectRoute) {
  auto claim = scalarClaim();
  claim.object_type = sdk::BuiltinObjectType::kImage;
  expectAdmissionRejected(std::move(claim), "forbidden");
}

TEST(ParserClaimCatalog, AdmissionRejectsObjectRouteWithoutObjectType) {
  auto claim = objectClaim();
  claim.object_type.reset();
  expectAdmissionRejected(std::move(claim), "requires object_type");
}

TEST(ParserClaimCatalog, AdmissionRejectsUnknownEncodingCaseSensitively) {
  for (const std::string_view encoding : {"unknown", "ROS2MSG"}) {
    SCOPED_TRACE(encoding);
    auto claim = scalarClaim();
    claim.encoding = encoding;
    expectAdmissionRejected(std::move(claim), "unknown encoding");
  }
}

TEST(ParserClaimCatalog, AdmissionRejectsUnknownObjectType) {
  auto claim = objectClaim();
  claim.object_type = static_cast<sdk::BuiltinObjectType>(999);
  expectAdmissionRejected(std::move(claim), "unknown object_type");
}

TEST(ParserClaimCatalog, AdmissionRejectsMalformedSchemaDigestAllowLists) {
  for (const std::string& digest : {
           std::string{},
           std::string("sha256:short"),
           "sha256:" + std::string(63, 'a'),
           "sha256:" + std::string(63, 'a') + "g",
           "SHA256:" + std::string(64, 'a'),
       }) {
    SCOPED_TRACE(digest);
    auto claim = scalarClaim();
    claim.schema_digests = {digest};
    expectAdmissionRejected(std::move(claim), "sha256:<64-hex>");
  }
}

TEST(ParserClaimCatalog, AdmissionRejectsDuplicateIdentityTransactionally) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));
  ASSERT_TRUE(catalog.admitClaims({scalarClaim("same", "one")}, ParserClaimProvenance::kFolderDrop, 1).has_value());

  auto duplicate = scalarClaim("same", "one");
  auto otherwise_valid = scalarClaim("same", "two");
  const auto result =
      catalog.admitClaims({std::move(otherwise_valid), std::move(duplicate)}, ParserClaimProvenance::kBundled, 2);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("duplicate"), std::string::npos);
  ASSERT_EQ(catalog.claims().size(), 1U);
  EXPECT_EQ(catalog.claims().front().claim.claim_id, "one");
  ASSERT_EQ(diagnostics.size(), 1U);
}

TEST(ParserClaimCatalog, AdmissionNormalizesNamesAndUsesHostProvenance) {
  ParserClaimCatalog catalog;
  auto claim = objectClaim("sensor_msgs/PointCloud2");
  claim.provenance = ParserClaimProvenance::kFolderDrop;

  ASSERT_TRUE(catalog.admitClaims({claim}, ParserClaimProvenance::kBundled, 42).has_value());
  ASSERT_EQ(catalog.claims().size(), 1U);
  EXPECT_EQ(catalog.claims()[0].claim.type_name, "sensor_msgs/msg/PointCloud2");
  EXPECT_EQ(catalog.claims()[0].claim.provenance, ParserClaimProvenance::kBundled);
  EXPECT_EQ(catalog.claims()[0].provider_generation, 42U);
}

TEST(ParserClaimCatalog, ModuleManifestDecodesValidatedClaimsAndMetadata) {
  const auto manifest = decodeParserModuleManifest(
      R"({
        "module_abi": 1,
        "id": "com.example.radar",
        "name": "Radar Parsers",
        "version": "1.2.3-rc.1+build.7",
        "claims": [
          {"claim_id":"object","encoding":"ros2msg","type_name":"radar_msgs/RadarScan",
           "routes":["scalar","object"],"object_type":"kPointCloud",
           "schema_digests":[
             "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
             "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
             "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"],"priority":17},
          {"claim_id":"fallback","encoding":"protobuf","type_name":".example.RadarScan",
           "routes":["scalar"],"priority":-2}
        ]
      })",
      ParserClaimProvenance::kMarketplace);

  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  EXPECT_EQ(manifest->id, "com.example.radar");
  EXPECT_EQ(manifest->name, "Radar Parsers");
  EXPECT_EQ(manifest->version, "1.2.3-rc.1+build.7");
  ASSERT_EQ(manifest->claims.size(), 2U);
  EXPECT_EQ(manifest->claims[0].type_name, "radar_msgs/msg/RadarScan");
  EXPECT_EQ(manifest->claims[0].route_flags, PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1);
  EXPECT_EQ(manifest->claims[0].object_type, sdk::BuiltinObjectType::kPointCloud);
  EXPECT_EQ(manifest->claims[0].schema_digests.size(), 2U);
  EXPECT_EQ(manifest->claims[0].provenance, ParserClaimProvenance::kMarketplace);
  EXPECT_EQ(manifest->claims[1].type_name, "example.RadarScan");
  EXPECT_FALSE(manifest->claims[1].object_type.has_value());
}

TEST(ParserClaimCatalog, ModuleManifestRejectsMalformedJsonAndAbiVersion) {
  for (const std::string_view manifest : {
           "not json",
           R"([])",
           R"({"module_abi":2,"id":"x","name":"X","version":"1.0.0","claims":[]})",
           R"({"id":"x","name":"X","version":"1.0.0","claims":[]})",
       }) {
    SCOPED_TRACE(manifest);
    const auto parsed = decodeParserModuleManifest(manifest, ParserClaimProvenance::kFolderDrop);
    EXPECT_FALSE(parsed.has_value());
  }
}

TEST(ParserClaimCatalog, MalformedModuleManifestEmitsOneDiagnosticAndAdmitsNothing) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));

  const auto result = catalog.ingestModuleManifest("not json", ParserClaimProvenance::kFolderDrop, 1);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().level, DiagnosticLevel::kError);
  EXPECT_NE(diagnostics.front().message.find("invalid JSON"), std::string::npos);
  EXPECT_TRUE(catalog.claims().empty());
}

TEST(ParserClaimCatalog, ModuleManifestRequiresStableIdentityNameAndThreePartSemver) {
  for (const std::string_view manifest : {
           R"({"module_abi":1,"name":"X","version":"1.0.0","claims":[]})",
           R"({"module_abi":1,"id":"x","version":"1.0.0","claims":[]})",
           R"({"module_abi":1,"id":"x","name":"X","version":"1.0","claims":[]})",
       }) {
    SCOPED_TRACE(manifest);
    const auto parsed = decodeParserModuleManifest(manifest, ParserClaimProvenance::kBundled);
    EXPECT_FALSE(parsed.has_value());
  }
}

TEST(ParserClaimCatalog, ModuleManifestRejectsDuplicateClaimsAsAWhole) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));
  const auto result = catalog.ingestModuleManifest(
      R"({"module_abi":1,"id":"module","name":"Module","version":"1.0.0","claims":[
        {"claim_id":"same","encoding":"protobuf","type_name":"a.Type","routes":["scalar"],"priority":0},
        {"claim_id":"same","encoding":"protobuf","type_name":"b.Type","routes":["scalar"],"priority":0}
      ]})",
      ParserClaimProvenance::kFolderDrop, 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("duplicate"), std::string::npos);
  EXPECT_TRUE(catalog.claims().empty());
  ASSERT_EQ(diagnostics.size(), 1U);
}

TEST(ParserClaimCatalog, ModuleManifestCannotForgeProvenance) {
  const auto result = decodeParserModuleManifest(
      R"({"module_abi":1,"id":"module","name":"Module","version":"1.0.0",
           "provenance":"bundled","claims":[]})",
      ParserClaimProvenance::kFolderDrop);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("provenance"), std::string::npos);
}

TEST(ParserClaimCatalog, ParserPluginSynthesisFreezesWildcardAndHandlerIds) {
  const std::vector<std::string> encodings{"ros2msg", "protobuf"};
  const std::vector<ParserPluginExactClaim> exact{
      {
          .encoding = "ros2msg",
          .type_name = "sensor_msgs/PointCloud2",
          .classification =
              {
                  .route_flags = PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1,
                  .match = PJ_PARSER_ROUTE_MATCH_EXACT_V1,
                  .status = PJ_PARSER_ROUTE_STATUS_CLAIMED_V1,
                  .object_type = static_cast<uint16_t>(sdk::BuiltinObjectType::kPointCloud),
              },
          .schema_digests = {},
      },
      {
          .encoding = "protobuf",
          .type_name = "foxglove.RawImage",
          .classification =
              {
                  .route_flags = 0,
                  .match = PJ_PARSER_ROUTE_MATCH_EXACT_V1,
                  .status = PJ_PARSER_ROUTE_STATUS_DECLINED_V1,
                  .object_type = static_cast<uint16_t>(sdk::BuiltinObjectType::kNone),
              },
          .schema_digests = {},
      },
  };

  const auto claims = synthesizeParserPluginClaims("parser", encodings, exact, ParserClaimProvenance::kBundled);

  ASSERT_TRUE(claims.has_value()) << claims.error();
  ASSERT_EQ(claims->size(), 3U);
  EXPECT_EQ((*claims)[0].claim_id, "wildcard:ros2msg");
  EXPECT_EQ((*claims)[1].claim_id, "wildcard:protobuf");
  EXPECT_EQ((*claims)[2].claim_id, "handler:ros2msg:sensor_msgs/msg/PointCloud2");
  EXPECT_EQ((*claims)[2].type_name, "sensor_msgs/msg/PointCloud2");
}

TEST(ParserClaimCatalog, ParserPluginSynthesisRejectsUnknownEncodingsAndMalformedClassifications) {
  const std::vector<std::string> unknown{"ROS2MSG"};
  EXPECT_FALSE(synthesizeParserPluginClaims("parser", unknown, {}, ParserClaimProvenance::kBundled).has_value());

  const std::vector<std::string> encodings{"protobuf"};
  const std::vector<ParserPluginExactClaim> malformed{{
      .encoding = "protobuf",
      .type_name = "foxglove.RawImage",
      .classification =
          {
              .route_flags = PJ_PARSER_ROUTE_FLAG_SCALAR_V1,
              .match = 1,
              .status = PJ_PARSER_ROUTE_STATUS_CLAIMED_V1,
              .object_type = static_cast<uint16_t>(sdk::BuiltinObjectType::kNone),
          },
      .schema_digests = {},
  }};
  const auto result = synthesizeParserPluginClaims("parser", encodings, malformed, ParserClaimProvenance::kBundled);
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("non-exact"), std::string::npos);
}

TEST(ParserClaimCatalog, CatalogMutationGenerationChangesOnlyWhenStateChanges) {
  ParserClaimCatalog catalog;
  EXPECT_EQ(catalog.generation(), 0U);
  ASSERT_TRUE(catalog.admitClaims({}, ParserClaimProvenance::kBundled, 1).has_value());
  EXPECT_EQ(catalog.generation(), 0U);
  ASSERT_TRUE(catalog.admitClaims({scalarClaim()}, ParserClaimProvenance::kBundled, 1).has_value());
  EXPECT_EQ(catalog.generation(), 1U);
  EXPECT_FALSE(catalog.removeProvider("missing"));
  EXPECT_EQ(catalog.generation(), 1U);
  EXPECT_TRUE(catalog.removeProvider("provider"));
  EXPECT_EQ(catalog.generation(), 2U);
  catalog.clear();
  EXPECT_EQ(catalog.generation(), 2U);
}

}  // namespace
}  // namespace PJ
