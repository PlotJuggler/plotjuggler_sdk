#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/** @file time.hpp @brief Checked ROS and protobuf timestamp normalization. */

#include <cstdint>

#include "pj_base/parser_module/core.hpp"
#include "pj_base/time_math.hpp"

namespace pj {
namespace detail {

inline Expected<int64_t> combineSecondsAndNanos(int64_t seconds, int32_t nanos) {
  const auto combined = PJ::combineSecondsAndNanos(seconds, nanos);
  if (nanos < 0 || nanos >= INT64_C(1000000000)) {
    return Status::error("timestamp nanoseconds are outside [0, 1000000000)");
  }
  if (!combined) {
    return Status::error("timestamp is outside the int64 nanosecond range");
  }
  return *combined;
}

}  // namespace detail

[[nodiscard]] inline Expected<int64_t> readRosTime(int32_t seconds, uint32_t nanoseconds) {
  if (nanoseconds >= UINT32_C(1000000000)) {
    return Status::error("ROS time nanoseconds are outside [0, 1000000000)");
  }
  return detail::combineSecondsAndNanos(seconds, static_cast<int32_t>(nanoseconds));
}

[[nodiscard]] inline Expected<int64_t> readProtoTimestamp(int64_t seconds, int32_t nanoseconds) {
  return detail::combineSecondsAndNanos(seconds, nanoseconds);
}

}  // namespace pj
