// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_route_resolver.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_route_claims_protocol.h"

namespace PJ {
namespace {

DiagnosticSink recordInto(std::vector<Diagnostic>& diagnostics) {
  return [&diagnostics](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); };
}

ParserClaim claim(
    std::string provider, std::string claim_id, std::string type_name = "foxglove.PointCloud",
    uint16_t routes = PJ_PARSER_ROUTE_FLAG_SCALAR_V1, int32_t priority = 0,
    ParserClaimProvenance provenance = ParserClaimProvenance::kFolderDrop) {
  ParserClaim result{
      .encoding = "protobuf",
      .type_name = std::move(type_name),
      .route_flags = routes,
      .object_type = std::nullopt,
      .schema_digests = {},
      .provider_id = std::move(provider),
      .claim_id = std::move(claim_id),
      .priority = priority,
      .provenance = provenance,
  };
  if ((routes & PJ_PARSER_ROUTE_FLAG_OBJECT_V1) != 0) {
    result.object_type = sdk::BuiltinObjectType::kPointCloud;
  }
  return result;
}

ParserProbeDecision probeDecision(
    ParserProbeOutcome outcome, std::string diagnostic = {}, std::shared_ptr<void> retained_instance = {}) {
  return {
      .outcome = outcome,
      .diagnostic = std::move(diagnostic),
      .retained_instance = std::move(retained_instance),
  };
}

std::string schemaDigest(char digit) {
  return "sha256:" + std::string(64, digit);
}

void admit(ParserClaimCatalog& catalog, std::vector<ParserClaim> claims, uint64_t generation = 1) {
  const ParserClaimProvenance provenance = claims.front().provenance;
  const auto result = catalog.admitClaims(std::move(claims), provenance, generation);
  ASSERT_TRUE(result.has_value()) << result.error();
}

ParserRouteRequest scalarRequest(std::string schema_digest = schemaDigest('a')) {
  return {
      .encoding = "protobuf",
      .type_name = ".foxglove.PointCloud",
      .schema_digest = std::move(schema_digest),
      .route = ParserRoute::kScalar,
  };
}

size_t traceCount(const ParserRouteResolution& result, ParserSelectionTraceKind kind) {
  return static_cast<size_t>(
      std::count_if(result.trace.begin(), result.trace.end(), [&](const auto& entry) { return entry.kind == kind; }));
}

TEST(ParserRouteResolver, ExactClaimsOutrankWildcardRegardlessOfTierAndPriority) {
  ParserClaimCatalog catalog;
  admit(
      catalog,
      {
          claim("wildcard", "wild", "*", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, 1000, ParserClaimProvenance::kBundled),
      });
  admit(
      catalog, {
                   claim(
                       "exact", "exact", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, -1000,
                       ParserClaimProvenance::kFolderDrop),
               });

  ParserRouteResolver resolver;
  const auto candidates = resolver.orderedCandidates(scalarRequest(), catalog, {});

  ASSERT_TRUE(candidates.has_value()) << candidates.error();
  ASSERT_EQ(candidates->size(), 2U);
  EXPECT_EQ((*candidates)[0].claim.provider_id, "exact");
  EXPECT_EQ((*candidates)[1].claim.provider_id, "wildcard");
}

TEST(ParserRouteResolver, ProvenanceBeatsPriorityAndPriorityBreaksTiesWithinTier) {
  ParserClaimCatalog catalog;
  admit(
      catalog, {claim(
                   "folder-high", "c", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, 1000,
                   ParserClaimProvenance::kFolderDrop)});
  admit(
      catalog, {claim(
                   "market-high", "b", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, 1000,
                   ParserClaimProvenance::kMarketplace)});
  admit(
      catalog, {claim(
                   "bundled-low", "a", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, -1000,
                   ParserClaimProvenance::kBundled)});
  admit(
      catalog, {claim(
                   "market-low", "d", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1, -5,
                   ParserClaimProvenance::kMarketplace)});

  ParserRouteResolver resolver;
  const auto candidates = resolver.orderedCandidates(scalarRequest(), catalog, {});

  ASSERT_TRUE(candidates.has_value()) << candidates.error();
  ASSERT_EQ(candidates->size(), 4U);
  EXPECT_EQ((*candidates)[0].claim.provider_id, "bundled-low");
  EXPECT_EQ((*candidates)[1].claim.provider_id, "market-high");
  EXPECT_EQ((*candidates)[2].claim.provider_id, "market-low");
  EXPECT_EQ((*candidates)[3].claim.provider_id, "folder-high");
}

TEST(ParserRouteResolver, StableIdentityBreaksEqualPolicyRanksLexicographically) {
  ParserClaimCatalog catalog;
  admit(
      catalog, {
                   claim("provider-b", "claim-a"),
                   claim("provider-a", "claim-z"),
                   claim("provider-a", "claim-a"),
               });

  ParserRouteResolver resolver;
  const auto candidates = resolver.orderedCandidates(scalarRequest(), catalog, {});

  ASSERT_TRUE(candidates.has_value()) << candidates.error();
  ASSERT_EQ(candidates->size(), 3U);
  EXPECT_EQ(candidates->at(0).claim.claim_id, "claim-a");
  EXPECT_EQ(candidates->at(1).claim.claim_id, "claim-z");
  EXPECT_EQ(candidates->at(2).claim.provider_id, "provider-b");
}

TEST(ParserRouteResolver, SchemaDigestAllowListsFilterCandidates) {
  ParserClaimCatalog catalog;
  auto supported = claim("supported", "supported");
  supported.schema_digests = {schemaDigest('a')};
  auto wrong = claim("wrong", "wrong");
  wrong.schema_digests = {schemaDigest('b')};
  admit(catalog, {supported, wrong});

  ParserRouteResolver resolver;
  const auto candidates = resolver.orderedCandidates(scalarRequest(), catalog, {});

  ASSERT_TRUE(candidates.has_value()) << candidates.error();
  ASSERT_EQ(candidates->size(), 1U);
  EXPECT_EQ(candidates->front().claim.provider_id, "supported");
}

TEST(ParserRouteResolver, DeclineAndErrorAdvanceAndRemainDistinctInTraceAndDiagnostics) {
  std::vector<Diagnostic> diagnostics;
  ParserRouteResolver resolver(recordInto(diagnostics));
  ParserClaimCatalog catalog;
  admit(
      catalog, {
                   claim("a-decline", "claim"),
                   claim("b-decline", "claim"),
                   claim("c-error", "claim"),
                   claim("d-accept", "claim"),
               });
  std::vector<std::string> probes;

  const auto result = resolver.resolve(
      scalarRequest(), catalog, {},
      [](std::string_view) { return ParserProviderConfig{.json = R"({"enabled":true})", .digest = "config"}; },
      [&](const ParserProbeRequest& request) {
        EXPECT_EQ(request.config_json, R"({"enabled":true})");
        probes.push_back(request.claim.provider_id);
        if (request.claim.provider_id == "a-decline" || request.claim.provider_id == "b-decline") {
          return probeDecision(ParserProbeOutcome::kDecline, "unsupported schema");
        }
        if (request.claim.provider_id == "c-error") {
          return probeDecision(ParserProbeOutcome::kError, "classification failed");
        }
        return probeDecision(ParserProbeOutcome::kAccept, {}, std::make_shared<int>(7));
      });

  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->status, ParserRouteResolutionStatus::kSelected);
  ASSERT_TRUE(result->winner.has_value());
  EXPECT_EQ(result->winner->provider_id, "d-accept");
  EXPECT_EQ(probes, (std::vector<std::string>{"a-decline", "b-decline", "c-error", "d-accept"}));
  EXPECT_EQ(traceCount(*result, ParserSelectionTraceKind::kProbeDecline), 2U);
  EXPECT_EQ(traceCount(*result, ParserSelectionTraceKind::kProbeError), 1U);
  EXPECT_EQ(traceCount(*result, ParserSelectionTraceKind::kProbeAccept), 1U);
  EXPECT_NE(result->retained_instance, nullptr);

