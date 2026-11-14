// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_claims.hpp"

#include <chrono>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <utility>

#include "pj_base/parser_module_abi.h"
#include "pj_base/parser_route_claims_protocol.h"
#include "pj_base/sdk/semver.hpp"

namespace PJ {
namespace {

[[nodiscard]] Expected<std::string> requiredString(
    const nlohmann::json& object, std::string_view key, std::string_view context) {
  const auto it = object.find(std::string(key));
  if (it == object.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
    return unexpected(std::string(context) + " missing required non-empty string key: " + std::string(key));
  }
  if (it->get_ref<const std::string&>().size() > kMaximumParserModuleManifestStringBytes) {
    return unexpected(
        std::string(context) + " " + std::string(key) + " exceeds " +
        std::to_string(kMaximumParserModuleManifestStringBytes) + " bytes");
  }
  return it->get<std::string>();
}

[[nodiscard]] Expected<int32_t> requiredPriority(const nlohmann::json& object, size_t claim_index) {
  const auto it = object.find("priority");
  if (it == object.end() || !it->is_number_integer()) {
    return unexpected("parser module manifest claim " + std::to_string(claim_index) + " missing integer priority");
  }

  if (it->is_number_unsigned()) {
    const uint64_t value = it->get<uint64_t>();
    if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return unexpected("parser module manifest claim priority does not fit int32");
    }
    return static_cast<int32_t>(value);
  }
  const int64_t value = it->get<int64_t>();
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    return unexpected("parser module manifest claim priority does not fit int32");
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] Expected<uint16_t> readRouteFlags(const nlohmann::json& claim, size_t claim_index) {
  const auto routes = claim.find("routes");
  if (routes == claim.end() || !routes->is_array() || routes->empty()) {
    return unexpected(
        "parser module manifest claim " + std::to_string(claim_index) + " requires a non-empty routes array");
  }

  uint16_t flags = 0;
  for (const auto& route : *routes) {
    if (!route.is_string()) {
      return unexpected("parser module manifest claim " + std::to_string(claim_index) + " routes must contain strings");
    }
    const auto& name = route.get_ref<const std::string&>();
    if (name == "scalar") {
      flags |= PJ_PARSER_ROUTE_FLAG_SCALAR_V1;
    } else if (name == "object") {
      flags |= PJ_PARSER_ROUTE_FLAG_OBJECT_V1;
    } else {
      return unexpected("parser module manifest claim uses unknown route: " + name);
    }
  }
  return flags;
}

[[nodiscard]] Expected<std::set<std::string>> readSchemaDigests(const nlohmann::json& claim, size_t claim_index) {
  std::set<std::string> digests;
  const auto values = claim.find("schema_digests");
  if (values == claim.end()) {
    return digests;
  }
  if (!values->is_array()) {
    return unexpected(
        "parser module manifest claim " + std::to_string(claim_index) + " schema_digests must be an array");
  }
  if (values->size() > kMaximumParserModuleClaimSchemaDigests) {
    return unexpected(
        "parser module manifest claim " + std::to_string(claim_index) + " schema_digests has more than " +
        std::to_string(kMaximumParserModuleClaimSchemaDigests) + " entries");
  }
  for (const auto& value : *values) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() > kMaximumParserModuleManifestStringBytes) {
      return unexpected(
          "parser module manifest claim " + std::to_string(claim_index) +
          " schema_digests must contain strings of at most " + std::to_string(kMaximumParserModuleManifestStringBytes) +
          " bytes");
    }
    digests.insert(value.get<std::string>());
  }
  return digests;
}

