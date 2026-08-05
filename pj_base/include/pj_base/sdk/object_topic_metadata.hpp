#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "pj_base/assert.hpp"
#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/sdk/detail/json.hpp"

namespace PJ::sdk {

/// Canonical metadata key used by hosts to discover a built-in object renderer.
/// @since 0.21.0
inline constexpr std::string_view kBuiltinObjectTypeMetadataKey = "builtin_object_type";

/// Builds deterministic metadata JSON for an object topic.
///
/// `builtinObjectType()` accepts only the SDK enum and serializes its canonical
/// `name()` (for example, `BuiltinObjectType::kImage` becomes `"kImage"`).
/// Custom string fields are emitted in lexicographic key order after the
/// canonical type field.
///
/// @since 0.21.0
class ObjectTopicMetadataBuilder {
 public:
  /// Select the canonical built-in object renderer for this topic.
  ///
  /// `kNone`, reserved values, and values unknown to this SDK are contract
  /// violations and are never added to the output.
  /// @since 0.21.0
  ObjectTopicMetadataBuilder& builtinObjectType(BuiltinObjectType type) {
    const auto parsed = parseBuiltinObjectType(name(type));
    const bool is_known_type = type != BuiltinObjectType::kNone && parsed.has_value() && *parsed == type;
    if (!is_known_type) {
      PJ_ASSERT(is_known_type, "builtin object topic type must be a known, non-reserved value other than kNone");
      return *this;
    }
    builtin_object_type_ = type;
    return *this;
  }

  /// Add or replace a custom string field.
  ///
  /// The canonical `builtin_object_type` key is reserved; set it through
  /// `builtinObjectType()` instead.
  /// @since 0.21.0
  ObjectTopicMetadataBuilder& string(std::string_view key, std::string_view value) {
    const bool is_custom_key = key != kBuiltinObjectTypeMetadataKey;
    if (!is_custom_key) {
      PJ_ASSERT(is_custom_key, "builtin_object_type must be set through builtinObjectType()");
      return *this;
    }
    strings_.insert_or_assign(std::string(key), std::string(value));
    return *this;
  }

  /// Serialize the accumulated metadata as a deterministic JSON object.
  /// @since 0.21.0
  [[nodiscard]] std::string build() const {
    std::string out;
    out.reserve(48U + strings_.size() * 16U);
    out.push_back('{');
    bool first = true;
    const auto append_string = [&](std::string_view key, std::string_view value) {
      if (!first) {
        out.push_back(',');
      }
      first = false;
      detail::appendJsonString(out, key);
      out.push_back(':');
      detail::appendJsonString(out, value);
    };

    if (builtin_object_type_.has_value()) {
      append_string(kBuiltinObjectTypeMetadataKey, name(*builtin_object_type_));
    }
    for (const auto& [key, value] : strings_) {
      append_string(key, value);
    }
    out.push_back('}');
    return out;
  }

 private:
  std::optional<BuiltinObjectType> builtin_object_type_;
  std::map<std::string, std::string> strings_;
};

}  // namespace PJ::sdk
