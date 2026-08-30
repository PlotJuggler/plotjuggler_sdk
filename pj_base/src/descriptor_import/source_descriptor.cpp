// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/descriptor_import/source_descriptor.hpp"

#include <algorithm>

#include "descriptor_import/sha256.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {

namespace {

constexpr std::size_t kMaxDepth = 16;

std::size_t normalizedHexChars(std::size_t hex_chars) {
  if (hex_chars < 2) {
    return 2;
  }
  if (hex_chars > 64) {
    return 64;
  }
  return hex_chars - (hex_chars % 2);
}

bool isAllowedField(const std::string& key, const SourceDescriptorPolicy& policy) {
  const auto in = [&key](const std::vector<std::string>& fields) {
    return std::find(fields.begin(), fields.end(), key) != fields.end();
  };
  return in(policy.identity_fields) || in(policy.presentation_fields);
}

// Recursive resource guard: strings/keys, container sizes, nesting depth.
std::optional<std::string> boundsViolation(
    const nlohmann::json& value, const SourceDescriptorPolicy& policy, std::size_t depth) {
  if (depth > kMaxDepth) {
    return "descriptor nests deeper than " + std::to_string(kMaxDepth) + " levels";
  }
  if (value.is_string()) {
    if (value.get_ref<const std::string&>().size() > policy.max_string_bytes) {
      return "a string value exceeds the " + std::to_string(policy.max_string_bytes) + "-byte limit";
    }
    return std::nullopt;
  }
  if (value.is_array()) {
    if (value.size() > policy.max_container_entries) {
      return "an array exceeds the " + std::to_string(policy.max_container_entries) + "-entry limit";
    }
    for (const auto& entry : value) {
      if (auto violation = boundsViolation(entry, policy, depth + 1)) {
        return violation;
      }
    }
    return std::nullopt;
  }
  if (value.is_object()) {
    if (value.size() > policy.max_container_entries) {
      return "an object exceeds the " + std::to_string(policy.max_container_entries) + "-member limit";
    }
    for (const auto& item : value.items()) {
      if (item.key().size() > policy.max_string_bytes) {
        return "an object key exceeds the " + std::to_string(policy.max_string_bytes) + "-byte limit";
      }
      if (auto violation = boundsViolation(item.value(), policy, depth + 1)) {
        return violation;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------
// IdentityScheme
// ---------------------------------------------------------------------------

std::size_t IdentityScheme::hexChars() const noexcept {
  return normalizedHexChars(digest_hex_chars);
}

std::string IdentityScheme::identityFor(std::string_view canonical_json) const {
  return prefix + sha256Hex(canonical_json, hexChars());
}

std::optional<std::string> IdentityScheme::digestOf(std::string_view identity) const {
  const std::size_t hex_chars = hexChars();
  if (identity.size() != prefix.size() + hex_chars || !identity.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view hex = identity.substr(prefix.size());
  const bool lower_hex = std::all_of(
      hex.begin(), hex.end(), [](const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  if (!lower_hex) {
    return std::nullopt;
  }
  return std::string(hex);
}

// ---------------------------------------------------------------------------
// Source descriptor
// ---------------------------------------------------------------------------

Expected<nlohmann::json> parseSourceDescriptor(std::string_view json, const SourceDescriptorPolicy& policy) {
  if (json.size() > policy.max_descriptor_bytes) {
    return unexpected("descriptor exceeds the " + std::to_string(policy.max_descriptor_bytes) + "-byte limit");
  }
  nlohmann::json obj = nlohmann::json::parse(json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (obj.is_discarded()) {
    return unexpected("descriptor is not valid JSON");
  }
  if (!obj.is_object()) {
    return unexpected("descriptor is not a JSON object");
  }
  // Allowlist BEFORE anything else: an unknown field is rejected even when
  // everything required is present and valid.
  for (const auto& item : obj.items()) {
    if (!isAllowedField(item.key(), policy)) {
      return unexpected("unknown field \"" + item.key() + "\"");
    }
  }
  if (auto violation = boundsViolation(obj, policy, 0)) {
    return unexpected(std::move(*violation));
  }
  return obj;
}

std::string canonicalSourceDescriptorJson(const nlohmann::json& descriptor, const SourceDescriptorPolicy& policy) {
  std::vector<std::string> fields = policy.identity_fields;
  std::sort(fields.begin(), fields.end());
  fields.erase(std::unique(fields.begin(), fields.end()), fields.end());
  nlohmann::ordered_json canonical = nlohmann::ordered_json::object();
  if (descriptor.is_object()) {
    for (const std::string& field : fields) {
      const auto it = descriptor.find(field);
      if (it != descriptor.end()) {
        // nlohmann::json keeps nested object members key-sorted, so the
        // conversion below yields deterministic bytes at every depth.
        canonical[field] = nlohmann::ordered_json(*it);
      }
    }
  }
  return canonical.dump();
}

std::string sourceDescriptorIdentity(const nlohmann::json& descriptor, const SourceDescriptorPolicy& policy) {
  return policy.identity.identityFor(canonicalSourceDescriptorJson(descriptor, policy));
}

std::string sha256Hex(std::string_view data, std::size_t hex_chars) {
  const auto digest = detail::sha256(data);
  const std::size_t bytes = normalizedHexChars(hex_chars) / 2;
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (std::size_t i = 0; i < bytes; ++i) {
    out += kHex[digest[i] >> 4];
    out += kHex[digest[i] & 0xF];
  }
  return out;
}

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
