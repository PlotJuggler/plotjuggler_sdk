// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/time_format.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <locale>

namespace PJ {

// --- formatTimestamp ---

TEST(TimeFormat, TimestampShortFormatUnder24h) {
  // 2026-03-11 17:12:05 UTC in nanoseconds
  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatTimestamp(ts, /*long_format=*/false), "17:12:05");
}

TEST(TimeFormat, TimestampLongFormatOver24h) {
  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatTimestamp(ts, /*long_format=*/true), "11/03 17:12:05");
}

// --- formatDuration ---

TEST(TimeFormat, DurationSeconds) {
  constexpr int64_t ns = 42LL * 1'000'000'000;
  EXPECT_EQ(formatDuration(ns), "42s");
}

TEST(TimeFormat, DurationMinutesAndSeconds) {
  constexpr int64_t ns = (12 * 60 + 30) * 1'000'000'000LL;
  EXPECT_EQ(formatDuration(ns), "12m 30s");
}

TEST(TimeFormat, DurationHoursAndMinutes) {
  constexpr int64_t ns = (3 * 3600 + 45 * 60) * 1'000'000'000LL;
  EXPECT_EQ(formatDuration(ns), "3h 45m");
}

TEST(TimeFormat, DurationDaysAndHours) {
  constexpr int64_t ns = (2 * 86400 + 5 * 3600 + 30 * 60) * 1'000'000'000LL;
  EXPECT_EQ(formatDuration(ns), "2d 5h 30m");
}

TEST(TimeFormat, DurationZero) {
  EXPECT_EQ(formatDuration(0), "0s");
}

TEST(TimeFormat, DurationSubSecond) {
  EXPECT_EQ(formatDuration(500'000'000LL), "0s");
}

TEST(TimeFormat, DurationExactMinute) {
  constexpr int64_t ns = 60LL * 1'000'000'000;
  EXPECT_EQ(formatDuration(ns), "1m 0s");
}

TEST(TimeFormat, DurationExactHour) {
  constexpr int64_t ns = 3600LL * 1'000'000'000;
  EXPECT_EQ(formatDuration(ns), "1h 0m");
}

TEST(TimeFormat, DurationExactDay) {
  constexpr int64_t ns = 86400LL * 1'000'000'000;
  EXPECT_EQ(formatDuration(ns), "1d 0h 0m");
}

// --- needsLongFormat ---

TEST(TimeFormat, NeedsLongFormatTrue) {
  constexpr int64_t span = 25LL * 3600 * 1'000'000'000;
  EXPECT_TRUE(needsLongFormat(span));
}

TEST(TimeFormat, NeedsLongFormatFalse) {
  constexpr int64_t span = 23LL * 3600 * 1'000'000'000;
  EXPECT_FALSE(needsLongFormat(span));
}

// --- UTC date/time helpers ---

TEST(TimeFormat, FormatsIso8601Utc) {
  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatIso8601Utc(ts), "2026-03-11T17:12:05");
}

TEST(TimeFormat, FormatsDateTimeUtc) {
  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatDateTimeUtc(ts), "11/03/2026 17:12:05 UTC");
}

TEST(TimeFormat, FormatsDateOnly) {
  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatDateDDMMYYYY(ts), "11/03/2026");
  EXPECT_EQ(formatDateOnlyIso(ts), "2026-03-11");
}

TEST(TimeFormat, FormatsNegativeFractionBeforeEpoch) {
  EXPECT_EQ(formatTimestamp(-1, false), "23:59:59");
  EXPECT_EQ(formatTimestamp(-1, true), "31/12 23:59:59");
  EXPECT_EQ(formatIso8601Utc(-1), "1969-12-31T23:59:59");
  EXPECT_EQ(formatDateTimeUtc(-1), "31/12/1969 23:59:59 UTC");
  EXPECT_EQ(formatDateDDMMYYYY(-1), "31/12/1969");
  EXPECT_EQ(formatDateOnlyIso(-1), "1969-12-31");
}

TEST(TimeFormat, FormatsMinimumTimestamp) {
  constexpr int64_t ts = std::numeric_limits<int64_t>::min();
  EXPECT_EQ(formatTimestamp(ts, false), "00:12:43");
  EXPECT_EQ(formatTimestamp(ts, true), "21/09 00:12:43");
  EXPECT_EQ(formatIso8601Utc(ts), "1677-09-21T00:12:43");
  EXPECT_EQ(formatDateTimeUtc(ts), "21/09/1677 00:12:43 UTC");
  EXPECT_EQ(formatDateDDMMYYYY(ts), "21/09/1677");
  EXPECT_EQ(formatDateOnlyIso(ts), "1677-09-21");
}

