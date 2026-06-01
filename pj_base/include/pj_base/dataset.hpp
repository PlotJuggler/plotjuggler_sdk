#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Dataset/time-domain vocabulary: TimeDomain (display offset), DatasetDescriptor
// (creation input), and the materialized dataset record stored by the engine.

#include <string>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// Time axis metadata shared by one or more datasets.
struct TimeDomain {
  /// Unique time-domain identifier.
  TimeDomainId id = 0;
  /// Human-readable domain name (e.g. "wall_clock", "sim_time").
  std::string name;
  /// Display-time shift in nanoseconds: display_time = raw_time - display_offset.
  Timestamp display_offset = 0;  // display_time = raw_time - display_offset
};

/// Input descriptor used when creating a dataset.
struct DatasetDescriptor {
  /// Source label shown to users (file path, topic root, capture name).
  std::string source_name;
  /// Time domain used by all topics in this dataset.
  TimeDomainId time_domain_id = 0;
};

/// Materialized dataset record stored in the engine.
struct DatasetInfo {
  /// Stable dataset identifier.
  DatasetId id = 0;
  /// Original source label.
  std::string source_name;
  /// Bound time-domain metadata.
  TimeDomain time_domain;
  /// Topics currently associated with this dataset.
  std::vector<TopicId> topic_ids;
};

}  // namespace PJ
