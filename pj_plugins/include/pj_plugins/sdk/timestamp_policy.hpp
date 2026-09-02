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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string_view>

namespace PJ {
namespace sdk {

/// How a candidate column's storage relates to an epoch-nanosecond axis.
enum class TimeKind : uint8_t {
  /// A native timestamp whose unit is known to the caller.
  kNativeTimestamp,
  /// A signed 64-bit integer containing nanoseconds.
  kInt64,
  /// An unsigned 64-bit integer containing nanoseconds.
  kUInt64,
  /// Double-precision seconds, with about 238 ns resolution at the present epoch.
  kFloat64,
  /// Unsigned 32-bit nanoseconds, which end about 4.3 seconds after the epoch.
  kUInt32,
  /// Signed 8/16/32-bit or unsigned 8/16-bit nanoseconds.
  kNarrowInt,
  /// Single-precision seconds, whose spacing reaches one second at 2^23 seconds.
  kFloat32,
  /// Storage that cannot serve as a timestamp axis.
  kOther,
};

/// Whether a TimeKind can be auto-selected or must be handled explicitly.
enum class AxisSupport : uint8_t {
  /// Safe enough for automatic timestamp-axis selection.
  kPlausible,
  /// Available only after surfacing explicitAxisWarning().
  kAcceptedWithWarning,
  /// Cannot serve as a timestamp axis.
  kUnsupported,
};

/// Classifies timestamp-axis support without inspecting a column name.
[[nodiscard]] constexpr AxisSupport axisSupport(TimeKind kind) noexcept {
  switch (kind) {
    case TimeKind::kNativeTimestamp:
    case TimeKind::kInt64:
    case TimeKind::kUInt64:
    case TimeKind::kFloat64:
      return AxisSupport::kPlausible;
    case TimeKind::kUInt32:
    case TimeKind::kNarrowInt:
    case TimeKind::kFloat32:
      return AxisSupport::kAcceptedWithWarning;
    case TimeKind::kOther:
      return AxisSupport::kUnsupported;
  }
  return AxisSupport::kUnsupported;
}

/// Returns the warning a plugin must surface for an explicitly selected lossy
/// or short-range axis; all other kinds return an empty view without allocating.
[[nodiscard]] constexpr std::string_view explicitAxisWarning(TimeKind kind) noexcept {
  switch (kind) {
    case TimeKind::kUInt32:
      return "uint32 can express at most 4294967295 ns since the Unix epoch.";
    case TimeKind::kNarrowInt:
      return "Narrow integers can express at most 2147483647 ns since the Unix epoch.";
    case TimeKind::kFloat32:
      return "float32 seconds reach 1-second spacing at 2^23 (8388608) seconds from the Unix epoch.";
    case TimeKind::kNativeTimestamp:
    case TimeKind::kInt64:
    case TimeKind::kUInt64:
    case TimeKind::kFloat64:
    case TimeKind::kOther:
      return {};
  }
  return {};
}

/// Arrow-independent description of a flattened column considered for the axis.
struct TimestampCandidate {
  /// Flattened leaf path; separators are '/', with source dots already normalized.
  std::string_view name;
  /// Storage classification supplied by the importing plugin.
  TimeKind kind;
  /// Expanded list elements are never eligible for automatic selection.
  bool is_list_element = false;
};

/// Ordered name preferences used after native timestamp-type detection.
struct TimestampPolicy {
  /// Candidate names in priority order, with the most specific first.
  std::span<const std::string_view> names;
  /// Whether the name pass also accepts ASCII case-folded matches.
  bool case_insensitive = true;
};

/// Union of timestamp names used by official plugins, most specific first.
inline constexpr std::array<std::string_view, 11> kCanonicalTimestampNames = {"timestamp_ns", "recording_timestamp_ns",
                                                                              "timestamp",    "time",
                                                                              "ts",           "t",
                                                                              "time_stamp",   "datetime",
                                                                              "date_time",    "_timestamp",
                                                                              "_time"};

/// Default policy shared by official plugins.
inline constexpr TimestampPolicy kCanonicalPolicy{kCanonicalTimestampNames, true};

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

/// Selects a timestamp column with a native-type pass followed by a plausible
/// scalar name pass. Exact-case matches win within each preferred name before
/// allocation-free ASCII case folding is considered.
[[nodiscard]] constexpr std::optional<std::size_t> detectTimestampColumn(
    std::span<const TimestampCandidate> candidates, const TimestampPolicy& policy = kCanonicalPolicy) {
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const TimestampCandidate& candidate = candidates[index];
    if (!candidate.is_list_element && candidate.kind == TimeKind::kNativeTimestamp) {
      return index;
    }
  }

  for (const std::string_view preferred_name : policy.names) {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      const TimestampCandidate& candidate = candidates[index];
      if (!candidate.is_list_element && axisSupport(candidate.kind) == AxisSupport::kPlausible &&
          candidate.name == preferred_name) {
        return index;
      }
    }