  ASSERT_EQ(diagnostics.size(), 2U);
  const auto decline_diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& entry) {
    return entry.message.find("declined") != std::string::npos;
  });
  const auto error_diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& entry) {
    return entry.message.find("error") != std::string::npos;
  });
  ASSERT_NE(decline_diagnostic, diagnostics.end());
  ASSERT_NE(error_diagnostic, diagnostics.end());
  EXPECT_EQ(decline_diagnostic->level, DiagnosticLevel::kInfo);
  EXPECT_NE(decline_diagnostic->message.find("2 candidate"), std::string::npos);
  EXPECT_EQ(error_diagnostic->level, DiagnosticLevel::kError);
}

TEST(ParserRouteResolver, ScalarPinFailsClosedWithoutDarkeningUnpinnedObjectRoute) {
  ParserClaimCatalog catalog;
  admit(
      catalog,
      {
          claim(
              "pinned", "both", "foxglove.PointCloud", PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1),
          claim(
              "fallback", "both", "foxglove.PointCloud",
              PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1),
      });
  ParserRouteResolver resolver;
  const ParserRoutePins pins{.scalar_provider = "pinned", .object_provider = std::nullopt};
  std::vector<std::string> probes;
  auto probe = [&](const ParserProbeRequest& request) {
    probes.push_back(request.claim.provider_id);
    return probeDecision(
        request.claim.provider_id == "pinned" ? ParserProbeOutcome::kDecline : ParserProbeOutcome::kAccept);
  };

  const auto scalar = resolver.resolve(scalarRequest(), catalog, pins, {}, probe);
  ASSERT_TRUE(scalar.has_value()) << scalar.error();
  EXPECT_EQ(scalar->status, ParserRouteResolutionStatus::kPinnedProviderRejected);
  EXPECT_FALSE(scalar->winner.has_value());
  EXPECT_EQ(probes, (std::vector<std::string>{"pinned"}));

  auto object_request = scalarRequest();
  object_request.route = ParserRoute::kObject;
  const auto object = resolver.resolve(object_request, catalog, pins, {}, probe);
  ASSERT_TRUE(object.has_value()) << object.error();
  EXPECT_EQ(object->status, ParserRouteResolutionStatus::kSelected);
  ASSERT_TRUE(object->winner.has_value());
  EXPECT_EQ(object->winner->provider_id, "fallback");
}

