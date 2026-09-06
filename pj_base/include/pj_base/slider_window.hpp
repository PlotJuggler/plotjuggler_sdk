// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace PJ {

struct SliderWindow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

/// Map ordered handles [0, steps] to a half-open nanosecond window. The upper
/// endpoint at steps extends max_ns by one tick so the last frame survives.
/// Nullopt for invalid handles, degenerate ranges, or unrepresentable max-min /
/// max+1. No provider-specific zero/unbounded sentinel is imposed.
[[nodiscard]] inline std::optional<SliderWindow> sliderToWindow(
    std::int64_t min_ns, std::int64_t max_ns, int lower, int upper, int steps) {
  if (max_ns <= min_ns || steps <= 0 || lower < 0 || upper < lower || upper > steps) {
    return std::nullopt;
  }
  const auto span_unsigned = static_cast<std::uint64_t>(max_ns) - static_cast<std::uint64_t>(min_ns);
  if (span_unsigned > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      (upper == steps && max_ns == std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  const auto span = static_cast<std::int64_t>(span_unsigned);
  const auto offset = [span, steps](int position) {
    // span*position can overflow before division. Both terms here fit:
    // quotient*position <= span, remainder*position < INT_MAX squared.
    return (span / steps) * position + (span % steps) * position / steps;
  };
  return SliderWindow{min_ns + offset(lower), upper == steps ? max_ns + 1 : min_ns + offset(upper)};
}

}  // namespace PJ
