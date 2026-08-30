/**
 * @file source_descriptor.hpp
 * @brief The allowlisted, canonically serialized, content-addressed SOURCE
 *        DESCRIPTOR that a layout embeds and a descriptor-import provider
 *        re-imports, and the identity scheme derived from it.
 *
 * A source descriptor is a JSON object. The provider declares a
 * SourceDescriptorPolicy: which fields make up the request IDENTITY (their
 * canonical serialization is hashed into a stable identity string), which
 * fields are PRESENTATION only (allowed, but excluded from the identity — a
 * display name is not a new request), the resource limits, and the
 * IdentityScheme (prefix + digest width). Everything else is rejected
 * outright: an unknown field is the credential-smuggling and forward-
 * compatibility hazard the allowlist exists to close, so it is never silently
 * ignored.
 *
 * Typed validation (which fields are required, their value semantics) stays
 * with the provider; this module only guarantees that whatever it parses is
 * allowlisted, bounded, and canonicalizes to the same bytes everywhere.
 *
 * Canonical form: only the identity fields present, in ALPHABETICAL key order,
 * compact (no whitespace), UTF-8, nested objects key-sorted. Providers pin
 * the resulting bytes and identities with a vectors file: changed bytes are a
 * different request (a cache miss), never a silently regenerated vector.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {

/// How an identity string is spelled: `prefix` + the first `digest_hex_chars`
/// lowercase-hex characters of sha256(canonical bytes). The ONE owner of that
/// rule — minting (identityFor) and parsing (digestOf) can never disagree.
/// Naming the scheme in the prefix (e.g. "mosaico:v1:sha256/128:") keeps
/// identities from different providers, or a future digest scheme, from ever
/// colliding.
struct IdentityScheme {
  std::string prefix;
  /// Requested digest width in hex characters; normalized to 32..64 (a
  /// 128-bit collision-resistance floor) and even by hexChars(), so an
  /// out-of-range value never yields a scheme whose minted identities fail
  /// its own parse — nor a trivially collidable address space.
  std::size_t digest_hex_chars = 32;

  [[nodiscard]] std::size_t hexChars() const noexcept;

  /// The identity of `canonical_json`.
  [[nodiscard]] std::string identityFor(std::string_view canonical_json) const;

  /// The digest part of `identity`, or nullopt unless `identity` is EXACTLY
  /// prefix + hexChars() lowercase hex digits. The gate every filesystem
  /// consumer applies before an identity may name a path.
  [[nodiscard]] std::optional<std::string> digestOf(std::string_view identity) const;
};

struct SourceDescriptorPolicy {
  /// Fields that define the request. Their canonical serialization is the
  /// identity's digest input.
  std::vector<std::string> identity_fields;
  /// Fields a descriptor may carry that do not change the request (display
  /// names, hints). Any other field is rejected.
  std::vector<std::string> presentation_fields;
  IdentityScheme identity;
  /// Resource guard: the raw descriptor text.
  std::size_t max_descriptor_bytes = 64 * 1024;
  /// Resource guard: every string value and object key, at any depth.
  std::size_t max_string_bytes = 4096;
  /// Resource guard: every array length and object member count, at any depth.
  std::size_t max_container_entries = 4096;
};

/// Parse + allowlist + bound `json` under `policy`. Fails (with a
/// human-readable reason) on: text over max_descriptor_bytes, invalid JSON, a
/// non-object root, any top-level key outside identity_fields ∪
/// presentation_fields, any string/key over max_string_bytes, any array or
/// object over max_container_entries, or nesting deeper than 16 levels.
[[nodiscard]] Expected<nlohmann::json> parseSourceDescriptor(
    std::string_view json, const SourceDescriptorPolicy& policy);

/// The canonical bytes of `descriptor` (an object): identity fields only,
/// alphabetical, compact. Presentation fields and unknown fields are dropped.
[[nodiscard]] std::string canonicalSourceDescriptorJson(
    const nlohmann::json& descriptor, const SourceDescriptorPolicy& policy);

/// policy.identity.identityFor(canonicalSourceDescriptorJson(descriptor)).
[[nodiscard]] std::string sourceDescriptorIdentity(
    const nlohmann::json& descriptor, const SourceDescriptorPolicy& policy);

/// Lowercase hex of the first `hex_chars` characters (normalized to 32..64,
/// even) of sha256(data). Exposed so artifact validators can re-hash embedded
/// provenance instead of trusting a stored identity string.
[[nodiscard]] std::string sha256Hex(std::string_view data, std::size_t hex_chars = 64);

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
