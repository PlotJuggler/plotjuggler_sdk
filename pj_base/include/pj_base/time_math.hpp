// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

/** @file time_math.hpp @brief C++17 checked arithmetic for nanosecond timestamps. */

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace PJ {

/// Units a tick count can be expressed in. Nanoseconds is the spine's native unit.
enum class TimeUnit : uint8_t { kSeconds, kMilliseconds, kMicroseconds, kNanoseconds };

/// Returns the integral nanosecond scale for a time unit.
[[nodiscard]] constexpr int64_t nanosecondsPer(TimeUnit unit) noexcept {
  switch (unit) {
    case TimeUnit::kSeconds:
      return 1'000'000'000;
    case TimeUnit::kMilliseconds:
      return 1'000'000;
    case TimeUnit::kMicroseconds:
      return 1'000;
    case TimeUnit::kNanoseconds:
      return 1;
  }
  return 0;
}

/// Converts ticks in the supplied unit to nanoseconds, returning nullopt on overflow.
[[nodiscard]] constexpr std::optional<int64_t> scaleToNanoseconds(int64_t ticks, TimeUnit unit) noexcept {
  const int64_t scale = nanosecondsPer(unit);
  if (scale == 0 || ticks > std::numeric_limits<int64_t>::max() / scale ||
      ticks < std::numeric_limits<int64_t>::min() / scale) {
    return std::nullopt;
  }
  return ticks * scale;
}

/// Checked uint64 -> int64 tick conversion; values above INT64_MAX return nullopt.
[[nodiscard]] constexpr std::optional<int64_t> toSignedTicks(uint64_t ticks) noexcept {
  if (ticks > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<int64_t>(ticks);
}

/// Converts floating seconds to nanoseconds using an integer split and half-away-from-zero rounding.
/// Non-finite inputs and either whole-second or final-addition overflow return nullopt.
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

/// Combines seconds and nanoseconds-of-second; nanos must be in [0, 1e9).
/// Returns nullopt when either the nanos range or the signed timestamp range would be exceeded.
[[nodiscard]] constexpr std::optional<int64_t> combineSecondsAndNanos(int64_t seconds, int64_t nanos) noexcept {
  constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
  if (nanos < 0 || nanos >= kNanosecondsPerSecond) {
    return std::nullopt;
  }
  const int64_t positive_room = (std::numeric_limits<int64_t>::max() - nanos) / kNanosecondsPerSecond;
  const int64_t negative_room = std::numeric_limits<int64_t>::min() / kNanosecondsPerSecond;
  if (seconds > positive_room || seconds < negative_room) {
    return std::nullopt;
  }
  return seconds * kNanosecondsPerSecond + nanos;
}

/// Computes anchor + row * interval for a non-negative synthetic row, returning nullopt on overflow.
[[nodiscard]] constexpr std::optional<int64_t> syntheticInstant(
    int64_t anchor_ns, int64_t interval_ns, int64_t row) noexcept {
  if (row < 0) {
    return std::nullopt;
  }
  if ((interval_ns > 0 && row > std::numeric_limits<int64_t>::max() / interval_ns) ||
      (interval_ns < 0 && row > 0 && interval_ns < std::numeric_limits<int64_t>::min() / row)) {
    return std::nullopt;
  }
  const int64_t offset = interval_ns * row;
  if ((offset > 0 && anchor_ns > std::numeric_limits<int64_t>::max() - offset) ||
      (offset < 0 && anchor_ns < std::numeric_limits<int64_t>::min() - offset)) {
    return std::nullopt;
  }
  return anchor_ns + offset;
}

/// Fits an interval across rows over [first, last], using fallback for insufficient rows,
/// a non-positive span, or an interval that cannot be represented by int64_t.
[[nodiscard]] constexpr int64_t fitSyntheticInterval(
    int64_t first_ns, int64_t last_ns, int64_t rows, int64_t fallback_ns) noexcept {
  if (rows < 2 || last_ns <= first_ns) {
    return fallback_ns;
  }
  const uint64_t span = static_cast<uint64_t>(last_ns) - static_cast<uint64_t>(first_ns);
  const uint64_t interval = span / static_cast<uint64_t>(rows - 1);
  if (interval > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return fallback_ns;
  }
  return static_cast<int64_t>(interval);
}

/// Default cadence for a synthesized axis when nothing better is known (approximately 30 fps).
inline constexpr int64_t kDefaultSyntheticIntervalNs = 33'333'333;

}  // namespace PJ
