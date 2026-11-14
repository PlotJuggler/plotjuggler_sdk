#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_claims.hpp
 * @brief Parser-module manifest decoding into ParserClaimCatalog claims.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_base/expected.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"

namespace PJ {

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