TEST(ParserRouteResolver, ObjectPinFailureDoesNotAffectScalarRoute) {
  ParserClaimCatalog catalog;
  admit(catalog, {claim("available", "scalar")});
  ParserRouteResolver resolver;
  const ParserRoutePins pins{.scalar_provider = std::nullopt, .object_provider = "missing"};
  auto accept = [](const ParserProbeRequest&) { return probeDecision(ParserProbeOutcome::kAccept); };

  auto object_request = scalarRequest();
  object_request.route = ParserRoute::kObject;
  const auto object = resolver.resolve(object_request, catalog, pins, {}, accept);
  ASSERT_TRUE(object.has_value()) << object.error();
  EXPECT_EQ(object->status, ParserRouteResolutionStatus::kPinnedProviderUnavailable);

  const auto scalar = resolver.resolve(scalarRequest(), catalog, pins, {}, accept);
  ASSERT_TRUE(scalar.has_value()) << scalar.error();
  EXPECT_EQ(scalar->status, ParserRouteResolutionStatus::kSelected);
  ASSERT_TRUE(scalar->winner.has_value());
  EXPECT_EQ(scalar->winner->provider_id, "available");
}

TEST(ParserRouteResolver, TieBreakDiagnosticIsEmittedOnlyOnce) {
  std::vector<Diagnostic> diagnostics;
  ParserRouteResolver resolver(recordInto(diagnostics));
  ParserClaimCatalog catalog;
  admit(catalog, {claim("b-provider", "claim"), claim("a-provider", "claim")});
  size_t calls = 0;
  auto accept = [&](const ParserProbeRequest&) {
    ++calls;
    return probeDecision(ParserProbeOutcome::kAccept);
  };

  const auto first = resolver.resolve(scalarRequest(), catalog, {}, {}, accept);
  const auto second = resolver.resolve(scalarRequest(), catalog, {}, {}, accept);

  ASSERT_TRUE(first.has_value()) << first.error();
  ASSERT_TRUE(second.has_value()) << second.error();
  ASSERT_TRUE(first->winner.has_value());
  EXPECT_EQ(first->winner->provider_id, "a-provider");
  EXPECT_EQ(traceCount(*first, ParserSelectionTraceKind::kAmbiguityTieBreak), 1U);
  EXPECT_EQ(traceCount(*second, ParserSelectionTraceKind::kAmbiguityTieBreak), 0U);
  EXPECT_EQ(traceCount(*second, ParserSelectionTraceKind::kCacheHit), 1U);
  EXPECT_EQ(calls, 1U);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(diagnostics.front().message.find("ambiguous"), std::string::npos);
}

