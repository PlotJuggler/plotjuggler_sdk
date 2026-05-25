// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/query.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace PJ {
namespace {

[[nodiscard]] Range<Timestamp> normalized(Range<Timestamp> range) {
  if (range.max < range.min) {
    std::swap(range.min, range.max);
  }
  return range;
}

[[nodiscard]] bool isBoolColumn(const TopicChunk& chunk, std::size_t column_index) {
  return column_index < chunk.columns.size() && chunk.columns[column_index].descriptor &&
         chunk.columns[column_index].descriptor->logical_type == PrimitiveType::kBool;
}

[[nodiscard]] std::optional<double> readSeriesValue(
    const TopicChunk& chunk, std::size_t column_index, std::size_t row) {
  if (column_index >= chunk.columns.size() || row >= chunk.stats.row_count || chunk.isNull(column_index, row)) {
    return std::nullopt;
  }
  if (isBoolColumn(chunk, column_index)) {
    return chunk.readBool(column_index, row) ? 1.0 : 0.0;
  }
  return chunk.readNumericAsDouble(column_index, row);
}

[[nodiscard]] SeriesSample makeSeriesSample(const TopicChunk& chunk, std::size_t column_index, std::size_t row) {
  const auto value = readSeriesValue(chunk, column_index, row);
  assert(value.has_value());
  return SeriesSample{chunk.readTimestamp(row), *value, &chunk, row};
}

[[nodiscard]] Range<Timestamp> allTime() {
  return Range<Timestamp>{
      .min = std::numeric_limits<Timestamp>::min(),
      .max = std::numeric_limits<Timestamp>::max(),
  };
}

}  // namespace

// ===========================================================================
// RangeCursor
// ===========================================================================

RangeCursor::RangeCursor(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max)
    : chunks_(&chunks), t_min_(t_min), t_max_(t_max) {
  findFirstValid();
}

bool RangeCursor::valid() const noexcept {
  return chunk_index_ < chunks_->size();
}

SampleRow RangeCursor::current() const {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  return SampleRow{chunk.readTimestamp(row_index_), &chunk, row_index_};
}

void RangeCursor::advance() {
  assert(valid());
  const auto& chunk = (*chunks_)[chunk_index_];
  ++row_index_;
  if (row_index_ >= chunk.stats.row_count) {
    ++chunk_index_;
    row_index_ = 0;
  }
  skipToValid();
}

void RangeCursor::forEach(std::function<void(const SampleRow&)> callback) {
  while (valid()) {
    callback(current());
    advance();
  }
}

void RangeCursor::forEachChunk(std::function<void(const ChunkRowRange&)> callback) {
  while (chunk_index_ < chunks_->size()) {
    const auto& chunk = (*chunks_)[chunk_index_];

    // Skip chunks entirely before our range
    if (chunk.stats.t_max < t_min_) {
      ++chunk_index_;
      continue;
    }
    // Stop if chunk is entirely after our range
    if (chunk.stats.t_min > t_max_) {
      break;
    }

    // Find first valid row in this chunk (>= t_min_)
    std::size_t first = row_index_;
    while (first < chunk.stats.row_count && chunk.readTimestamp(first) < t_min_) {
      ++first;
    }

    // Find one-past-last valid row in this chunk (<= t_max_)
    std::size_t end = first;
    while (end < chunk.stats.row_count && chunk.readTimestamp(end) <= t_max_) {
      ++end;
    }

    if (first < end) {
      callback(ChunkRowRange{&chunk, first, end});
    }

    // Move to next chunk
    ++chunk_index_;
    row_index_ = 0;
  }
  // Mark cursor exhausted
  chunk_index_ = chunks_->size();
}

void RangeCursor::findFirstValid() {
  // Linear scan to find the first chunk whose t_max >= t_min_
  // (i.e., that could contain data in our range)
  for (chunk_index_ = 0; chunk_index_ < chunks_->size(); ++chunk_index_) {
    const auto& chunk = (*chunks_)[chunk_index_];
    if (chunk.stats.t_max >= t_min_) {
      // This chunk might contain data in range.
      // Now find the first row where timestamp >= t_min_.
      for (row_index_ = 0; row_index_ < chunk.stats.row_count; ++row_index_) {
        Timestamp ts = chunk.readTimestamp(row_index_);
        if (ts >= t_min_) {
          // Check if this row is also within t_max
          if (ts <= t_max_) {
            return;  // Found a valid starting position
          }
          // ts > t_max_ means no valid data in range at all
          chunk_index_ = chunks_->size();
          return;
        }
      }
      // All rows in this chunk are before t_min, try next chunk
      continue;
    }
  }
  // No valid chunk found: chunk_index_ == chunks_->size() (past-end)
}

