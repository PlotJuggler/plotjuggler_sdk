// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_route_resolver.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

#include "pj_base/parser_route_claims_protocol.h"

namespace PJ {
namespace {

[[nodiscard]] uint16_t routeFlag(ParserRoute route) noexcept {
  return route == ParserRoute::kScalar ? PJ_PARSER_ROUTE_FLAG_SCALAR_V1 : PJ_PARSER_ROUTE_FLAG_OBJECT_V1;
}

[[nodiscard]] ParserClaimIdentity identityOf(const ParserClaim& claim) {
  return {claim.provider_id, claim.claim_id};
}

[[nodiscard]] bool hasSamePolicyRank(const ParserClaim& lhs, const ParserClaim& rhs) noexcept {
  return (lhs.type_name == "*") == (rhs.type_name == "*") && lhs.provenance == rhs.provenance &&
         lhs.priority == rhs.priority;
}

[[nodiscard]] std::string outcomeDetail(const ParserProbeDecision& decision, std::string_view fallback) {
  return decision.diagnostic.empty() ? std::string(fallback) : decision.diagnostic;
}

}  // namespace

struct ParserRouteResolver::CacheEntry {
  std::string provider_id;
  std::string claim_id;
  uint64_t provider_generation = 0;
  std::string encoding;
  std::string type_name;
  std::string schema_digest;
  std::string config_digest;
  ParserProbeDecision decision;
};

ParserRouteResolver::ParserRouteResolver(DiagnosticSink sink, std::string diagnostic_source)
    : sink_(std::move(sink)), diagnostic_source_(std::move(diagnostic_source)) {}

ParserRouteResolver::~ParserRouteResolver() = default;

void ParserRouteResolver::setDiagnosticSink(DiagnosticSink sink) {
  sink_ = std::move(sink);
}

Expected<std::vector<ParserRouteCandidate>> ParserRouteResolver::orderedCandidates(
    const ParserRouteRequest& request, const ParserClaimCatalog& catalog, const ParserRoutePins& pins) const {
  if (!isRegisteredParserEncoding(request.encoding)) {
    return unexpected("route request uses unknown encoding: " + request.encoding);
  }
  auto normalized_type = normalizeParserTypeName(request.encoding, request.type_name);
  if (!normalized_type) {
    return unexpected(normalized_type.error());
  }

  const auto& pin = pins.forRoute(request.route);
  std::vector<ParserRouteCandidate> candidates;
  for (const auto& entry : catalog.claims()) {
    const ParserClaim& claim = entry.claim;
    if (claim.encoding != request.encoding || (claim.route_flags & routeFlag(request.route)) == 0) {
      continue;
    }
    if (claim.type_name != "*" && claim.type_name != *normalized_type) {
      continue;
    }
    if (!claim.schema_digests.empty() && !claim.schema_digests.contains(request.schema_digest)) {
      continue;
    }
    if (pin.has_value() && claim.provider_id != *pin) {
      continue;
    }
    candidates.push_back({.claim = claim, .provider_generation = entry.provider_generation});
  }

  std::sort(candidates.begin(), candidates.end(), [](const ParserRouteCandidate& lhs, const ParserRouteCandidate& rhs) {
    const bool lhs_exact = lhs.claim.type_name != "*";
    const bool rhs_exact = rhs.claim.type_name != "*";
    if (lhs_exact != rhs_exact) {
      return lhs_exact;
    }
    if (lhs.claim.provenance != rhs.claim.provenance) {
      return lhs.claim.provenance > rhs.claim.provenance;
    }
    if (lhs.claim.priority != rhs.claim.priority) {
      return lhs.claim.priority > rhs.claim.priority;
    }
    return std::tie(lhs.claim.provider_id, lhs.claim.claim_id) < std::tie(rhs.claim.provider_id, rhs.claim.claim_id);
  });
  return candidates;
}

Expected<ParserRouteResolution> ParserRouteResolver::resolve(
    const ParserRouteRequest& request, const ParserClaimCatalog& catalog, const ParserRoutePins& pins,
    const ParserProviderConfigLookup& provider_config, const ParserProbeCallback& probe) {
  if (!probe) {
    return unexpected("parser probe callback is empty");
  }
  auto normalized_type = normalizeParserTypeName(request.encoding, request.type_name);
  if (!normalized_type) {
    return unexpected(normalized_type.error());
  }
  auto candidates = orderedCandidates(request, catalog, pins);
  if (!candidates) {
    return unexpected(candidates.error());
  }

  ParserRouteResolution resolution;
  resolution.ordered_candidates.reserve(candidates->size());
  resolution.trace.reserve(candidates->size() * 3 + 2);
  for (const auto& candidate : *candidates) {
    const auto identity = identityOf(candidate.claim);
    resolution.ordered_candidates.push_back(identity);
    resolution.trace.push_back({
        .kind = ParserSelectionTraceKind::kCandidate,
        .claim = identity,
        .detail = candidate.claim.type_name == "*" ? "wildcard candidate" : "exact candidate",
    });
  }

  const auto& pin = pins.forRoute(request.route);
  if (candidates->empty()) {
    if (pin.has_value()) {
      resolution.status = ParserRouteResolutionStatus::kPinnedProviderUnavailable;
      resolution.trace.push_back({
          .kind = ParserSelectionTraceKind::kPinnedProviderUnavailable,
          .claim = {.provider_id = *pin, .claim_id = {}},
          .detail = "pinned provider has no matching admitted claim",
      });
      report(
          DiagnosticLevel::kError, *pin,
          "pinned parser provider is unavailable for " + request.encoding + ":" + *normalized_type);
    } else {
      resolution.status = ParserRouteResolutionStatus::kNoCandidates;
      resolution.trace.push_back({
          .kind = ParserSelectionTraceKind::kExhausted,
          .claim = {},
          .detail = "no matching admitted claims",
      });
    }
    return resolution;
  }

  std::vector<std::string> declined_probes;
  const auto report_declines = [&]() {
    if (declined_probes.empty()) {
      return;
    }
    std::string summary = "parser probe declined for " + std::to_string(declined_probes.size()) + " candidate(s): ";
    for (size_t index = 0; index < declined_probes.size(); ++index) {
      if (index != 0) {
        summary += "; ";
      }
      summary += declined_probes[index];
    }
    report(DiagnosticLevel::kInfo, {}, std::move(summary));
  };

  for (const auto& candidate : *candidates) {
    ParserProviderConfig config;
    if (provider_config) {
      try {
        config = provider_config(candidate.claim.provider_id);
      } catch (const std::exception& error) {
        return unexpected(
            "provider config lookup failed for provider " + candidate.claim.provider_id + ": " + error.what());
      } catch (...) {
        return unexpected("provider config lookup failed for provider " + candidate.claim.provider_id);
      }
    }

    auto& route_cache = request.route == ParserRoute::kScalar ? scalar_probe_cache_ : object_probe_cache_;
    auto cached = std::find_if(route_cache.begin(), route_cache.end(), [&](const CacheEntry& entry) {
      return entry.provider_id == candidate.claim.provider_id && entry.claim_id == candidate.claim.claim_id &&
             entry.provider_generation == candidate.provider_generation && entry.encoding == request.encoding &&
             entry.type_name == *normalized_type && entry.schema_digest == request.schema_digest &&
             entry.config_digest == config.digest;
    });

    ParserProbeDecision decision;
    const bool cache_hit = cached != route_cache.end();
    if (cache_hit) {
      decision = cached->decision;
      resolution.trace.push_back({
          .kind = ParserSelectionTraceKind::kCacheHit,
          .claim = identityOf(candidate.claim),
          .detail = "probe result reused",
      });
    } else {
      try {
        decision = probe(
            ParserProbeRequest{
                .claim = candidate.claim,
                .provider_generation = candidate.provider_generation,
                .config_json = config.json,
                .config_digest = config.digest,
            });
      } catch (const std::exception& error) {
        decision = {
            .outcome = ParserProbeOutcome::kError,
            .diagnostic = std::string("probe callback threw: ") + error.what(),
            .retained_instance = {},
        };
      } catch (...) {
        decision = {
            .outcome = ParserProbeOutcome::kError,
            .diagnostic = "probe callback threw an unknown exception",
            .retained_instance = {},
        };
      }
      if (decision.outcome != ParserProbeOutcome::kAccept && decision.outcome != ParserProbeOutcome::kDecline &&
          decision.outcome != ParserProbeOutcome::kError) {
        decision = {
            .outcome = ParserProbeOutcome::kError,
            .diagnostic = "probe callback returned an invalid outcome",
            .retained_instance = {},
        };
      } else if (decision.outcome != ParserProbeOutcome::kAccept) {
        decision.retained_instance.reset();
      }
      route_cache.push_back(
          CacheEntry{
              .provider_id = candidate.claim.provider_id,
              .claim_id = candidate.claim.claim_id,
              .provider_generation = candidate.provider_generation,
              .encoding = request.encoding,
              .type_name = *normalized_type,
              .schema_digest = request.schema_digest,
              .config_digest = config.digest,
              .decision = decision,
          });
    }

    if (decision.outcome == ParserProbeOutcome::kDecline) {
      const std::string detail = outcomeDetail(decision, "provider declined during probe");
      resolution.trace.push_back({
          .kind = ParserSelectionTraceKind::kProbeDecline,
          .claim = identityOf(candidate.claim),
          .detail = detail,
      });
      if (!cache_hit) {
        declined_probes.push_back(candidate.claim.provider_id + "/" + candidate.claim.claim_id + ": " + detail);
      }
      continue;
    }
    if (decision.outcome == ParserProbeOutcome::kError) {
      const std::string detail = outcomeDetail(decision, "provider probe failed");
      resolution.trace.push_back({
          .kind = ParserSelectionTraceKind::kProbeError,
          .claim = identityOf(candidate.claim),
          .detail = detail,
      });
      if (!cache_hit) {
        report(DiagnosticLevel::kError, candidate.claim.provider_id, "parser probe error: " + detail);
      }
      continue;
    }

    resolution.trace.push_back({
        .kind = ParserSelectionTraceKind::kProbeAccept,
        .claim = identityOf(candidate.claim),
        .detail = "provider accepted during probe",
    });
    resolution.status = ParserRouteResolutionStatus::kSelected;
    resolution.winner = identityOf(candidate.claim);
    resolution.winning_claim = candidate.claim;
    resolution.retained_instance = std::move(decision.retained_instance);
    report_declines();

    const auto tied = std::find_if(candidates->begin(), candidates->end(), [&](const ParserRouteCandidate& other) {
      return identityOf(other.claim) > *resolution.winner && hasSamePolicyRank(candidate.claim, other.claim);
    });
    if (tied != candidates->end()) {
      std::string ambiguity_key = request.encoding + "\n" + *normalized_type + "\n" + request.schema_digest + "\n" +
                                  std::to_string(static_cast<uint8_t>(request.route)) + "\n" +
                                  resolution.winner->provider_id + "\n" + resolution.winner->claim_id;
      if (std::find(reported_ambiguities_.begin(), reported_ambiguities_.end(), ambiguity_key) ==
          reported_ambiguities_.end()) {
        reported_ambiguities_.push_back(std::move(ambiguity_key));
        const std::string detail = "stable claim identity selected " + resolution.winner->provider_id + "/" +
                                   resolution.winner->claim_id + " over an equal-ranked candidate";
        resolution.trace.push_back({
            .kind = ParserSelectionTraceKind::kAmbiguityTieBreak,
            .claim = *resolution.winner,
            .detail = detail,
        });
        report(DiagnosticLevel::kWarning, resolution.winner->provider_id, "ambiguous parser claims: " + detail);
      }
    }

    resolution.trace.push_back({
        .kind = ParserSelectionTraceKind::kSelected,
        .claim = *resolution.winner,
        .detail = "route provider selected",
    });
    return resolution;
  }

  resolution.status = pin.has_value() ? ParserRouteResolutionStatus::kPinnedProviderRejected
                                      : ParserRouteResolutionStatus::kNoProviderAccepted;
  resolution.trace.push_back({
      .kind = ParserSelectionTraceKind::kExhausted,
      .claim = {.provider_id = pin.value_or(""), .claim_id = {}},
      .detail = pin.has_value() ? "pinned provider declined or failed; fallback is disabled"
                                : "all matching providers declined or failed",
  });
  report_declines();
  if (pin.has_value()) {
    report(DiagnosticLevel::kError, *pin, "pinned parser provider declined or failed; route remains unbound");
  }
  return resolution;
}

void ParserRouteResolver::invalidateCatalog() {
  clearAllCachedState();
}

void ParserRouteResolver::invalidatePins() {
  clearAllCachedState();
}

void ParserRouteResolver::invalidateProviderConfig(std::string_view provider_id) {
  const auto belongs_to_provider = [&](const CacheEntry& entry) { return entry.provider_id == provider_id; };
  std::erase_if(scalar_probe_cache_, belongs_to_provider);
  std::erase_if(object_probe_cache_, belongs_to_provider);
}

size_t ParserRouteResolver::probeCacheSize() const noexcept {
  return scalar_probe_cache_.size() + object_probe_cache_.size();
}

void ParserRouteResolver::clearAllCachedState() {
  scalar_probe_cache_.clear();
  object_probe_cache_.clear();
  reported_ambiguities_.clear();
}

void ParserRouteResolver::report(DiagnosticLevel level, std::string_view id, std::string message) const {
  if (!sink_) {
    return;
  }
  sink_(
      Diagnostic{
          .level = level,
          .source = diagnostic_source_,
          .id = std::string(id),
          .message = std::move(message),
          .timestamp = std::chrono::system_clock::now(),
      });
}

}  // namespace PJ
