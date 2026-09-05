// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Compile-fence + behavior tests for the absolute time spine (pj_base/time.hpp).
// The static_asserts are the real point: they prove the type system rejects the
// instant-vs-duration and raw-vs-Timepoint mistakes the vocabulary prevents.
// Display-relative time lives in pj_runtime and is tested there.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "pj_base/time.hpp"

namespace {

template <class A, class B>
concept Addable = requires(A a, B b) { a + b; };

// An absolute instant plus another absolute instant is meaningless and must not
// compile — this is the instant-vs-duration guarantee std::chrono gives us.
static_assert(!Addable<PJ::Timepoint, PJ::Timepoint>, "Timepoint + Timepoint must be ill-formed");
// ...but instant - instant (a span) and instant + duration (a shifted instant) do.
static_assert(Addable<PJ::Timepoint, PJ::Duration>, "Timepoint + Duration must compile");

// A raw int64 timestamp must go through fromRaw(), never implicitly become a
// Timepoint.
static_assert(!std::is_convertible_v<PJ::Timestamp, PJ::Timepoint>);

TEST(TimeSpine, RawRoundTrip) {
  const PJ::Timestamp ts = 1'717'500'000'123'456'789LL;
  EXPECT_EQ(PJ::toRaw(PJ::fromRaw(ts)), ts);
}

TEST(TimeSpine, FromRawRangeLiftsBothEnds) {
  const PJ::Range<PJ::Timestamp> raw{1'000'000'000LL, 5'000'000'000LL};
  const PJ::Range<PJ::Timepoint> lifted = PJ::fromRawRange(raw);
  EXPECT_EQ(PJ::toRaw(lifted.min), 1'000'000'000LL);
  EXPECT_EQ(PJ::toRaw(lifted.max), 5'000'000'000LL);
}

TEST(TimeMath, ScalesTicksAndRejectsOverflow) {
  EXPECT_EQ(PJ::scaleToNanoseconds(1, PJ::TimeUnit::kSeconds), std::optional<int64_t>{1'000'000'000});
  EXPECT_FALSE(PJ::scaleToNanoseconds(9'223'372'037, PJ::TimeUnit::kSeconds));
}

TEST(TimeMath, ConvertsUnsignedTicksWithinSignedRange) {
  EXPECT_FALSE(PJ::toSignedTicks(std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(
      PJ::toSignedTicks(static_cast<uint64_t>(std::numeric_limits<int64_t>::max())),
      std::optional<int64_t>{std::numeric_limits<int64_t>::max()});
}

TEST(TimeMath, ConvertsSecondsUsingIntegerSplitAndStableRounding) {
  struct ConversionCase {
    double seconds;
    int64_t nanoseconds;
  };
  const ConversionCase cases[] = {
      {1.5, 1'500'000'000},
      {1.7e9 + 0.125, 1'700'000'000'125'000'000},
      {-1.6e-9, -2},
      {1.6e-9, 2},
      {2.4e-9, 2},
      // Epoch-sized values expose rounding lost by multiplying the whole double by 1e9.
      {1620132785.1132183, 1620132785113218307LL},
      {1702807114.4990616, 1702807114499061584LL},
      {-1529583318.5568106, -1529583318556810617LL},
  };

  for (const ConversionCase& test_case : cases) {
    const auto converted = PJ::secondsToNanoseconds(test_case.seconds);
    ASSERT_TRUE(converted);
    EXPECT_EQ(*converted, test_case.nanoseconds);
  }
}

TEST(TimeMath, RejectsNonFiniteAndOverflowingSeconds) {
  EXPECT_FALSE(PJ::secondsToNanoseconds(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_FALSE(PJ::secondsToNanoseconds(std::numeric_limits<double>::infinity()));
  EXPECT_FALSE(PJ::secondsToNanoseconds(-std::numeric_limits<double>::infinity()));
  EXPECT_FALSE(PJ::secondsToNanoseconds(9.3e9));
  EXPECT_FALSE(PJ::secondsToNanoseconds(9223372036.0 + 0.999999999));
}

TEST(TimeMath, CombinesSecondsAndNanosecondsWithChecks) {
  EXPECT_EQ(PJ::combineSecondsAndNanos(1, 5), std::optional<int64_t>{1'000'000'005});
  EXPECT_EQ(PJ::combineSecondsAndNanos(-1, 0), std::optional<int64_t>{-1'000'000'000});
  EXPECT_EQ(PJ::combineSecondsAndNanos(-1, 999'999'999), std::optional<int64_t>{-1});
  EXPECT_FALSE(PJ::combineSecondsAndNanos(0, 1'000'000'000));
  EXPECT_FALSE(PJ::combineSecondsAndNanos(0, -1));
}

TEST(TimeMath, CombinesSecondsAndNanosecondsAtBothSignedBoundaries) {
  EXPECT_EQ(
      PJ::combineSecondsAndNanos(-9'223'372'037, 145'224'192),
      std::optional<int64_t>{std::numeric_limits<int64_t>::min()});
  EXPECT_FALSE(PJ::combineSecondsAndNanos(-9'223'372'037, 145'224'191));
  EXPECT_EQ(
      PJ::combineSecondsAndNanos(-9'223'372'037, 145'224'193),
      std::optional<int64_t>{std::numeric_limits<int64_t>::min() + 1});
  EXPECT_EQ(
      PJ::combineSecondsAndNanos(9'223'372'036, 854'775'807),
      std::optional<int64_t>{std::numeric_limits<int64_t>::max()});
  EXPECT_FALSE(PJ::combineSecondsAndNanos(9'223'372'036, 854'775'808));
  EXPECT_FALSE(PJ::combineSecondsAndNanos(std::numeric_limits<int64_t>::min(), 999'999'999));
  EXPECT_FALSE(PJ::combineSecondsAndNanos(std::numeric_limits<int64_t>::max(), 0));
}

TEST(TimeMath, ComputesSyntheticInstantsWithChecks) {
  EXPECT_EQ(PJ::syntheticInstant(10, 3, 4), std::optional<int64_t>{22});
  EXPECT_FALSE(PJ::syntheticInstant(std::numeric_limits<int64_t>::max(), 1, 1));
  EXPECT_FALSE(PJ::syntheticInstant(std::numeric_limits<int64_t>::min(), -1, 1));
  EXPECT_FALSE(PJ::syntheticInstant(10, 3, -1));
  EXPECT_FALSE(PJ::syntheticInstant(0, std::numeric_limits<int64_t>::max(), 2));
  EXPECT_FALSE(PJ::syntheticInstant(0, std::numeric_limits<int64_t>::min(), 2));
}

TEST(TimeMath, FitsSyntheticIntervalsOrUsesFallback) {
  EXPECT_EQ(PJ::fitSyntheticInterval(1000, 4000, 5, 7), 750);
  EXPECT_EQ(PJ::fitSyntheticInterval(1000, 1000, 5, 7), 7);
  EXPECT_EQ(PJ::fitSyntheticInterval(1000, 4000, 1, 7), 7);
  EXPECT_EQ(PJ::fitSyntheticInterval(4000, 1000, 5, 7), 7);
  // The span exceeds INT64_MAX although the fitted interval is representable.
  EXPECT_EQ(
      PJ::fitSyntheticInterval(-9'000'000'000'000'000'000LL, 9'000'000'000'000'000'000LL, 3, 7),
      9'000'000'000'000'000'000LL);
  EXPECT_EQ(
      PJ::fitSyntheticInterval(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 2, 7), 7);
  EXPECT_EQ(
      PJ::fitSyntheticInterval(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 3, 7),
      std::numeric_limits<int64_t>::max());
  EXPECT_EQ(PJ::kDefaultSyntheticIntervalNs, 33'333'333);
}

}  // namespace
