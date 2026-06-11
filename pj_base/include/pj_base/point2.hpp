// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Plain (x, y) 2D point. Generic double-precision vocab type — used by image
// annotations (pixel coordinates), Filter Editor transforms (time/value
// samples), and any other 2D context where a tagged shape would be overkill.
// The semantic of x / y is owned by the caller.

namespace PJ::sdk {

struct Point2 {
  double x = 0.0;
  double y = 0.0;
  bool operator==(const Point2&) const = default;
};

}  // namespace PJ::sdk
