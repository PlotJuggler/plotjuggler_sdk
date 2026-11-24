#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <map>
#include <string>
#include <string_view>

#include "pj_base/sdk/detail/json.hpp"

namespace PJ::sdk {

/// Canonical field- and topic-metadata key: plain text the host shows as a tooltip on the field's or topic's row in the
/// dataset list.
/// @since 1.2.0
inline constexpr std::string_view kFieldDescriptionMetadataKey = "description";

/// Builds the JSON object `SourceWriteHostView::setFieldMetadata()` and `setTopicMetadata()` take.
///
/// The host replaces the whole document on every call, so set every key before `build()`. Keys are emitted in
/// lexicographic order; setting a key again replaces its value. An empty builder yields "{}", which clears the
/// metadata.
///
/// @since 1.2.0
class FieldMetadataBuilder {
 public:
  /// Set the canonical `description` key.
  /// @since 1.2.0
  FieldMetadataBuilder& description(std::string_view text) {
    return string(kFieldDescriptionMetadataKey, text);
  }

  /// Add or replace a custom string field. The host keeps keys it does not display.
  /// @since 1.2.0
  FieldMetadataBuilder& string(std::string_view key, std::string_view value) {
    strings_.insert_or_assign(std::string(key), std::string(value));
    return *this;
  }

  /// Serialize the accumulated metadata as a deterministic JSON object.
  /// @since 1.2.0
  [[nodiscard]] std::string build() const {
    std::string out;
    out.push_back('{');
    bool first = true;
    for (const auto& [key, value] : strings_) {
      if (!first) {
        out.push_back(',');
      }
      first = false;
      detail::appendJsonString(out, key);
      out.push_back(':');
      detail::appendJsonString(out, value);
    }
    out.push_back('}');
    return out;
  }

 private:
  std::map<std::string, std::string> strings_;
};

}  // namespace PJ::sdk
