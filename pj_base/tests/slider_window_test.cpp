// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Hermetic unit tests for the slider->nanoseconds window mapping. The bug this
// pins: the naive `union_min + span*pos/steps` overflowed int64 for multi-hour
// aggregate spans (span*pos before the divide), wrapping the window NEGATIVE so
// the server matched zero chunks ("[Nx] no messages in the selected time
// range") — single-file fetches never overflowed, which masked it.

#include "pj_base/slider_window.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using PJ::sliderToWindow;
using PJ::SliderWindow;

constexpr int kSteps = 1'000'000;

// Real S3-use-case staging numbers: 34 contiguous files, ~7.35h union. union_min is
// the first file's start; span ~2.65e13 ns. The persisted middle band that
// triggered the report.
constexpr std::int64_t kAggMin = 1779209029609533992LL;
constexpr std::int64_t kAggMax = 1779235502659755586LL;  // ~7.35h later

}  // namespace

// THE REGRESSION: a multi-hour aggregate span must produce a window INSIDE the
// union, not a negative-wrapped one before it. With the old int64 multiply,
// span*624146 (~1.65e19) overflowed INT64_MAX and start landed below union_min.
TEST(SliderWindow, MultiHourAggregateDoesNotOverflow) {
  const auto window = sliderToWindow(kAggMin, kAggMax, 502278, 624146, kSteps);
  ASSERT_TRUE(window.has_value());
  // Both bounds strictly inside the union and correctly ordered.
  EXPECT_GE(window->start_ns, kAggMin) << "start wrapped below union_min (the overflow bug)";
  EXPECT_LE(window->end_ns, kAggMax + 1);
  EXPECT_LT(window->start_ns, window->end_ns);
  // Exact proportional positions, pinned as literals computed independently
  // in arbitrary-precision arithmetic (python3: span * pos // kSteps) — a
  // ground-truth oracle that cannot itself overflow and, unlike the previous
  // __int128 recomputation, is portable to MSVC and independent of however
  // sliderToWindow performs the widening.
  EXPECT_EQ(window->start_ns, kAggMin + 13296830719201LL);
  EXPECT_EQ(window->end_ns, kAggMin + 16523048403607LL);
}

// Single ~13-min file: the old code worked here (no overflow); the new code
// must match it exactly.
TEST(SliderWindow, SingleFileUnchanged) {
  constexpr std::int64_t fmin = 1779209029609533992LL;
  constexpr std::int64_t fmax = 1779209780845898011LL;  // ~751s span
  const auto window = sliderToWindow(fmin, fmax, 502278, 624146, kSteps);
  ASSERT_TRUE(window.has_value());
  const std::int64_t span = fmax - fmin;
  EXPECT_EQ(window->start_ns, fmin + span * 502278 / kSteps);  // no overflow at this span
  EXPECT_EQ(window->end_ns, fmin + span * 624146 / kSteps);
  EXPECT_GT(window->start_ns, fmin);
  EXPECT_LT(window->end_ns, fmax);
}

// Full-range upper handle extends one tick past union_max (half-open [start,end)
// must include the final frame).
TEST(SliderWindow, FullUpperHandleExtendsPastMax) {
  const auto window = sliderToWindow(kAggMin, kAggMax, 0, kSteps, kSteps);
  ASSERT_TRUE(window.has_value());
  EXPECT_EQ(window->start_ns, kAggMin);
  EXPECT_EQ(window->end_ns, kAggMax + 1);
}

// Degenerate unions have no representable slider window.
TEST(SliderWindow, DegenerateUnionIsRejected) {
  EXPECT_FALSE(sliderToWindow(100, 100, 0, kSteps, kSteps).has_value());
  EXPECT_FALSE(sliderToWindow(200, 100, 0, kSteps, kSteps).has_value());
  const auto window = sliderToWindow(100, 100, 0, kSteps, kSteps);
  EXPECT_FALSE(window);
}

// Worst case: maximum plausible span (a full day) at the maximum sub-range
// handle still stays ordered and in-union — proves the checked path holds well
// past the int64 overflow threshold (~2.56h).
TEST(SliderWindow, FullDaySpanStaysInUnion) {
  constexpr std::int64_t dmin = 1779000000000000000LL;
  constexpr std::int64_t dmax = dmin + 86'400LL * 1'000'000'000LL;  // +24h
  const auto window = sliderToWindow(dmin, dmax, 999998, 999999, kSteps);
  ASSERT_TRUE(window.has_value());
  EXPECT_GE(window->start_ns, dmin);
  EXPECT_LE(window->end_ns, dmax);
  EXPECT_LT(window->start_ns, window->end_ns);
}

TEST(SliderWindow, RejectsInvalidHandlesAndUnrepresentableBounds) {
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  EXPECT_FALSE(sliderToWindow(minimum, maximum, 0, 10, 10));
  EXPECT_FALSE(sliderToWindow(0, maximum, 0, 10, 10));
  EXPECT_TRUE(sliderToWindow(0, maximum, 0, 9, 10));
  EXPECT_FALSE(sliderToWindow(0, 10, -1, 5, 10));
  EXPECT_FALSE(sliderToWindow(0, 10, 6, 5, 10));
  EXPECT_FALSE(sliderToWindow(0, 10, 0, 11, 10));
  EXPECT_FALSE(sliderToWindow(0, 10, 0, 0, 0));
  const auto window = sliderToWindow(-10, 10, 5, 10, 10);
  ASSERT_TRUE(window);
  EXPECT_EQ(window->start_ns, 0);
  EXPECT_EQ(window->end_ns, 11);
}
