// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace PJ {

/// Timestamp formatters use UTC Unix-epoch nanoseconds, independent of locale.
/// They floor fractional timestamps to whole seconds, including before the epoch.
/// Format as "hh:mm:ss" (short) or "dd/MM hh:mm:ss" (long).
std::string formatTimestamp(int64_t ts_ns, bool long_format);

/// Format a duration as "42s", "12m 30s", "3h 45m" or "2d 5h 30m".
/// Subseconds truncate toward zero; hours/days omit seconds. Negative durations
/// use signed whole seconds (e.g. -90 seconds becomes "-90s").
std::string formatDuration(int64_t duration_ns);

/// True only when the span exceeds 24 hours (exactly 24 hours uses short format).
bool needsLongFormat(int64_t span_ns);

/// UTC "YYYY-MM-DDTHH:MM:SS", with no timezone suffix or fractional seconds.
/// This is whole-second display formatting, not a lossless nanosecond serializer.
std::string formatIso8601Utc(int64_t timestamp_ns);

/// UTC "dd/MM/YYYY hh:mm:ss UTC", floored to whole seconds.
std::string formatDateTimeUtc(int64_t timestamp_ns);

/// UTC calendar date "dd/MM/YYYY".
std::string formatDateDDMMYYYY(int64_t timestamp_ns);

/// UTC calendar date "YYYY-MM-DD".
std::string formatDateOnlyIso(int64_t timestamp_ns);

/// Parse the entire "YYYY-MM-DDTHH:MM:SS[.fraction][zone]" to Unix nanoseconds.
/// Fraction, when present, has 1..9 digits. Zone is absent (UTC), uppercase Z,
/// or a numeric offset +/-HH, +/-HHMM or +/-HH:MM (HH <= 23, MM <= 59).
/// Numeric offsets are subtracted to obtain UTC; local time is never inferred.
/// Invalid calendar dates, leap seconds, whitespace, trailing junk, excess
/// precision and int64 nanosecond overflow return nullopt. No locale dependence.
std::optional<int64_t> parseIso8601Utc(std::string_view text);

}  // namespace PJ
