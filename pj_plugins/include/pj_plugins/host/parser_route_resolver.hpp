#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_route_resolver.hpp
 * @brief Deterministic host-side parser route selection and probe caching.
 *
 * The resolver owns policy and cached probe decisions only. A caller-provided
 * callback performs provider-specific creation, binding, and classification on
 * the required executor. An optional opaque lease in an accepted decision is
 * retained by the cache and returned with the winner. Scalar and object probe
 * caches are independent partitions so each route retains its own instance.
 */

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"

namespace PJ {

enum class ParserRoute : uint8_t {
  kScalar = 1,
  kObject = 2,
};

struct ParserRoutePins {
  std::optional<std::string> scalar_provider;
  std::optional<std::string> object_provider;

  [[nodiscard]] const std::optional<std::string>& forRoute(ParserRoute route) const noexcept {
    return route == ParserRoute::kScalar ? scalar_provider : object_provider;
  }
};

struct ParserRouteRequest {
  std::string encoding;
  std::string type_name;
  std::string schema_digest;
  ParserRoute route = ParserRoute::kScalar;
};

struct ParserClaimIdentity {
  std::string provider_id;
  std::string claim_id;

  auto operator<=>(const ParserClaimIdentity&) const = default;
};

struct ParserRouteCandidate {
  ParserClaim claim;
  uint64_t provider_generation = 0;
};

enum class ParserProbeOutcome : uint8_t {
  kAccept,
  kDecline,
  kError,
};

struct ParserProbeRequest {
  const ParserClaim& claim;
  uint64_t provider_generation = 0;
  std::string_view config_json;
  std::string_view config_digest;
};

struct ParserProbeDecision {
  ParserProbeOutcome outcome = ParserProbeOutcome::kError;
  std::string diagnostic;
  std::shared_ptr<void> retained_instance;
};

using ParserProbeCallback = std::function<ParserProbeDecision(const ParserProbeRequest&)>;

/// Provider config supplied by the host's config-envelope layer.
struct ParserProviderConfig {
  std::string json;
  std::string digest;
};

using ParserProviderConfigLookup = std::function<ParserProviderConfig(std::string_view provider_id)>;

enum class ParserSelectionTraceKind : uint8_t {
  kCandidate,
  kCacheHit,
  kProbeAccept,
  kProbeDecline,
  kProbeError,
  kAmbiguityTieBreak,
  kSelected,
  kPinnedProviderUnavailable,
  kExhausted,
};

struct ParserSelectionTraceEntry {
  ParserSelectionTraceKind kind = ParserSelectionTraceKind::kCandidate;
  ParserClaimIdentity claim;
  std::string detail;
};

enum class ParserRouteResolutionStatus : uint8_t {
  kSelected,
  kNoCandidates,
  kNoProviderAccepted,
  kPinnedProviderUnavailable,
  kPinnedProviderRejected,
};

struct ParserRouteResolution {
  ParserRouteResolutionStatus status = ParserRouteResolutionStatus::kNoCandidates;
  std::optional<ParserClaimIdentity> winner;
  std::optional<ParserClaim> winning_claim;
  std::shared_ptr<void> retained_instance;
  std::vector<ParserClaimIdentity> ordered_candidates;
  std::vector<ParserSelectionTraceEntry> trace;
};

/// Deterministic, non-thread-safe route resolver.
class ParserRouteResolver {
 public:
  explicit ParserRouteResolver(DiagnosticSink sink = {}, std::string diagnostic_source = "ParserRouteResolver");
  ~ParserRouteResolver();

  ParserRouteResolver(const ParserRouteResolver&) = delete;
  ParserRouteResolver& operator=(const ParserRouteResolver&) = delete;

  void setDiagnosticSink(DiagnosticSink sink);

  /// Produce the policy-ordered candidates without probing them.
  [[nodiscard]] Expected<std::vector<ParserRouteCandidate>> orderedCandidates(
      const ParserRouteRequest& request, const ParserClaimCatalog& catalog, const ParserRoutePins& pins) const;

  /// Probe candidates in policy order and return a machine-readable trace.
  [[nodiscard]] Expected<ParserRouteResolution> resolve(
      const ParserRouteRequest& request, const ParserClaimCatalog& catalog, const ParserRoutePins& pins,
      const ParserProviderConfigLookup& provider_config, const ParserProbeCallback& probe);

  /// Explicit invalidation hooks used by the host mutation paths.
  void invalidateCatalog();
  void invalidatePins();
  void invalidateProviderConfig(std::string_view provider_id);

  [[nodiscard]] size_t probeCacheSize() const noexcept;

 private:
  struct CacheEntry;

  void clearAllCachedState();
  void report(DiagnosticLevel level, std::string_view id, std::string message) const;

  DiagnosticSink sink_;
  std::string diagnostic_source_;
  std::vector<CacheEntry> scalar_probe_cache_;
  std::vector<CacheEntry> object_probe_cache_;
  std::vector<std::string> reported_ambiguities_;
};

}  // namespace PJ
