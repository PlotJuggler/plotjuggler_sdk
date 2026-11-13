// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_claims.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "pj_base/parser_route_claims_protocol.h"

namespace PJ {
namespace {

DiagnosticSink recordInto(std::vector<Diagnostic>& diagnostics) {
  return [&diagnostics](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); };
}

TEST(ParserModuleClaims, ModuleManifestDecodesValidatedClaimsAndMetadata) {
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

TEST(ParserModuleClaims, ModuleManifestRejectsMalformedJsonAndAbiVersion) {
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

TEST(ParserModuleClaims, MalformedModuleManifestEmitsOneDiagnosticAndAdmitsNothing) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));

  const auto result =
      ingestParserModuleManifest(catalog, "not json", ParserClaimProvenance::kFolderDrop, 1, recordInto(diagnostics));

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().level, DiagnosticLevel::kError);
  EXPECT_NE(diagnostics.front().message.find("invalid JSON"), std::string::npos);
  EXPECT_TRUE(catalog.claims().empty());
}

TEST(ParserModuleClaims, ModuleManifestRequiresStableIdentityNameAndThreePartSemver) {
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

TEST(ParserModuleClaims, ModuleManifestRejectsDuplicateClaimsAsAWhole) {
  std::vector<Diagnostic> diagnostics;
  ParserClaimCatalog catalog(recordInto(diagnostics));
  const auto result = ingestParserModuleManifest(
      catalog,
      R"({"module_abi":1,"id":"module","name":"Module","version":"1.0.0","claims":[
        {"claim_id":"same","encoding":"protobuf","type_name":"a.Type","routes":["scalar"],"priority":0},
        {"claim_id":"same","encoding":"protobuf","type_name":"b.Type","routes":["scalar"],"priority":0}
      ]})",
      ParserClaimProvenance::kFolderDrop, 1, recordInto(diagnostics));

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("duplicate"), std::string::npos);
  EXPECT_TRUE(catalog.claims().empty());
  ASSERT_EQ(diagnostics.size(), 1U);
}

TEST(ParserModuleClaims, ModuleManifestCannotForgeProvenance) {
  const auto result = decodeParserModuleManifest(
      R"({"module_abi":1,"id":"module","name":"Module","version":"1.0.0",
           "provenance":"bundled","claims":[]})",
      ParserClaimProvenance::kFolderDrop);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("provenance"), std::string::npos);
}

}  // namespace
}  // namespace PJ
