// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/time_format.hpp"

#include <fmt/format.h>

#include <cctype>
#include <chrono>

#include "pj_base/time_math.hpp"

namespace PJ {

namespace {
struct UtcTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

UtcTime utcFromNanoseconds(int64_t ts_ns) {
  using namespace std::chrono;
  const auto whole = floor<seconds>(sys_time<nanoseconds>{nanoseconds{ts_ns}});
  const auto midnight = floor<days>(whole);
  const year_month_day date{midnight};
  // Subtract in seconds: midnight can be outside the int64 nanosecond range.
  const hh_mm_ss time{whole - midnight};
  return {
      static_cast<int>(date.year()),
      static_cast<int>(static_cast<unsigned>(date.month())),
      static_cast<int>(static_cast<unsigned>(date.day())),
      static_cast<int>(time.hours().count()),
      static_cast<int>(time.minutes().count()),
      static_cast<int>(time.seconds().count())};
}

std::string formatDate(const UtcTime& utc, char separator, bool day_first) {
  return day_first ? fmt::format("{:02}{}{:02}{}{:04}", utc.day, separator, utc.month, separator, utc.year)
                   : fmt::format("{:04}{}{:02}{}{:02}", utc.year, separator, utc.month, separator, utc.day);
}

std::string formatTime(const UtcTime& utc) {
  return fmt::format("{:02}:{:02}:{:02}", utc.hour, utc.minute, utc.second);
}

bool isDigit(char character) {
  return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool parseFixedDigits(std::string_view text, std::size_t offset, std::size_t count, int& value) {
  if (offset + count > text.size()) {
    return false;
  }
  int parsed = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const char character = text[offset + index];
    if (!isDigit(character)) {
      return false;
    }
    parsed = parsed * 10 + (character - '0');
  }
  value = parsed;
  return true;
}
}  // namespace

std::string formatTimestamp(int64_t ts_ns, bool long_format) {
  const auto utc = utcFromNanoseconds(ts_ns);
  return long_format ? fmt::format("{:02}/{:02} {}", utc.day, utc.month, formatTime(utc)) : formatTime(utc);
}

std::string formatDuration(int64_t duration_ns) {
  const int64_t total_secs = duration_ns / 1'000'000'000LL;
  if (total_secs < 60) {
    return std::to_string(total_secs) + "s";
  }

  const int64_t days = total_secs / 86400;
  const int64_t hours = (total_secs % 86400) / 3600;
  const int64_t minutes = (total_secs % 3600) / 60;
  const int64_t secs = total_secs % 60;

  std::string result;
  if (days > 0) {
    result = std::to_string(days) + "d " + std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  } else if (hours > 0) {
    result = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  } else {
    result = std::to_string(minutes) + "m " + std::to_string(secs) + "s";
  }
  return result;
}

bool needsLongFormat(int64_t span_ns) {
  return span_ns > 24LL * 3600 * 1'000'000'000;
}

std::string formatIso8601Utc(int64_t timestamp_ns) {
  const auto utc = utcFromNanoseconds(timestamp_ns);
  return formatDate(utc, '-', false) + "T" + formatTime(utc);
}

std::string formatDateTimeUtc(int64_t timestamp_ns) {
  const auto utc = utcFromNanoseconds(timestamp_ns);
  return formatDate(utc, '/', true) + " " + formatTime(utc) + " UTC";
}

std::string formatDateDDMMYYYY(int64_t timestamp_ns) {
  return formatDate(utcFromNanoseconds(timestamp_ns), '/', true);
}

std::string formatDateOnlyIso(int64_t timestamp_ns) {
  return formatDate(utcFromNanoseconds(timestamp_ns), '-', false);
}

std::optional<int64_t> parseIso8601Utc(std::string_view text) {
  if (text.empty() || text.size() < 19) {
    return std::nullopt;
  }
  if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') {
    return std::nullopt;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parseFixedDigits(text, 0, 4, year) || !parseFixedDigits(text, 5, 2, month) ||
      !parseFixedDigits(text, 8, 2, day) || !parseFixedDigits(text, 11, 2, hour) ||
      !parseFixedDigits(text, 14, 2, minute) || !parseFixedDigits(text, 17, 2, second)) {
    return std::nullopt;
  }
  const std::chrono::year_month_day date{
      std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
      std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || hour > 23 || minute > 59 || second > 59) {
    return std::nullopt;
  }

  std::size_t pos = 19;
  int64_t fractional_ns = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    const std::size_t fraction_start = pos;
    while (pos < text.size() && isDigit(text[pos])) {
      if (pos - fraction_start >= 9) {
        return std::nullopt;
      }
      fractional_ns = fractional_ns * 10 + (text[pos] - '0');
      ++pos;
    }
    const std::size_t fraction_digits = pos - fraction_start;
    if (fraction_digits == 0) {
      return std::nullopt;
    }
    for (std::size_t index = fraction_digits; index < 9; ++index) {
      fractional_ns *= 10;
    }
  }
  // Timezone suffix: 'Z' (UTC) or a numeric offset ±HH:MM / ±HHMM / ±HH.
  // Qt's QDateTime(..., QTimeZone::utc()).toString(Qt::ISODate) emits UTC as
  // "+00:00" (not "Z"), so the offset form must be accepted and folded back to
  // UTC, otherwise every date filter parsed empty and silently did nothing.
  int64_t offset_seconds = 0;
  if (pos < text.size() && text[pos] == 'Z') {
    ++pos;
  } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    const int sign = (text[pos] == '-') ? -1 : 1;
    ++pos;
    int offset_hours = 0;
    int offset_minutes = 0;
    if (!parseFixedDigits(text, pos, 2, offset_hours)) {
      return std::nullopt;
    }
    pos += 2;
    if (pos < text.size()) {
      if (text[pos] == ':') {
        ++pos;
      }
      if (!parseFixedDigits(text, pos, 2, offset_minutes)) {
        return std::nullopt;
      }
      pos += 2;
    }
    if (offset_hours > 23 || offset_minutes > 59) {
      return std::nullopt;
    }
    offset_seconds = sign * (offset_hours * 3600 + offset_minutes * 60);
  }
  if (pos != text.size()) {
    return std::nullopt;
  }

  // Subtract the zone offset to land on UTC ("12:00+05:00" == "07:00Z").
  const int64_t midnight_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::sys_days{date}.time_since_epoch()).count();
  const int64_t seconds = midnight_seconds + hour * 3600 + minute * 60 + second - offset_seconds;
  return combineSecondsAndNanos(seconds, fractional_ns);
}

}  // namespace PJ
