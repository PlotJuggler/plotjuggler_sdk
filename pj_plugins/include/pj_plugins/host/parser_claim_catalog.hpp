#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_claim_catalog.hpp
 * @brief Host-owned parser claims, admission, and manifest decoding.
 *
 * Claims from parser plugins and functional modules share one validated value
 * model. Artifact metadata supplies identity and coverage; the host supplies
 * provenance and provider generation when it admits a batch. Batch admission
 * is transactional so one invalid or duplicate claim rejects the whole batch.
 */

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/parser_route_claims_protocol.h"

namespace PJ {

/// Host-derived trust tier. Numeric order is the route-selection order.
enum class ParserClaimProvenance : uint8_t {
  kFolderDrop = 0,
  kMarketplace = 1,
  kBundled = 2,
};

/// One admitted parser route claim.
struct ParserClaim {
  std::string encoding;
  std::string type_name;
  uint16_t route_flags = 0;
  std::optional<sdk::BuiltinObjectType> object_type;
  std::set<std::string> schema_digests;
  std::string provider_id;
  std::string claim_id;
  int32_t priority = 0;
  ParserClaimProvenance provenance = ParserClaimProvenance::kFolderDrop;
};

/// Catalog storage for a claim plus the host's immutable provider generation.
struct ParserClaimEntry {
  ParserClaim claim;
  uint64_t provider_generation = 0;
};

/// Parsed module metadata. Claim order remains manifest order.
struct ParserModuleManifest {
  std::string id;
  std::string name;
  std::string version;
  std::vector<ParserClaim> claims;
};

/// One successful route-classification result supplied by a parser-plugin host.
struct ParserPluginExactClaim {
  std::string encoding;
  std::string type_name;
  PJ_route_classification_v1_t classification{};
  std::set<std::string> schema_digests;
};

/// The case-sensitive encoding vocabulary accepted by catalog admission.
[[nodiscard]] std::span<const std::string_view> registeredParserEncodings() noexcept;

/// Return whether encoding is in the SDK-owned case-sensitive registry.
[[nodiscard]] bool isRegisteredParserEncoding(std::string_view encoding) noexcept;

/// Normalize one type name according to its registered encoding.
///
/// ros2msg accepts `pkg/Type` and `pkg/msg/Type`, producing the latter.
/// protobuf accepts an optional leading dot and produces a full dotted name.
/// Other registered encodings preserve a non-empty name verbatim. The wildcard
/// spelling `*` is preserved for every encoding.
[[nodiscard]] Expected<std::string> normalizeParserTypeName(std::string_view encoding, std::string_view type_name);

/// Decode and validate one complete parser-module manifest. Provenance is
/// supplied by the host and is never read from JSON.
[[nodiscard]] Expected<ParserModuleManifest> decodeParserModuleManifest(
    std::string_view manifest_json, ParserClaimProvenance provenance);

/// Synthesize the frozen wildcard and exact parser-plugin claim identities.
/// Declined classifications add no exact claim. Malformed classification
/// records reject the complete synthesized batch.
[[nodiscard]] Expected<std::vector<ParserClaim>> synthesizeParserPluginClaims(
    std::string_view provider_id, std::span<const std::string> manifest_encodings,
    std::span<const ParserPluginExactClaim> exact_claims, ParserClaimProvenance provenance);

/// Non-thread-safe host catalog for validated parser claims.
class ParserClaimCatalog {
 public:
  explicit ParserClaimCatalog(DiagnosticSink sink = {}, std::string diagnostic_source = "ParserClaimCatalog");

  /// Replace the optional diagnostic sink.
  void setDiagnosticSink(DiagnosticSink sink);

  /// Validate and atomically admit a claim batch. The trusted provenance
  /// argument overwrites every input value; artifacts cannot choose their tier.
  [[nodiscard]] Status admitClaims(
      std::vector<ParserClaim> claims, ParserClaimProvenance provenance, uint64_t provider_generation);

  /// Decode and atomically admit a complete module manifest.
  [[nodiscard]] Expected<ParserModuleManifest> ingestModuleManifest(
      std::string_view manifest_json, ParserClaimProvenance provenance, uint64_t provider_generation);

  /// Synthesize and atomically admit all claims for one parser plugin.
  [[nodiscard]] Status admitParserPlugin(
      std::string_view provider_id, std::span<const std::string> manifest_encodings,
      std::span<const ParserPluginExactClaim> exact_claims, ParserClaimProvenance provenance,
      uint64_t provider_generation);

  /// Remove every claim owned by provider_id. Returns true when state changed.
  [[nodiscard]] bool removeProvider(std::string_view provider_id);

  /// Remove every claim. Does not advance generation when already empty.
  void clear();

  [[nodiscard]] const std::vector<ParserClaimEntry>& claims() const noexcept {
    return claims_;
  }

  /// Monotonic catalog mutation generation, advanced once per non-empty batch.
  [[nodiscard]] uint64_t generation() const noexcept {
    return generation_;
  }

 private:
  void report(DiagnosticLevel level, std::string_view id, std::string message) const;

  DiagnosticSink sink_;
  std::string diagnostic_source_;
  std::vector<ParserClaimEntry> claims_;
  uint64_t generation_ = 0;
};

}  // namespace PJ
