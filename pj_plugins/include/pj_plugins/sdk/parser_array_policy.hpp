/**
 * @file parser_array_policy.hpp
 * @brief The "maximum array size + clamp/skip" contract every message-parser
 *        plugin applies when an incoming array field exceeds a configured
 *        length.
 *
 * Parsers flatten each array element into its own scalar series
 * (field[0], field[1], ...), so an unbounded array becomes an unbounded
 * number of series. ArrayLimit caps that: max_size is the threshold and
 * policy decides what happens past it — Clamp keeps the first max_size
 * elements, Skip drops the whole field.
 *
 * The JSON keys below are the cross-plugin contract: a layout or config
 * written by one parser must mean the same thing to every other parser, so
 * the reader accepts the canonical keys first and the legacy per-parser
 * spellings (clamp_large_arrays / discard_large_arrays) as a fallback.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace PJ {
namespace sdk {

/// What a parser does with an array field longer than ArrayLimit::max_size.
enum class ArrayPolicy : uint8_t {
  /// Keep the first max_size elements; drop the rest. The series still exist,
  /// truncated.
  kClamp,
  /// Drop the entire field; no series is created for it.
  kSkip,
};

/// The unified array-size policy carried in a parser's config. Defaults are
/// canonical across every parser: clamp the first 500 elements.
struct ArrayLimit {
  /// Maximum number of array elements materialized per field. 0 = unlimited
  /// (no element is ever dropped, whatever the policy).
  uint32_t max_size = 500;
  /// Action taken when an array exceeds max_size.
  ArrayPolicy policy = ArrayPolicy::kClamp;

  /// True when oversized arrays are truncated to max_size (vs skipped whole).
  /// Convenience for parsers whose flatten helpers take a `clamp` bool.
  [[nodiscard]] bool clamp() const {
    return policy == ArrayPolicy::kClamp;
  }
};

/// Canonical JSON keys. Parsers should read/write these.
inline constexpr const char* kMaxArraySizeKey = "max_array_size";
inline constexpr const char* kArrayPolicyKey = "array_policy";  // "clamp" | "skip"

/// Reads an ArrayLimit from a parser config object.
///
/// Precedence:
///   1. Canonical keys: "max_array_size" (uint) + "array_policy" ("clamp"|"skip").
///   2. Legacy fallback for configs written before this contract:
///        "discard_large_arrays" (bool, true -> Skip)
///        "clamp_large_arrays"   (bool, true -> Clamp, false -> Skip)
///   3. Defaults (max_size = 500, policy = Clamp) for anything absent.
///
/// max_array_size is read independently of the policy keys. Never throws: a
/// non-object config, a missing key, or a malformed value — wrong JSON type,
/// a negative number, or one larger than uint32_t — falls back to the default
/// for that field (it is neither truncated nor wrapped).
[[nodiscard]] inline ArrayLimit arrayLimitFromJson(const nlohmann::json& cfg) {
  ArrayLimit limit;
  if (!cfg.is_object()) {
    return limit;
  }

  // Accept max_array_size only as a non-negative integer that fits uint32_t.
  // is_number_integer() covers both signed and unsigned JSON integers (a value
  // parsed from text is unsigned, one built from a C++ int is signed) while
  // rejecting strings, floats, and bools. The range check rejects negatives
  // and overflow so they fall back to the default rather than wrapping.
  if (auto it = cfg.find(kMaxArraySizeKey); it != cfg.end() && it->is_number_integer()) {
    const int64_t value = it->get<int64_t>();
    if (value >= 0 && value <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      limit.max_size = static_cast<uint32_t>(value);
    }
  }

  if (auto policy_it = cfg.find(kArrayPolicyKey); policy_it != cfg.end() && policy_it->is_string()) {
    limit.policy = (policy_it->get<std::string>() == "skip") ? ArrayPolicy::kSkip : ArrayPolicy::kClamp;
  } else if (auto discard_it = cfg.find("discard_large_arrays"); discard_it != cfg.end() && discard_it->is_boolean()) {
    limit.policy = discard_it->get<bool>() ? ArrayPolicy::kSkip : ArrayPolicy::kClamp;
  } else if (auto clamp_it = cfg.find("clamp_large_arrays"); clamp_it != cfg.end() && clamp_it->is_boolean()) {
    limit.policy = clamp_it->get<bool>() ? ArrayPolicy::kClamp : ArrayPolicy::kSkip;
  }
  return limit;
}

/// Writes the canonical keys for an ArrayLimit into a config object. Also
/// mirrors the legacy bools so a plugin built before this contract can still
/// honor the policy.
inline void arrayLimitToJson(nlohmann::json& cfg, const ArrayLimit& limit) {
  cfg[kMaxArraySizeKey] = limit.max_size;
  cfg[kArrayPolicyKey] = limit.clamp() ? "clamp" : "skip";
  cfg["clamp_large_arrays"] = limit.clamp();
  cfg["discard_large_arrays"] = !limit.clamp();
}

/// Convenience overload for call sites that carry a loose (max_size, clamp)
/// pair instead of an ArrayLimit.
inline void arrayLimitToJson(nlohmann::json& cfg, uint32_t max_size, bool clamp) {
  arrayLimitToJson(cfg, ArrayLimit{.max_size = max_size, .policy = clamp ? ArrayPolicy::kClamp : ArrayPolicy::kSkip});
}

}  // namespace sdk
}  // namespace PJ
