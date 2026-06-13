// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Compile-fence + behavior tests for the absolute time spine (pj_base/time.hpp).
// The static_asserts are the real point: they prove the type system rejects the
// instant-vs-duration and raw-vs-Timepoint mistakes the vocabulary prevents.
// Display-relative time lives in pj_runtime and is tested there.

#include <gtest/gtest.h>

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

}  // namespace
