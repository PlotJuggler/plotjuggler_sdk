/**
 * @file timestamp_policy.hpp
 * @brief Shared timestamp-axis detection, unit persistence, and conversion
 *        policy for plugins that import columnar data.
 *
 * A column name alone is not enough to make a useful epoch axis: narrow
 * integers overflow near the epoch, float32 loses sub-second resolution, and
 * expanded list elements are not scalar columns. This header keeps those
 * decisions consistent without depending on a particular columnar library.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string_view>

#include "pj_base/time.hpp"

namespace PJ {
namespace sdk {

/// Storage classification of a candidate column. Integer kinds carry ticks in
/// the policy's TimeUnit; floating kinds carry seconds.
enum class TimestampStorage : uint8_t {
  /// A native timestamp whose unit is known to the caller.
  kNativeTimestamp,
  kInt64,
  kUInt64,
  /// Double-precision seconds, with about 238 ns resolution at the present epoch.
  kFloat64,
  kInt32,
  kUInt32,
  /// Signed or unsigned 8/16-bit integers.
  kNarrowInt,
  /// Single-precision seconds: 24 significant bits, so spacing exceeds 100 s at the present epoch.
  kFloat32,
  /// Storage that cannot serve as a timestamp axis.
  kOther,
};

/// When a TimestampStorage may become the time axis.
enum class TimestampEligibility : uint8_t {
  /// May be picked automatically by detectTimestampColumn().
  kEligible,
  /// Only when named explicitly, after surfacing explicitOnlyWarning().
  kExplicitOnly,
  /// Never.
  kIneligible,
};

/// An integer column is eligible for automatic selection when its width can hold instants at
/// least this far past the Unix epoch at the configured unit: int32 seconds (2038-01-19), the
/// narrowest storage in common use for absolute time.
inline constexpr int64_t kEligibleHorizonSeconds = std::numeric_limits<int32_t>::max();

namespace detail {

/// Largest tick count the integer kind can hold; nullopt for non-integer kinds.
[[nodiscard]] constexpr std::optional<uint64_t> maxIntegerTicks(TimestampStorage kind) noexcept {
  switch (kind) {
    case TimestampStorage::kInt64:
      return static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    case TimestampStorage::kUInt64:
      return std::numeric_limits<uint64_t>::max();
    case TimestampStorage::kInt32:
      return static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    case TimestampStorage::kUInt32:
      return std::numeric_limits<uint32_t>::max();
    case TimestampStorage::kNarrowInt:
      return static_cast<uint64_t>(std::numeric_limits<int8_t>::max());
    case TimestampStorage::kNativeTimestamp:
    case TimestampStorage::kFloat64:
    case TimestampStorage::kFloat32:
    case TimestampStorage::kOther:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace detail

/// Whether storage holding ticks of `unit` may become the time axis, without inspecting a column
/// name: kEligible for automatic selection, kExplicitOnly when a producer must name it and surface
/// explicitOnlyWarning(), kIneligible never. Integers are eligible when they reach
/// kEligibleHorizonSeconds at that unit; float32 is always explicit-only because its precision,
/// not its range, is the problem.
[[nodiscard]] constexpr TimestampEligibility timestampEligibility(TimestampStorage kind, PJ::TimeUnit unit) noexcept {
  switch (kind) {
    case TimestampStorage::kNativeTimestamp:
    case TimestampStorage::kFloat64:
      return TimestampEligibility::kEligible;
    case TimestampStorage::kFloat32:
      return TimestampEligibility::kExplicitOnly;
    case TimestampStorage::kOther:
      return TimestampEligibility::kIneligible;
    case TimestampStorage::kInt64:
    case TimestampStorage::kUInt64:
    case TimestampStorage::kInt32:
    case TimestampStorage::kUInt32:
    case TimestampStorage::kNarrowInt:
      break;
  }
  const uint64_t ticks_per_second = static_cast<uint64_t>(1'000'000'000 / PJ::nanosecondsPer(unit));
  const uint64_t horizon_ticks = static_cast<uint64_t>(kEligibleHorizonSeconds) * ticks_per_second;
  return *detail::maxIntegerTicks(kind) >= horizon_ticks ? TimestampEligibility::kEligible
                                                         : TimestampEligibility::kExplicitOnly;
}

/// The warning a plugin must surface when a kExplicitOnly column is selected by name;
/// every other eligibility returns an empty view.
[[nodiscard]] constexpr std::string_view explicitOnlyWarning(TimestampStorage kind, PJ::TimeUnit unit) noexcept {
  if (timestampEligibility(kind, unit) != TimestampEligibility::kExplicitOnly) {
    return {};
  }
  if (kind == TimestampStorage::kFloat32) {
    return "float32 keeps 24 significant bits, so instants near the present epoch are spaced over 100 s apart.";
  }
  return "Integer storage too narrow to reach present-day instants at the configured timestamp unit.";
}

/// Arrow-independent description of a flattened column considered for the axis.
struct TimestampCandidate {
  /// Flattened leaf path; separators are '/', with source dots already normalized.
  std::string_view name;
  /// Storage classification supplied by the importing plugin.
  TimestampStorage kind;
  /// Expanded list elements are never eligible for automatic selection.
  bool is_list_element = false;
};

/// Ordered name preferences used after native timestamp-type detection.
struct TimestampPolicy {
  /// Candidate names in priority order, with the most specific first.
  std::span<const std::string_view> names;
  /// Whether the name pass also accepts ASCII case-folded matches.
  bool case_insensitive = true;
  /// Unit of integer candidates (the configured kTimestampUnitKey); decides which widths are eligible.
  PJ::TimeUnit unit = PJ::TimeUnit::kNanoseconds;
};

/// Union of timestamp names used by official plugins, most specific first.
inline constexpr std::array<std::string_view, 11> kCanonicalTimestampNames = {"timestamp_ns", "recording_timestamp_ns",
                                                                              "timestamp",    "time",
                                                                              "ts",           "t",
                                                                              "time_stamp",   "datetime",
                                                                              "date_time",    "_timestamp",
                                                                              "_time"};

/// Default policy shared by official plugins.
inline constexpr TimestampPolicy kCanonicalPolicy{kCanonicalTimestampNames, true, PJ::TimeUnit::kNanoseconds};

namespace detail {

[[nodiscard]] constexpr char foldTimestampAscii(char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value + ('a' - 'A'));
  }
  return value;
}

[[nodiscard]] constexpr bool timestampNamesEqualFolded(std::string_view left, std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (foldTimestampAscii(left[index]) != foldTimestampAscii(right[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace detail

/// The name pass on its own: the priority index (into `policy.names`) of the first policy name that `name` matches —
/// exact-case first, then ASCII case-folded when `policy.case_insensitive` — or nullopt. Says nothing about type or
/// list-ness; pair it with timestampEligibility() for a full verdict. detectTimestampColumn's name pass is built on
/// this.
[[nodiscard]] constexpr std::optional<std::size_t> timestampNamePriority(
    std::string_view name, const TimestampPolicy& policy = kCanonicalPolicy) noexcept {
  for (std::size_t index = 0; index < policy.names.size(); ++index) {
    if (name == policy.names[index]) {
      return index;
    }
  }

  if (!policy.case_insensitive) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < policy.names.size(); ++index) {
    if (detail::timestampNamesEqualFolded(name, policy.names[index])) {
      return index;
    }
  }
  return std::nullopt;
}

/// Selects a timestamp column with a native-type pass followed by a name pass
/// over kEligible scalars. Exact-case matches win within each preferred name before
/// allocation-free ASCII case folding is considered.
[[nodiscard]] constexpr std::optional<std::size_t> detectTimestampColumn(
    std::span<const TimestampCandidate> candidates, const TimestampPolicy& policy = kCanonicalPolicy) {
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const TimestampCandidate& candidate = candidates[index];
    if (!candidate.is_list_element && candidate.kind == TimestampStorage::kNativeTimestamp) {
      return index;
    }
  }

  for (std::size_t name_index = 0; name_index < policy.names.size(); ++name_index) {
    const TimestampPolicy exact_policy{policy.names.subspan(name_index, 1), false, policy.unit};
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      const TimestampCandidate& candidate = candidates[index];
      if (!candidate.is_list_element &&
          timestampEligibility(candidate.kind, policy.unit) == TimestampEligibility::kEligible &&
          timestampNamePriority(candidate.name, exact_policy)) {
        return index;
      }
    }

    if (!policy.case_insensitive) {
      continue;
    }
    const TimestampPolicy folded_policy{policy.names.subspan(name_index, 1), true, policy.unit};
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      const TimestampCandidate& candidate = candidates[index];
      if (!candidate.is_list_element &&
          timestampEligibility(candidate.kind, policy.unit) == TimestampEligibility::kEligible &&
          timestampNamePriority(candidate.name, folded_policy)) {
        return index;
      }
    }
  }
  return std::nullopt;
}

/// Canonical JSON key for the selected timestamp column.
inline constexpr std::string_view kTimestampColumnKey = "timestamp_column";

/// Canonical JSON key for an integer timestamp column's unit.
inline constexpr std::string_view kTimestampUnitKey = "timestamp_unit";

/// Canonical JSON key for a synthesized axis interval in nanoseconds.
inline constexpr std::string_view kSyntheticIntervalKey = "synthetic_interval_ns";

/// Canonical JSON key controlling whether structured columns are flattened.
inline constexpr std::string_view kFlattenStructsKey = "flatten_structs";

/// Reads "ns", "us", "ms", or "s" from kTimestampUnitKey. A missing key
/// preserves the historical nanosecond default; malformed or unknown values
/// return nullopt so callers can reject the named config field.
[[nodiscard]] inline std::optional<PJ::TimeUnit> timestampUnitFromJson(const nlohmann::json& object) {
  const auto unit_it = object.find(kTimestampUnitKey.data());
  if (unit_it == object.end()) {
    return PJ::TimeUnit::kNanoseconds;
  }
  if (!unit_it->is_string()) {
    return std::nullopt;
  }

  const auto& value = unit_it->get_ref<const nlohmann::json::string_t&>();
  if (value == "ns") {
    return PJ::TimeUnit::kNanoseconds;
  }
  if (value == "us") {
    return PJ::TimeUnit::kMicroseconds;
  }
  if (value == "ms") {
    return PJ::TimeUnit::kMilliseconds;
  }
  if (value == "s") {
    return PJ::TimeUnit::kSeconds;
  }
  return std::nullopt;
}

/// Writes a TimeUnit using the canonical short spelling under
/// kTimestampUnitKey, converting a null JSON value to an object as needed.
inline void timestampUnitToJson(nlohmann::json& object, PJ::TimeUnit unit) {
  switch (unit) {
    case PJ::TimeUnit::kNanoseconds:
      object[kTimestampUnitKey.data()] = "ns";
      return;
    case PJ::TimeUnit::kMicroseconds:
      object[kTimestampUnitKey.data()] = "us";
      return;
    case PJ::TimeUnit::kMilliseconds:
      object[kTimestampUnitKey.data()] = "ms";
      return;
    case PJ::TimeUnit::kSeconds:
      object[kTimestampUnitKey.data()] = "s";
      return;
  }
}

}  // namespace sdk
}  // namespace PJ