TEST(ParserRouteResolver, ProbeCacheKeysAndExplicitInvalidationsAreObserved) {
  ParserClaimCatalog catalog;
  admit(catalog, {claim("provider", "claim")}, 10);
  ParserRouteResolver resolver;
  std::map<std::string, std::string> configs{{"provider", "config-a"}};
  size_t calls = 0;
  auto lookup = [&](std::string_view provider) {
    const auto digest = configs.at(std::string(provider));
    return ParserProviderConfig{.json = "{}", .digest = digest};
  };
  auto accept = [&](const ParserProbeRequest&) {
    ++calls;
    return probeDecision(ParserProbeOutcome::kAccept, {}, std::make_shared<size_t>(calls));
  };

  ASSERT_TRUE(resolver.resolve(scalarRequest(), catalog, {}, lookup, accept).has_value());
  auto cached = resolver.resolve(scalarRequest(), catalog, {}, lookup, accept);
  ASSERT_TRUE(cached.has_value()) << cached.error();
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(traceCount(*cached, ParserSelectionTraceKind::kCacheHit), 1U);

  ASSERT_TRUE(resolver.resolve(scalarRequest(schemaDigest('b')), catalog, {}, lookup, accept).has_value());
  EXPECT_EQ(calls, 2U);

  configs["provider"] = "config-b";
  ASSERT_TRUE(resolver.resolve(scalarRequest(), catalog, {}, lookup, accept).has_value());
  EXPECT_EQ(calls, 3U);
  resolver.invalidateProviderConfig("provider");
  ASSERT_TRUE(resolver.resolve(scalarRequest(), catalog, {}, lookup, accept).has_value());
  EXPECT_EQ(calls, 4U);

  resolver.invalidatePins();
  ASSERT_TRUE(resolver.resolve(scalarRequest(), catalog, {}, lookup, accept).has_value());
  EXPECT_EQ(calls, 5U);
  resolver.invalidateCatalog();
  ASSERT_TRUE(resolver.resolve(scalarRequest(), catalog, {}, lookup, accept).has_value());
  EXPECT_EQ(calls, 6U);
}

TEST(ParserRouteResolver, ProviderGenerationParticipatesInProbeCacheKey) {
  ParserClaimCatalog first_catalog;
  ParserClaimCatalog second_catalog;
  admit(first_catalog, {claim("provider", "claim")}, 1);
  admit(second_catalog, {claim("provider", "claim")}, 2);
  ParserRouteResolver resolver;
  size_t calls = 0;
  auto accept = [&](const ParserProbeRequest&) {
    ++calls;
    return probeDecision(ParserProbeOutcome::kAccept);
  };

  ASSERT_TRUE(resolver.resolve(scalarRequest(), first_catalog, {}, {}, accept).has_value());
  ASSERT_TRUE(resolver.resolve(scalarRequest(), second_catalog, {}, {}, accept).has_value());
  EXPECT_EQ(calls, 2U);
  EXPECT_EQ(resolver.probeCacheSize(), 2U);
}

TEST(ParserRouteResolver, SameProviderExactDeclineDoesNotPoisonWildcardProbeCache) {
  ParserClaimCatalog catalog;
  admit(
      catalog, {
                   claim("provider", "exact", "foxglove.PointCloud"),
                   claim("provider", "wildcard", "*"),
               });
  ParserRouteResolver resolver;
  std::vector<std::string> probed;
  const auto result = resolver.resolve(scalarRequest(), catalog, {}, {}, [&](const ParserProbeRequest& request) {
    probed.push_back(request.claim.claim_id);
    return probeDecision(
        request.claim.claim_id == "exact" ? ParserProbeOutcome::kDecline : ParserProbeOutcome::kAccept);
  });

  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->status, ParserRouteResolutionStatus::kSelected);
  ASSERT_TRUE(result->winner.has_value());
  EXPECT_EQ(result->winner->claim_id, "wildcard");
  EXPECT_EQ(probed, (std::vector<std::string>{"exact", "wildcard"}));
  EXPECT_EQ(traceCount(*result, ParserSelectionTraceKind::kCacheHit), 0U);
}

}  // namespace
}  // namespace PJ