void RangeCursor::skipToValid() {
  if (!valid()) {
    return;
  }
  const auto& chunk = (*chunks_)[chunk_index_];
  Timestamp ts = chunk.readTimestamp(row_index_);
  if (ts > t_max_) {
    // Past the end of the query range
    chunk_index_ = chunks_->size();
    return;
  }
  // ts >= t_min_ is guaranteed by how we advance through sorted data
}

// ===========================================================================
// latest_at
// ===========================================================================

std::optional<SampleRow> latestAt(const std::deque<TopicChunk>& chunks, Timestamp t) {
  if (chunks.empty()) {
    return std::nullopt;
  }

  // Reverse iterate chunks. For each chunk, if t_min <= t, search within it.
  for (std::size_t ci = chunks.size(); ci > 0; --ci) {
    const auto& chunk = chunks[ci - 1];
    if (chunk.stats.t_min > t) {
      continue;  // Entire chunk is after t
    }
    // chunk.stats.t_min <= t, so there might be a row <= t in this chunk.
    // Reverse scan within the chunk to find the last row with timestamp <= t.
    for (std::size_t ri = chunk.stats.row_count; ri > 0; --ri) {
      Timestamp ts = chunk.readTimestamp(ri - 1);
      if (ts <= t) {
        return SampleRow{ts, &chunk, ri - 1};
      }
    }
    // All rows in this chunk are after t, but t_min <= t was true.
    // This shouldn't happen with sorted data, but handle gracefully
    // by continuing to the previous chunk.
  }

  return std::nullopt;
}

// ===========================================================================
// range_query
// ===========================================================================

RangeCursor rangeQuery(const std::deque<TopicChunk>& chunks, Timestamp t_min, Timestamp t_max) {
  return RangeCursor(chunks, t_min, t_max);
}

// ===========================================================================
// SeriesCursor
// ===========================================================================

SeriesCursor::SeriesCursor(const std::deque<TopicChunk>& chunks, std::size_t column_index, Range<Timestamp> time_range)
    : chunks_(&chunks), column_index_(column_index), time_range_(normalized(time_range)) {
  skipToSample();
}

bool SeriesCursor::valid() const noexcept {
  return chunk_index_ < chunks_->size();
}

SeriesSample SeriesCursor::current() const {
  assert(valid());
  return makeSeriesSample((*chunks_)[chunk_index_], column_index_, row_index_);
}

void SeriesCursor::advance() {
  assert(valid());
  ++row_index_;
  skipToSample();
}

void SeriesCursor::forEach(std::function<void(const SeriesSample&)> callback) {
  while (valid()) {
    callback(current());
    advance();
  }
}

void SeriesCursor::skipToSample() {
  while (chunk_index_ < chunks_->size()) {
    const auto& chunk = (*chunks_)[chunk_index_];

    if (chunk.stats.row_count == 0 || chunk.stats.t_max < time_range_.min || column_index_ >= chunk.columns.size()) {
      ++chunk_index_;
      row_index_ = 0;
      continue;
    }

    if (chunk.stats.t_min > time_range_.max) {
      chunk_index_ = chunks_->size();
      return;
    }

    while (row_index_ < chunk.stats.row_count) {
      const Timestamp ts = chunk.readTimestamp(row_index_);
      if (ts < time_range_.min) {
        ++row_index_;
        continue;
      }
      if (ts > time_range_.max) {
        chunk_index_ = chunks_->size();
        return;
      }
      if (readSeriesValue(chunk, column_index_, row_index_).has_value()) {
        return;
      }
      ++row_index_;
    }

    ++chunk_index_;
    row_index_ = 0;
  }
}

// ===========================================================================
// SeriesReader
// ===========================================================================

SeriesReader::SeriesReader(const std::deque<TopicChunk>& chunks, std::size_t column_index)
    : chunks_(&chunks), column_index_(column_index) {}

