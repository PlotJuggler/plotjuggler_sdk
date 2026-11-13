// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_claim_catalog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <string>
#include <utility>

namespace PJ {
namespace {

constexpr std::array<std::string_view, 11> kRegisteredEncodings = {
    "bson", "cbor", "data_tamer", "json", "msgpack", "omgidl", "protobuf", "ros1", "ros1msg", "ros2", "ros2msg",
};

[[nodiscard]] bool isIdentifierStart(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
}

[[nodiscard]] bool isIdentifierContinuation(char value) noexcept {
  return isIdentifierStart(value) || (value >= '0' && value <= '9');
}

[[nodiscard]] bool isProtobufFullName(std::string_view name) noexcept {
  size_t component_start = 0;
  while (component_start < name.size()) {
    const size_t component_end = name.find('.', component_start);
    const size_t end = component_end == std::string_view::npos ? name.size() : component_end;
    const std::string_view component = name.substr(component_start, end - component_start);
    if (component.empty() || !isIdentifierStart(component.front())) {
      return false;
    }
    if (!std::all_of(component.begin() + 1, component.end(), isIdentifierContinuation)) {
      return false;
    }
    if (component_end == std::string_view::npos) {
      return true;
    }
    component_start = component_end + 1;
  }
  return false;
}

[[nodiscard]] bool isKnownObjectType(sdk::BuiltinObjectType object_type) noexcept {
  if (object_type == sdk::BuiltinObjectType::kNone) {
    return false;
  }
  const auto parsed = sdk::parseBuiltinObjectType(sdk::name(object_type));
  return parsed.has_value() && *parsed == object_type;
}

[[nodiscard]] bool isSchemaDigest(std::string_view digest) noexcept {
  constexpr std::string_view kPrefix = "sha256:";
  if (!digest.starts_with(kPrefix) || digest.size() != kPrefix.size() + 64) {
    return false;
  }
  return std::all_of(digest.begin() + static_cast<ptrdiff_t>(kPrefix.size()), digest.end(), [](char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
  });
}

[[nodiscard]] Expected<ParserClaim> normalizeAndValidateClaim(ParserClaim claim) {
  if (claim.provider_id.empty()) {
    return unexpected("parser claim provider_id must not be empty");
  }
  if (claim.claim_id.empty()) {
    return unexpected("parser claim claim_id must not be empty");
  }
  if (claim.priority < -1000 || claim.priority > 1000) {
    return unexpected(
        "parser claim priority is outside the admitted range [-1000,1000]: " + claim.provider_id + "/" +
        claim.claim_id);
  }
  if (!isRegisteredParserEncoding(claim.encoding)) {
    return unexpected("parser claim uses unknown encoding: " + claim.encoding);
  }

  auto normalized_type = normalizeParserTypeName(claim.encoding, claim.type_name);
  if (!normalized_type) {
    return unexpected(normalized_type.error());
  }
  claim.type_name = std::move(*normalized_type);

  constexpr uint16_t kKnownRouteFlags = PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1;
  if (claim.route_flags == 0 || (claim.route_flags & ~kKnownRouteFlags) != 0) {
    return unexpected("parser claim route_flags must contain only scalar and/or object");
  }

  const bool has_object_route = (claim.route_flags & PJ_PARSER_ROUTE_FLAG_OBJECT_V1) != 0;
  if (claim.type_name == "*" && has_object_route) {
    return unexpected("parser claim wildcard type_name cannot claim the object route");
  }
  if (has_object_route != claim.object_type.has_value()) {
    return unexpected(
        has_object_route ? "parser claim object route requires object_type"
                         : "parser claim object_type is forbidden without the object route");
  }
  if (claim.object_type.has_value() && !isKnownObjectType(*claim.object_type)) {
    return unexpected("parser claim uses unknown object_type");
  }
  for (const auto& digest : claim.schema_digests) {
    if (!isSchemaDigest(digest)) {
      return unexpected("parser claim schema_digest must use sha256:<64-hex>");
    }
  }
  return claim;
}

[[nodiscard]] std::string claimIdentity(const ParserClaim& claim) {
  return claim.provider_id + "/" + claim.claim_id;
}

}  // namespace

std::span<const std::string_view> registeredParserEncodings() noexcept {
  return kRegisteredEncodings;
}

bool isRegisteredParserEncoding(std::string_view encoding) noexcept {
  return std::find(kRegisteredEncodings.begin(), kRegisteredEncodings.end(), encoding) != kRegisteredEncodings.end();
}

Expected<std::string> normalizeParserTypeName(std::string_view encoding, std::string_view type_name) {
  if (!isRegisteredParserEncoding(encoding)) {
    return unexpected("cannot normalize type name for unknown encoding: " + std::string(encoding));
  }
  if (type_name.empty()) {
    return unexpected("parser claim type_name must not be empty");
  }
  if (type_name == "*") {
    return std::string(type_name);
  }

  if (encoding == "ros2msg") {
    const size_t first = type_name.find('/');
    if (first == std::string_view::npos || first == 0 || first + 1 == type_name.size()) {
      return unexpected("ros2msg type_name must be pkg/Type or pkg/msg/Type");
    }
    const size_t second = type_name.find('/', first + 1);
    if (second == std::string_view::npos) {
      return std::string(type_name.substr(0, first)) + "/msg/" + std::string(type_name.substr(first + 1));
    }
    if (type_name.substr(first + 1, second - first - 1) != "msg" || second + 1 == type_name.size() ||
        type_name.find('/', second + 1) != std::string_view::npos) {
      return unexpected("ros2msg type_name must be pkg/Type or pkg/msg/Type");
    }
    return std::string(type_name);
  }

  if (encoding == "protobuf") {
    if (type_name.front() == '.') {
      type_name.remove_prefix(1);
    }
    if (!isProtobufFullName(type_name)) {
      return unexpected("protobuf type_name must be a full dotted message name");
    }
    return std::string(type_name);
  }

  return std::string(type_name);
}

Expected<std::vector<ParserClaim>> synthesizeParserPluginClaims(
    std::string_view provider_id, std::span<const std::string> manifest_encodings,
    std::span<const ParserPluginExactClaim> exact_claims, ParserClaimProvenance provenance) {
  if (provider_id.empty()) {
    return unexpected("parser plugin provider id must not be empty");
  }

  std::vector<ParserClaim> claims;
  claims.reserve(manifest_encodings.size() + exact_claims.size());
  std::set<std::string> encodings;
  std::set<std::pair<std::string, std::string>> identities;

  for (const auto& encoding : manifest_encodings) {
    ParserClaim wildcard{
        .encoding = encoding,
        .type_name = "*",
        .route_flags = PJ_PARSER_ROUTE_FLAG_SCALAR_V1,
        .object_type = std::nullopt,
        .schema_digests = {},
        .provider_id = std::string(provider_id),
        .claim_id = "wildcard:" + encoding,
        .priority = 0,
        .provenance = provenance,
    };
    auto validated = normalizeAndValidateClaim(std::move(wildcard));
    if (!validated) {
      return unexpected(validated.error());
    }
    if (!encodings.insert(encoding).second || !identities.emplace(validated->provider_id, validated->claim_id).second) {
      return unexpected("duplicate parser claim identity: " + claimIdentity(*validated));
    }
    claims.push_back(std::move(*validated));
  }

  for (const auto& exact : exact_claims) {
    if (!encodings.contains(exact.encoding)) {
      return unexpected("parser exact claim encoding is absent from its manifest: " + exact.encoding);
    }
    if (exact.classification.match != PJ_PARSER_ROUTE_MATCH_EXACT_V1) {
      return unexpected("parser route classification reported a non-exact match");
    }
    if (exact.classification.status == PJ_PARSER_ROUTE_STATUS_DECLINED_V1) {
      if (exact.classification.route_flags != 0 ||
          exact.classification.object_type != static_cast<uint16_t>(sdk::BuiltinObjectType::kNone)) {
        return unexpected("declined parser route classification contains claimed route data");
      }
      continue;
    }
    if (exact.classification.status != PJ_PARSER_ROUTE_STATUS_CLAIMED_V1) {
      return unexpected("parser route classification has an invalid status");
    }
    if (exact.type_name == "*") {
      return unexpected("parser route classification extension cannot report wildcard claims");
    }

    std::optional<sdk::BuiltinObjectType> object_type;
    if ((exact.classification.route_flags & PJ_PARSER_ROUTE_FLAG_OBJECT_V1) != 0) {
      object_type = static_cast<sdk::BuiltinObjectType>(exact.classification.object_type);
    } else if (exact.classification.object_type != static_cast<uint16_t>(sdk::BuiltinObjectType::kNone)) {
      return unexpected("parser route classification has object_type without an object route");
    }

    auto normalized_type = normalizeParserTypeName(exact.encoding, exact.type_name);
    if (!normalized_type) {
      return unexpected(normalized_type.error());
    }
    ParserClaim claim{
        .encoding = exact.encoding,
        .type_name = *normalized_type,
        .route_flags = exact.classification.route_flags,
        .object_type = object_type,
        .schema_digests = exact.schema_digests,
        .provider_id = std::string(provider_id),
        .claim_id = "handler:" + exact.encoding + ":" + *normalized_type,
        .provenance = provenance,
    };
    auto validated = normalizeAndValidateClaim(std::move(claim));
    if (!validated) {
      return unexpected(validated.error());
    }
    if (!identities.emplace(validated->provider_id, validated->claim_id).second) {
      return unexpected("duplicate parser claim identity: " + claimIdentity(*validated));
    }
    claims.push_back(std::move(*validated));
  }

  return claims;
}

ParserClaimCatalog::ParserClaimCatalog(DiagnosticSink sink, std::string diagnostic_source)
    : sink_(std::move(sink)), diagnostic_source_(std::move(diagnostic_source)) {}

void ParserClaimCatalog::setDiagnosticSink(DiagnosticSink sink) {
  sink_ = std::move(sink);
}

Status ParserClaimCatalog::admitClaims(
    std::vector<ParserClaim> claims, ParserClaimProvenance provenance, uint64_t provider_generation) {
  std::vector<ParserClaim> validated_claims;
  validated_claims.reserve(claims.size());
  std::set<std::pair<std::string, std::string>> batch_identities;

  for (auto& claim : claims) {
    claim.provenance = provenance;
    auto validated = normalizeAndValidateClaim(std::move(claim));
    if (!validated) {
      report(DiagnosticLevel::kError, {}, validated.error());
      return unexpected(validated.error());
    }
    const auto identity = std::pair(validated->provider_id, validated->claim_id);
    if (!batch_identities.insert(identity).second) {
      const std::string error = "duplicate parser claim identity: " + claimIdentity(*validated);
      report(DiagnosticLevel::kError, validated->provider_id, error);
      return unexpected(error);
    }
    const bool already_present = std::any_of(claims_.begin(), claims_.end(), [&](const ParserClaimEntry& entry) {
      return entry.claim.provider_id == identity.first && entry.claim.claim_id == identity.second;
    });
    if (already_present) {
      const std::string error = "duplicate parser claim identity: " + claimIdentity(*validated);
      report(DiagnosticLevel::kError, validated->provider_id, error);
      return unexpected(error);
    }
    validated_claims.push_back(std::move(*validated));
  }

  for (auto& claim : validated_claims) {
    claims_.push_back(ParserClaimEntry{.claim = std::move(claim), .provider_generation = provider_generation});
  }
  if (!validated_claims.empty()) {
    ++generation_;
  }
  return okStatus();
}

Status ParserClaimCatalog::admitParserPlugin(
    std::string_view provider_id, std::span<const std::string> manifest_encodings,
    std::span<const ParserPluginExactClaim> exact_claims, ParserClaimProvenance provenance,
    uint64_t provider_generation) {
  auto claims = synthesizeParserPluginClaims(provider_id, manifest_encodings, exact_claims, provenance);
  if (!claims) {
    report(DiagnosticLevel::kError, provider_id, claims.error());
    return unexpected(claims.error());
  }
  return admitClaims(std::move(*claims), provenance, provider_generation);
}

bool ParserClaimCatalog::removeProvider(std::string_view provider_id) {
  const size_t prior_size = claims_.size();
  std::erase_if(claims_, [&](const ParserClaimEntry& entry) { return entry.claim.provider_id == provider_id; });
  if (claims_.size() == prior_size) {
    return false;
  }
  ++generation_;
  return true;
}

void ParserClaimCatalog::clear() {
  if (claims_.empty()) {
    return;
  }
  claims_.clear();
  ++generation_;
}

void ParserClaimCatalog::report(DiagnosticLevel level, std::string_view id, std::string message) const {
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
