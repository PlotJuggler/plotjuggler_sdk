/**
 * @file log.hpp
 * @brief A single textual log message (level + text + originating name).
 *
 * Log is a small owned builtin — no byte blob, no BufferAnchor. It mirrors the
 * core of Foxglove's Log schema (and rcl_interfaces/Log / rosgraph_msgs/Log),
 * expressed as canonical PJ vocabulary. The Foxglove source-location fields
 * (`file`, `line`) are intentionally omitted.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// A textual log message at a point in time.
struct Log {
  /// Severity level. Values match Foxglove's Log.Level.
  enum class Level : uint8_t {
    kUnknown = 0,
    kDebug = 1,
    kInfo = 2,
    kWarning = 3,
    kError = 4,
    kFatal = 5,
  };

  Timestamp timestamp_ns = 0;
  Level level = Level::kUnknown;
  std::string message;  ///< Log text.
  std::string name;     ///< Originating process / node / logger name.

  bool operator==(const Log&) const = default;
};

}  // namespace sdk
}  // namespace PJ