    if (!policy.case_insensitive) {
      continue;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      const TimestampCandidate& candidate = candidates[index];
      if (!candidate.is_list_element && axisSupport(candidate.kind) == AxisSupport::kPlausible &&
          detail::timestampNamesEqualFolded(candidate.name, preferred_name)) {
        return index;
      }
    }
  }
  return std::nullopt;
}

/// Converts seconds to nanoseconds without platform-dependent long-double
/// arithmetic. Fractional nanoseconds round halfway away from zero; non-finite
/// input and either whole-second or final-addition overflow return nullopt.
[[nodiscard]] inline std::optional<int64_t> secondsToNanoseconds(double seconds) noexcept {
  if (!std::isfinite(seconds)) {
    return std::nullopt;
  }

  constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
  constexpr int64_t kMaximumWholeSeconds = std::numeric_limits<int64_t>::max() / kNanosecondsPerSecond;
  constexpr int64_t kMinimumWholeSeconds = std::numeric_limits<int64_t>::min() / kNanosecondsPerSecond;

  double whole_seconds = 0.0;
  const double fractional_seconds = std::modf(seconds, &whole_seconds);
  if (whole_seconds > static_cast<double>(kMaximumWholeSeconds) ||
      whole_seconds < static_cast<double>(kMinimumWholeSeconds)) {
    return std::nullopt;
  }

  const int64_t whole_nanoseconds = static_cast<int64_t>(whole_seconds) * kNanosecondsPerSecond;
  const int64_t fractional_nanoseconds =
      static_cast<int64_t>(std::llround(fractional_seconds * static_cast<double>(kNanosecondsPerSecond)));

  if ((fractional_nanoseconds > 0 &&
       whole_nanoseconds > std::numeric_limits<int64_t>::max() - fractional_nanoseconds) ||
      (fractional_nanoseconds < 0 &&
       whole_nanoseconds < std::numeric_limits<int64_t>::min() - fractional_nanoseconds)) {
    return std::nullopt;
  }
  return whole_nanoseconds + fractional_nanoseconds;
}

/// Canonical JSON key for the selected timestamp column.
inline constexpr std::string_view kTimestampColumnKey = "timestamp_column";

/// Canonical JSON key for an integer timestamp column's unit.
inline constexpr std::string_view kTimestampUnitKey = "timestamp_unit";

/// Units accepted by the shared timestamp-axis configuration contract.
enum class TimestampUnit : uint8_t {
  /// Nanoseconds ("ns"); the compatibility default for integer columns.
  kNanoseconds,
  /// Microseconds ("us").
  kMicroseconds,
  /// Milliseconds ("ms").
  kMilliseconds,
  /// Seconds ("s").
  kSeconds,
};

/// Returns the integral nanosecond scale for a configured timestamp unit.
[[nodiscard]] constexpr int64_t nanosecondsPer(TimestampUnit unit) noexcept {
  switch (unit) {
    case TimestampUnit::kNanoseconds:
      return 1;
    case TimestampUnit::kMicroseconds:
      return 1'000;
    case TimestampUnit::kMilliseconds:
      return 1'000'000;
    case TimestampUnit::kSeconds:
      return 1'000'000'000;
  }
  return 0;
}

/// Reads "ns", "us", "ms", or "s" from kTimestampUnitKey. A missing key
/// preserves the historical nanosecond default; malformed or unknown values
/// return nullopt so callers can reject the named config field.
[[nodiscard]] inline std::optional<TimestampUnit> timestampUnitFromJson(const nlohmann::json& object) {
  const auto unit_it = object.find(kTimestampUnitKey.data());
  if (unit_it == object.end()) {
    return TimestampUnit::kNanoseconds;
  }
  if (!unit_it->is_string()) {
    return std::nullopt;
  }

  const auto& value = unit_it->get_ref<const nlohmann::json::string_t&>();
  if (value == "ns") {
    return TimestampUnit::kNanoseconds;
  }
  if (value == "us") {
    return TimestampUnit::kMicroseconds;
  }
  if (value == "ms") {
    return TimestampUnit::kMilliseconds;
  }
  if (value == "s") {
    return TimestampUnit::kSeconds;
  }
  return std::nullopt;
}

/// Writes a TimestampUnit using the canonical short spelling under
/// kTimestampUnitKey, converting a null JSON value to an object as needed.
inline void timestampUnitToJson(nlohmann::json& object, TimestampUnit unit) {
  switch (unit) {
    case TimestampUnit::kNanoseconds:
      object[kTimestampUnitKey.data()] = "ns";
      return;
    case TimestampUnit::kMicroseconds:
      object[kTimestampUnitKey.data()] = "us";
      return;
    case TimestampUnit::kMilliseconds:
      object[kTimestampUnitKey.data()] = "ms";
      return;
    case TimestampUnit::kSeconds:
      object[kTimestampUnitKey.data()] = "s";
      return;
  }
}

}  // namespace sdk
}  // namespace PJ
