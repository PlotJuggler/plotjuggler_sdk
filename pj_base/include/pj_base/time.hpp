#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// The absolute time spine: a chrono vocabulary that sits one lossless step above
// the int64-ns PJ::Timestamp. Timestamp stays the storage/ABI/wire currency;
// these types are how layered code names absolute time without re-deriving the
// epoch or hand-rolling 1e9 conversions. Two concepts, kept un-mixable by the
// type system:
//   * Timepoint — an absolute instant (sys_time<nanoseconds>, Unix epoch).
//   * Duration  — a span (nanoseconds); Timepoint + Timepoint won't compile.
//
// fromRaw()/toRaw() are the only sanctioned crossing between the int64 spine and
// the chrono world: lift a Timestamp into a Timepoint to compute with it, lower
// it back immediately before a storage/ABI/wire boundary. Display-relative time
// (the Qwt-axis / PlaybackEngine coordinate) is a separate, app-level vocabulary
// that builds on this one — it lives in pj_runtime, not here.

#include <chrono>

#include "pj_base/types.hpp"  // PJ::Timestamp, PJ::Range

namespace PJ {

/// An ABSOLUTE wall-clock instant, nanosecond precision, Unix epoch. Lossless
/// mirror of PJ::Timestamp (C++20 guarantees system_clock's epoch == Unix epoch).
using Timepoint = std::chrono::sys_time<std::chrono::nanoseconds>;

/// A length of time (a span, not a point): retention windows, lifetimes, deltas.
using Duration = std::chrono::nanoseconds;

/// Lift an int64-ns PJ::Timestamp out of the spine into a Timepoint.
[[nodiscard]] constexpr Timepoint fromRaw(Timestamp ns) noexcept {
  return Timepoint{Duration{ns}};
}

/// Lower a Timepoint back to the int64-ns spine, immediately before crossing a
/// storage/ABI boundary (DataWriter, the C-ABI trampolines, the codecs).
[[nodiscard]] constexpr Timestamp toRaw(Timepoint t) noexcept {
  return t.time_since_epoch().count();
}

/// Lift an int64 interval into an absolute Timepoint interval (reuses PJ::Range,
/// never std::pair).
[[nodiscard]] constexpr Range<Timepoint> fromRawRange(const Range<Timestamp>& r) noexcept {
  return {fromRaw(r.min), fromRaw(r.max)};
}

}  // namespace PJ