// Every bound runs before the claims are built: each claim copies the module
// id, and the scratch-catalog validation copies the claims again.
[[nodiscard]] Expected<ParserModuleManifest> decodeBoundedManifest(
    std::string_view manifest_json, ParserClaimProvenance provenance) {
  if (manifest_json.empty()) {
    return unexpected("parser module manifest is empty");
  }
  if (manifest_json.size() > kMaximumParserModuleManifestBytes) {
    return unexpected("parser module manifest exceeds " + std::to_string(kMaximumParserModuleManifestBytes) + " bytes");
  }

  nlohmann::json json;
  try {
    json = nlohmann::json::parse(manifest_json);
  } catch (const nlohmann::json::exception& error) {
    return unexpected("parser module manifest is invalid JSON: " + std::string(error.what()));
  }
  if (!json.is_object()) {
    return unexpected("parser module manifest must be a JSON object");
  }
  if (json.contains("provenance")) {
    return unexpected("parser module manifest must not declare host-owned provenance");
  }

  const auto abi = json.find("module_abi");
  if (abi == json.end() || !abi->is_number_integer()) {
    return unexpected("parser module manifest missing integer module_abi");
  }
  // The host ABI version is positive, so any signed (hence negative) encoding
  // of module_abi is a mismatch by construction.
  if (!abi->is_number_unsigned() || abi->get<uint64_t>() != PJ_PARSER_MODULE_ABI_VERSION) {
    return unexpected(
        "parser module manifest module_abi does not match host ABI " + std::to_string(PJ_PARSER_MODULE_ABI_VERSION));
  }

  auto id = requiredString(json, "id", "parser module manifest");
  if (!id) {
    return unexpected(id.error());
  }
  auto name = requiredString(json, "name", "parser module manifest");
  if (!name) {
    return unexpected(name.error());
  }
  auto version = requiredString(json, "version", "parser module manifest");
  if (!version) {
    return unexpected(version.error());
  }
  if (auto parsed = SemVer::parse(*version); !parsed) {
    return unexpected("parser module manifest version is not valid SemVer: " + parsed.error());
  }

  const auto claims_json = json.find("claims");
  if (claims_json == json.end() || !claims_json->is_array()) {
    return unexpected("parser module manifest requires a claims array");
  }
  if (claims_json->size() > kMaximumParserModuleManifestClaims) {
    return unexpected(
        "parser module manifest has more than " + std::to_string(kMaximumParserModuleManifestClaims) + " claims");
  }

  ParserModuleManifest manifest{.id = *id, .name = *name, .version = *version, .claims = {}};
  manifest.claims.reserve(claims_json->size());

  for (size_t index = 0; index < claims_json->size(); ++index) {
    const auto& claim_json = (*claims_json)[index];
    if (!claim_json.is_object()) {
      return unexpected("parser module manifest claim " + std::to_string(index) + " must be an object");
    }
    if (claim_json.contains("provenance")) {
      return unexpected("parser module manifest claim must not declare host-owned provenance");
    }

    auto claim_id = requiredString(claim_json, "claim_id", "parser module manifest claim");
    if (!claim_id) {
      return unexpected(claim_id.error());
    }
    auto encoding = requiredString(claim_json, "encoding", "parser module manifest claim");
    if (!encoding) {
      return unexpected(encoding.error());
    }
    auto type_name = requiredString(claim_json, "type_name", "parser module manifest claim");
    if (!type_name) {
      return unexpected(type_name.error());
    }
    auto route_flags = readRouteFlags(claim_json, index);
    if (!route_flags) {
      return unexpected(route_flags.error());
    }
    auto priority = requiredPriority(claim_json, index);
    if (!priority) {
      return unexpected(priority.error());
    }
    auto schema_digests = readSchemaDigests(claim_json, index);
    if (!schema_digests) {
      return unexpected(schema_digests.error());
    }

    std::optional<sdk::BuiltinObjectType> object_type;
    const auto object_type_json = claim_json.find("object_type");
    if (object_type_json != claim_json.end()) {
      if (!object_type_json->is_string()) {
        return unexpected("parser module manifest claim object_type must be a string");
      }
      object_type = sdk::parseBuiltinObjectType(object_type_json->get_ref<const std::string&>());
      if (!object_type.has_value() || *object_type == sdk::BuiltinObjectType::kNone) {
        return unexpected(
            "parser module manifest claim uses unknown object_type: " + object_type_json->get<std::string>());
      }
    }

    ParserClaim claim{
        .encoding = *encoding,
        .type_name = *type_name,
        .route_flags = *route_flags,
        .object_type = object_type,
        .schema_digests = std::move(*schema_digests),
        .provider_id = manifest.id,
        .claim_id = *claim_id,
        .priority = *priority,
        .provenance = provenance,
    };
    manifest.claims.push_back(std::move(claim));
  }

  // Validate and normalize exactly as admission does, without widening the
  // catalog's public API: admit into a scratch catalog and read the claims back.
  ParserClaimCatalog scratch;
  if (auto admitted = scratch.admitClaims(std::move(manifest.claims), provenance, 0); !admitted) {
    return unexpected(admitted.error());
  }
  manifest.claims.clear();
  for (const auto& entry : scratch.claims()) {
    manifest.claims.push_back(entry.claim);
  }
  return manifest;
}

}  // namespace

Expected<ParserModuleManifest> decodeParserModuleManifest(
    std::string_view manifest_json, ParserClaimProvenance provenance) {
  try {
    return decodeBoundedManifest(manifest_json, provenance);
  } catch (const std::bad_alloc&) {
    return unexpected("allocation failed while decoding the parser module manifest");
  }
}

Expected<ParserModuleManifest> ingestParserModuleManifest(
    ParserClaimCatalog& catalog, std::string_view manifest_json, ParserClaimProvenance provenance,
    uint64_t provider_generation, const DiagnosticSink& sink) {
  auto manifest = decodeParserModuleManifest(manifest_json, provenance);
  if (!manifest) {
    if (sink) {
      sink(
          Diagnostic{
              .level = DiagnosticLevel::kError,
              .source = "ParserModuleManifest",
              .id = {},
              .message = manifest.error(),
              .timestamp = std::chrono::system_clock::now(),
          });
    }
    return unexpected(manifest.error());
  }
  auto admission = catalog.admitClaims(manifest->claims, provenance, provider_generation);
  if (!admission) {
    return unexpected(admission.error());
  }
  return manifest;
}

}  // namespace PJ
