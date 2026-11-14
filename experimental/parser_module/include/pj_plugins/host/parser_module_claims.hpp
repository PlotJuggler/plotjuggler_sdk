#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_claims.hpp
 * @brief Parser-module manifest decoding into ParserClaimCatalog claims.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"

namespace PJ {

/// Manifest bounds, checked before any claim is built: every claim copies the
/// module id, so unbounded metadata would expand in host memory before any
/// session budget sees it. A real manifest is a few kilobytes.
/// Manifest JSON bytes: about 1 KiB for each of the maximum number of claims.
inline constexpr size_t kMaximumParserModuleManifestBytes = size_t{4} * 1024U * 1024U;
/// Claims per manifest: a module with more could never fit the default session budget.
inline constexpr size_t kMaximumParserModuleManifestClaims = ParserModuleSessionBudgetLimits::kDefaultMaximumClaims;
/// Each of id, name, version, claim_id, encoding, type_name and schema digest
/// (reverse-DNS ids, type names and `sha256:<64-hex>` digests are under 256).
inline constexpr size_t kMaximumParserModuleManifestStringBytes = 1024;
/// schema_digests per claim: one per supported schema revision of the type.
inline constexpr size_t kMaximumParserModuleClaimSchemaDigests = 64;

/// Parsed module metadata. Claim order remains manifest order.
struct ParserModuleManifest {
  std::string id;
  std::string name;
  std::string version;
  std::vector<ParserClaim> claims;
};

/// Decode and validate one complete parser-module manifest. Provenance is
/// supplied by the host and is never read from JSON.
[[nodiscard]] Expected<ParserModuleManifest> decodeParserModuleManifest(
    std::string_view manifest_json, ParserClaimProvenance provenance);

/// Decode a complete module manifest and atomically admit its claims into
/// `catalog`. A manifest that fails to decode is reported to `sink` (pass the
/// catalog's own sink); admission failures are reported by the catalog.
[[nodiscard]] Expected<ParserModuleManifest> ingestParserModuleManifest(
    ParserClaimCatalog& catalog, std::string_view manifest_json, ParserClaimProvenance provenance,
    uint64_t provider_generation, const DiagnosticSink& sink = {});

}  // namespace PJ