TEST(TimeFormat, FormatsIndependentlyOfGlobalLocale) {
  struct GroupedPunctuation : std::numpunct<char> {
    char do_thousands_sep() const override {
      return ',';
    }
    std::string do_grouping() const override {
      return "\3";
    }
  };
  struct RestoreLocale {
    std::locale previous;
    ~RestoreLocale() {
      std::locale::global(previous);
    }
  } restore;
  std::locale::global(std::locale(std::locale::classic(), new GroupedPunctuation));

  constexpr int64_t ts = 1773249125000000000LL;
  EXPECT_EQ(formatTimestamp(ts, false), "17:12:05");
  EXPECT_EQ(formatTimestamp(ts, true), "11/03 17:12:05");
  EXPECT_EQ(formatIso8601Utc(ts), "2026-03-11T17:12:05");
  EXPECT_EQ(formatDateTimeUtc(ts), "11/03/2026 17:12:05 UTC");
  EXPECT_EQ(formatDateDDMMYYYY(ts), "11/03/2026");
  EXPECT_EQ(formatDateOnlyIso(ts), "2026-03-11");
  EXPECT_EQ(formatDuration(1000LL * 86400 * 1'000'000'000), "1000d 0h 0m");
}

TEST(TimeFormat, ParsesIso8601UtcRoundTrip) {
  constexpr int64_t timestamps[] = {
      -1'000'000'000LL,      0, 946684800000000000LL,
      951782400000000000LL,  // 2000-02-29: leap year despite being a century.
      1773249125000000000LL,
  };
  for (const int64_t ts : timestamps) {
    EXPECT_EQ(parseIso8601Utc(formatIso8601Utc(ts)), ts);
  }
}

TEST(TimeFormat, ParsesIso8601UtcWithZAndFraction) {
  EXPECT_EQ(parseIso8601Utc("1970-01-01T00:00:00Z"), 0);
  EXPECT_EQ(parseIso8601Utc("1970-01-01T00:00:00.123Z"), 123000000);
  EXPECT_EQ(parseIso8601Utc("1970-01-01T00:00:00.000000001"), 1);
}

// Qt's QDateTime(..., QTimeZone::utc()).toString(Qt::ISODate) emits a "+00:00"
// offset, not "Z". The parser must accept numeric offsets and fold them to UTC,
// otherwise the date filter parsed empty and never excluded anything.
TEST(TimeFormat, ParsesIso8601UtcWithNumericOffset) {
  // "+00:00" is just UTC.
  EXPECT_EQ(parseIso8601Utc("1970-01-01T00:00:00+00:00"), 0);
  EXPECT_EQ(parseIso8601Utc("2016-04-20T13:37:25+00:00"), parseIso8601Utc("2016-04-20T13:37:25Z"));
  // A real offset shifts back to UTC: 12:00+05:00 == 07:00Z.
  EXPECT_EQ(parseIso8601Utc("2016-04-20T12:00:00+05:00"), parseIso8601Utc("2016-04-20T07:00:00Z"));
  EXPECT_EQ(parseIso8601Utc("2016-04-20T12:00:00-05:00"), parseIso8601Utc("2016-04-20T17:00:00Z"));
  // Offset without a colon, and fractional seconds + offset together.
  EXPECT_EQ(parseIso8601Utc("2016-04-20T12:00:00+0500"), parseIso8601Utc("2016-04-20T07:00:00Z"));
  EXPECT_EQ(parseIso8601Utc("2016-04-20T12:00:00+05"), parseIso8601Utc("2016-04-20T07:00:00Z"));
  EXPECT_EQ(parseIso8601Utc("1970-01-01T00:00:00.123+00:00"), 123000000);
}

TEST(TimeFormat, RejectsMalformedIso8601Utc) {
  EXPECT_EQ(parseIso8601Utc(""), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11 17:12:05"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-02-30T17:12:05"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("1900-02-29T17:12:05"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11T25:12:05"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11T17:12:05."), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11T17:12:05+05:"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11T17:12:05+05:0"), std::nullopt);
  EXPECT_EQ(parseIso8601Utc("2026-03-11T17:12:05+05:00junk"), std::nullopt);
}

}  // namespace PJ

TEST(TimeFormat, FractionalAdditionChecksBothInt64Boundaries) {
  EXPECT_EQ(PJ::parseIso8601Utc("2262-04-11T23:47:16.854775807Z"), std::numeric_limits<int64_t>::max());
  EXPECT_FALSE(PJ::parseIso8601Utc("2262-04-11T23:47:16.854775808Z"));
  EXPECT_EQ(PJ::parseIso8601Utc("1677-09-21T00:12:43.145224192Z"), std::numeric_limits<int64_t>::min());
  EXPECT_FALSE(PJ::parseIso8601Utc("1677-09-21T00:12:43.145224191Z"));
  EXPECT_FALSE(PJ::parseIso8601Utc("9999-01-01T00:00:00Z"));
  EXPECT_FALSE(PJ::parseIso8601Utc("1970-01-01T00:00:00.1234567890Z"));
}
