#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "pj_base/assert.hpp"
#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/detail/json.hpp"

namespace PJ::sdk {

/// Canonical metadata key used by hosts to discover a built-in object renderer.
/// @since 0.21.0
inline constexpr std::string_view kBuiltinObjectTypeMetadataKey = "builtin_object_type";

/// Canonical metadata key marking a snapshot stream. Value `"true"` on a
/// SceneEntities/ImageAnnotations topic means EVERY entry is a complete
/// clear-and-replace snapshot, so a stateless consumer may render each entry
/// alone without accumulating state across prior entries. Absent (or any
/// other value) means entries may be incremental and a consumer must replay
/// the topic's history to reconstruct the current state.
/// @since 0.36.0
inline constexpr std::string_view kSnapshotMetadataKey = "pj_snapshot";

/// Canonical metadata key marking a DERIVED object topic: one produced by a host
/// data processor rather than ingested from a data source. The value names the
/// kind of processor that produced it and is an open set: `kDerivedOnDemandValue`
/// ("on_demand") today, and a host may stamp other kinds (e.g. "markers"). A
/// consumer (a viewer, the assistant, a script author) checks the key's presence
/// for "is derived" and the value for one specific kind; it is set by the HOST,
/// never by the plugin that created the processor. Match the key by parsing the
/// metadata JSON, never by substring.
/// @since 0.36.0
inline constexpr std::string_view kDerivedMetadataKey = "pj_derived";

/// The `kDerivedMetadataKey` value for a topic produced by an "on_demand" processor.
/// @since 0.36.0
inline constexpr std::string_view kDerivedOnDemandValue = "on_demand";

/// The `kDerivedMetadataKey` value of a plot-marker set a host publishes. A host
/// recognises and routes a marker topic by this metadata, never by its name.
/// @since 0.38.0
inline constexpr std::string_view kDerivedMarkersValue = "markers";

/// Which plots draw a marker set: `kMarkerScopeSeries` (the plots showing the series
/// `kMarkerTargetMetadataKey` names), `kMarkerScopeDataset` (every plot of the topic's
/// dataset) or `kMarkerScopeAll` (every plot of every dataset). A consumer treats an
/// unknown value as a marker set it does not understand.
/// @since 0.38.0
inline constexpr std::string_view kMarkerScopeMetadataKey = "pj_marker_scope";
/// The series key a "series"-scoped marker set annotates; absent for other scopes.
/// @since 0.38.0
inline constexpr std::string_view kMarkerTargetMetadataKey = "pj_marker_target";
/// The id of the generator that publishes the set; absent for a direct write.
/// @since 0.38.0
inline constexpr std::string_view kMarkerOwnerMetadataKey = "pj_marker_owner";
/// @since 0.38.0
inline constexpr std::string_view kMarkerScopeSeries = "series";
/// @since 0.38.0
inline constexpr std::string_view kMarkerScopeDataset = "dataset";
/// @since 0.38.0
inline constexpr std::string_view kMarkerScopeAll = "all";

/// Builds deterministic metadata JSON for an object topic.
///
/// `builtinObjectType()` accepts only the SDK enum and serializes its canonical
/// `name()` (for example, `BuiltinObjectType::kImage` becomes `"kImage"`).
/// Custom string fields are emitted in lexicographic key order after the
/// canonical type field. `build()` returns an error after any invalid setter
/// call, including in release builds where assertions are disabled.
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
      builtin_object_type_.reset();
      setError("builtin object topic type must be a known, non-reserved value other than kNone");
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
      builtin_object_type_.reset();
      setError("metadata key \"builtin_object_type\" is reserved; use builtinObjectType()");
      PJ_ASSERT(is_custom_key, "builtin_object_type must be set through builtinObjectType()");
      return *this;
    }
    strings_.insert_or_assign(std::string(key), std::string(value));
    return *this;
  }

  /// Mark this topic as a snapshot stream (see kSnapshotMetadataKey): every
  /// entry is a complete clear-and-replace snapshot. `false` removes the key
  /// again (incremental, the default).
  /// @since 0.36.0
  ObjectTopicMetadataBuilder& snapshot(bool value = true) {
    if (value) {
      strings_.insert_or_assign(std::string(kSnapshotMetadataKey), std::string("true"));
    } else {
      strings_.erase(std::string(kSnapshotMetadataKey));
    }
    return *this;
  }

  /// Serialize the accumulated metadata as a deterministic JSON object, or
  /// return the first contract error recorded by a setter.
  /// @since 0.21.0
  [[nodiscard]] Expected<std::string> build() const {
    if (error_.has_value()) {
      return unexpected(*error_);
    }

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
  std::optional<std::string> error_;

  void setError(std::string_view error) {
    if (!error_.has_value()) {
      error_ = error;
    }
  }
};

}  // namespace PJ::sdk