std::size_t SeriesReader::size() const {
  std::size_t count = 0;
  for (const TopicChunk& chunk : *chunks_) {
    if (column_index_ >= chunk.columns.size()) {
      continue;
    }
    for (std::size_t row = 0; row < chunk.stats.row_count; ++row) {
      if (readSeriesValue(chunk, column_index_, row).has_value()) {
        ++count;
      }
    }
  }
  return count;
}

bool SeriesReader::empty() const {
  return size() == 0;
}

std::optional<SeriesSample> SeriesReader::sampleAt(std::size_t index) const {
  std::size_t series_index = 0;
  for (const TopicChunk& chunk : *chunks_) {
    if (column_index_ >= chunk.columns.size()) {
      continue;
    }
    for (std::size_t row = 0; row < chunk.stats.row_count; ++row) {
      if (!readSeriesValue(chunk, column_index_, row).has_value()) {
        continue;
      }
      if (series_index == index) {
        return makeSeriesSample(chunk, column_index_, row);
      }
      ++series_index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> SeriesReader::indexAtOrBeforeTime(Timestamp t) const {
  std::optional<std::size_t> latest;
  std::size_t series_index = 0;
  for (const TopicChunk& chunk : *chunks_) {
    if (chunk.stats.row_count == 0 || column_index_ >= chunk.columns.size()) {
      continue;
    }
    if (chunk.stats.t_min > t) {
      break;
    }
    for (std::size_t row = 0; row < chunk.stats.row_count; ++row) {
      const Timestamp ts = chunk.readTimestamp(row);
      if (ts > t) {
        return latest;
      }
      if (readSeriesValue(chunk, column_index_, row).has_value()) {
        latest = series_index;
        ++series_index;
      }
    }
  }
  return latest;
}

std::optional<std::size_t> SeriesReader::indexAtOrAfterTime(Timestamp t) const {
  std::size_t series_index = 0;
  for (const TopicChunk& chunk : *chunks_) {
    if (chunk.stats.row_count == 0 || column_index_ >= chunk.columns.size()) {
      continue;
    }
    if (chunk.stats.t_max < t) {
      for (std::size_t row = 0; row < chunk.stats.row_count; ++row) {
        if (readSeriesValue(chunk, column_index_, row).has_value()) {
          ++series_index;
        }
      }
      continue;
    }
    for (std::size_t row = 0; row < chunk.stats.row_count; ++row) {
      if (!readSeriesValue(chunk, column_index_, row).has_value()) {
        continue;
      }
      if (chunk.readTimestamp(row) >= t) {
        return series_index;
      }
      ++series_index;
    }
  }
  return std::nullopt;
}

std::optional<SeriesSample> SeriesReader::sampleAtOrBeforeTime(Timestamp t) const {
  const auto index = indexAtOrBeforeTime(t);
  return index.has_value() ? sampleAt(*index) : std::nullopt;
}

std::optional<SeriesSample> SeriesReader::sampleAtOrAfterTime(Timestamp t) const {
  const auto index = indexAtOrAfterTime(t);
  return index.has_value() ? sampleAt(*index) : std::nullopt;
}

SeriesCursor SeriesReader::samples(Range<Timestamp> time_range) const {
  return SeriesCursor(*chunks_, column_index_, time_range);
}

std::optional<SeriesBounds> SeriesReader::bounds() const {
  return bounds(allTime());
}

std::optional<SeriesBounds> SeriesReader::bounds(Range<Timestamp> time_range) const {
  SeriesBounds result;
  bool found_time = false;
  bool found_value = false;
  auto cursor = samples(time_range);
  cursor.forEach([&](const SeriesSample& sample) {
    if (!found_time) {
      result.time.min = sample.timestamp;
      result.time.max = sample.timestamp;
      found_time = true;
    } else {
      result.time.min = std::min(result.time.min, sample.timestamp);
      result.time.max = std::max(result.time.max, sample.timestamp);
    }

    if (std::isfinite(sample.value)) {
      if (!found_value) {
        result.value.min = sample.value;
        result.value.max = sample.value;
        found_value = true;
      } else {
        result.value.min = std::min(result.value.min, sample.value);
        result.value.max = std::max(result.value.max, sample.value);
      }
    }
    ++result.sample_count;
  });

  if (!found_time || !found_value) {
    return std::nullopt;
  }
  return result;
}

}  // namespace PJ
