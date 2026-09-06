// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/time_format.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

#include "pj_base/time_math.hpp"

namespace PJ {

namespace {
constexpr int64_t kNsPerSecond = 1'000'000'000LL;
constexpr int64_t kSecondsPerDay = 24 * 60 * 60;

struct UtcTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

int64_t floorDiv(int64_t value, int64_t divisor) {
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder < 0 ? quotient - 1 : quotient;
}

UtcTime utcFromUnixSeconds(int64_t secs) {
  const int64_t days = floorDiv(secs, kSecondsPerDay);
  const int64_t seconds_of_day = secs - days * kSecondsPerDay;

  // Howard Hinnant's civil-from-days algorithm, with civil_days as days since 1970-01-01.
  const int64_t civil_days = days + 719468;
  const int64_t era = (civil_days >= 0 ? civil_days : civil_days - 146096) / 146097;
  const int64_t doe = civil_days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;

  UtcTime utc;
  utc.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  utc.month = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  utc.year = static_cast<int>(yoe + era * 400 + (utc.month <= 2));
  utc.hour = static_cast<int>(seconds_of_day / 3600);
  utc.minute = static_cast<int>((seconds_of_day % 3600) / 60);
  utc.second = static_cast<int>(seconds_of_day % 60);
  return utc;
}

UtcTime utcFromNanoseconds(int64_t ts_ns) {
  return utcFromUnixSeconds(floorDiv(ts_ns, kNsPerSecond));
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  // Howard Hinnant's days-from-civil algorithm, returning days since 1970-01-01.
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::string formatDate(const UtcTime& utc, char separator, bool day_first) {
  std::ostringstream os;
  os << std::setfill('0');
  if (day_first) {
    os << std::setw(2) << utc.day << separator << std::setw(2) << utc.month << separator << std::setw(4) << utc.year;
  } else {
    os << std::setw(4) << utc.year << separator << std::setw(2) << utc.month << separator << std::setw(2) << utc.day;
  }
  return os.str();
}

std::string formatTime(const UtcTime& utc) {
  std::ostringstream os;
  os << std::setfill('0') << std::setw(2) << utc.hour << ":" << std::setw(2) << utc.minute << ":" << std::setw(2)
     << utc.second;
  return os.str();
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

  std::ostringstream os;
  if (long_format) {
    os << std::setfill('0') << std::setw(2) << utc.day << "/" << std::setw(2) << utc.month << " ";
  }
  os << std::setfill('0') << std::setw(2) << utc.hour << ":" << std::setw(2) << utc.minute << ":" << std::setw(2)
     << utc.second;
  return os.str();
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
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
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
    if (pos < text.size() && text[pos] == ':') {
      ++pos;
    }
    if (pos < text.size() && isDigit(text[pos])) {
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

  const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  const UtcTime normalized = utcFromUnixSeconds(days * kSecondsPerDay);
  if (normalized.year != year || normalized.month != month || normalized.day != day) {
    return std::nullopt;
  }

  // Subtract the zone offset to land on UTC ("12:00+05:00" == "07:00Z").
  const int64_t seconds = days * kSecondsPerDay + hour * 3600 + minute * 60 + second - offset_seconds;
  return combineSecondsAndNanos(seconds, fractional_ns);
}

}  // namespace PJ
