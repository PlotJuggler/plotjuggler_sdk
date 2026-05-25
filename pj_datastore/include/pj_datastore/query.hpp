#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "pj_base/types.hpp"
#include "pj_datastore/chunk.hpp"

namespace PJ {

struct QueryRange {
  /// Topic to query.
  PJ::TopicId topic_id = 0;
  /// Inclusive range start.
  PJ::Timestamp t_min = 0;
  /// Inclusive range end.
  PJ::Timestamp t_max = 0;
};

/// Point query descriptor for latest-at lookup.
struct QueryPoint {
  /// Topic to query.
  PJ::TopicId topic_id = 0;
  /// Query timestamp.
  PJ::Timestamp t = 0;
};

/// One materialized series sample. A series is a topic column viewed as a
/// time series; rows where the column is null are not samples.
struct SeriesSample {
  /// Sample timestamp.
  PJ::Timestamp timestamp = 0;
  /// Numeric sample value, converted to double for display/analysis.
  double value = 0.0;
  /// Pointer to source chunk containing this sample.
  const TopicChunk* chunk = nullptr;
  /// Physical row index inside `chunk`.
  std::size_t row_index = 0;
};

/// Bounds for a series over a query window.
struct SeriesBounds {
  /// Time range covered by value-bearing samples.
  PJ::Range<PJ::Timestamp> time;
  /// Finite value range covered by value-bearing samples.
  PJ::Range<double> value;
  /// Number of value-bearing samples in the range.
  std::size_t sample_count = 0;
};

/// One materialized row reference returned by cursors.
struct SampleRow {
  /// Sample timestamp.
  PJ::Timestamp timestamp = 0;
  /// Pointer to source chunk containing this row.
  const TopicChunk* chunk = nullptr;
  /// Row index inside `chunk`.
  std::size_t row_index = 0;
};

/// Contiguous row interval inside one chunk.
struct ChunkRowRange {
  /// Source chunk.
  const TopicChunk* chunk = nullptr;
  /// Inclusive start row.
  std::size_t row_start = 0;
  /// Exclusive end row.
  std::size_t row_end = 0;  // exclusive
};

// Cursor for iterating range query results across chunks
class RangeCursor {
 public:
  /// Construct cursor over [t_min, t_max] from committed chunks.
  RangeCursor(const std::deque<TopicChunk>& chunks, PJ::Timestamp t_min, PJ::Timestamp t_max);

  [[nodiscard]] bool valid() const noexcept;

  /// Advance to next matching row.
  void advance();

  /// Return current row descriptor.
  [[nodiscard]] SampleRow current() const;

  // Iterate all results via callback (per-row)
  void forEach(std::function<void(const SampleRow&)> callback);

  // Iterate chunk-at-a-time (bulk path)
  void forEachChunk(std::function<void(const ChunkRowRange&)> callback);

 private:
  const std::deque<TopicChunk>* chunks_;
  PJ::Timestamp t_min_;
  PJ::Timestamp t_max_;
  std::size_t chunk_index_ = 0;
  std::size_t row_index_ = 0;

  void findFirstValid();

  void skipToValid();
};

/// Cursor for iterating a topic column as a time series. It skips null rows by
/// definition; every current() value is a value-bearing sample for the bound
/// column.
class SeriesCursor {
 public:
  /// Construct cursor over [time_range.min, time_range.max] from committed chunks.
  SeriesCursor(const std::deque<TopicChunk>& chunks, std::size_t column_index, PJ::Range<PJ::Timestamp> time_range);

  [[nodiscard]] bool valid() const noexcept;

  /// Advance to next matching sample.
  void advance();

  /// Return current sample descriptor.
  [[nodiscard]] SeriesSample current() const;

  /// Iterate all results via callback.
  void forEach(std::function<void(const SeriesSample&)> callback);

 private:
  const std::deque<TopicChunk>* chunks_;
  std::size_t column_index_ = 0;
  PJ::Range<PJ::Timestamp> time_range_;
  std::size_t chunk_index_ = 0;
  std::size_t row_index_ = 0;

  void skipToSample();
};

/// View a topic column as a virtual vector of value-bearing time series
/// samples. Null rows are storage details and are not visible through this API.
class SeriesReader {
 public:
  /// Construct a series reader over committed chunks.
  SeriesReader(const std::deque<TopicChunk>& chunks, std::size_t column_index);

  /// Number of samples in the virtual series.
  [[nodiscard]] std::size_t size() const;

  /// True when the virtual series contains no samples.
  [[nodiscard]] bool empty() const;

  /// Return the sample at a virtual series index.
  [[nodiscard]] std::optional<SeriesSample> sampleAt(std::size_t index) const;

  /// Return the virtual series index of the latest sample at or before `t`.
  [[nodiscard]] std::optional<std::size_t> indexAtOrBeforeTime(PJ::Timestamp t) const;

  /// Return the virtual series index of the first sample at or after `t`.
  [[nodiscard]] std::optional<std::size_t> indexAtOrAfterTime(PJ::Timestamp t) const;

  /// Return the latest sample at or before `t`.
  [[nodiscard]] std::optional<SeriesSample> sampleAtOrBeforeTime(PJ::Timestamp t) const;

  /// Return the first sample at or after `t`.
  [[nodiscard]] std::optional<SeriesSample> sampleAtOrAfterTime(PJ::Timestamp t) const;

  /// Iterate samples in an inclusive time range.
  [[nodiscard]] SeriesCursor samples(PJ::Range<PJ::Timestamp> time_range) const;

  /// Return bounds over the entire series.
  [[nodiscard]] std::optional<SeriesBounds> bounds() const;

  /// Return bounds over an inclusive time range.
  [[nodiscard]] std::optional<SeriesBounds> bounds(PJ::Range<PJ::Timestamp> time_range) const;

 private:
  const std::deque<TopicChunk>* chunks_;
  std::size_t column_index_ = 0;
};

// Find the most recent sample at or before time t; nullopt if none exists.
[[nodiscard]] std::optional<SampleRow> latestAt(const std::deque<TopicChunk>& chunks, PJ::Timestamp t);

// Create a range cursor
[[nodiscard]] RangeCursor rangeQuery(const std::deque<TopicChunk>& chunks, PJ::Timestamp t_min, PJ::Timestamp t_max);

}  // namespace PJ
