// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0
#pragma once

// Filter Editor transform contract. Plugins provide the concrete strategies;
// the host consumes them by id through FilterTransformFactory.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pj_base/point2.hpp"

namespace PJ::sdk {

/// Strategy interface for a Filter Editor transform.
///
/// Two streaming surfaces:
///  - `calculateNextPoint` — SISO, one input -> at most one output (PJ3 mirror).
///  - `appendTail` — process a chunk; default loops `calculateNextPoint`. Override
///    when running state (sliding sum, deque) makes it O(Δsamples) per call.
///
/// `clone()` is the host hand-off across the plugin DSO boundary.
class FilterTransform {
 public:
  virtual ~FilterTransform() = default;

  // Catalog identity used in saveConfig JSON, the factory registry, and the
  // legend.
  [[nodiscard]] virtual const char* id() const = 0;
  [[nodiscard]] virtual const char* label() const = 0;
  [[nodiscard]] virtual const char* bracketLabel() const = 0;

  // True if the transform can extend its output as new samples arrive.
  [[nodiscard]] virtual bool isStreamSafe() const = 0;

  /// Drop accumulated state. Must be called before the first `calculateNextPoint`
  /// after a series replace / clear.
  virtual void reset() = 0;

  /// One input -> optional output. Inputs MUST arrive in x-ascending order.
  /// nullopt suppresses the output (e.g. Derivative drops the first sample).
  [[nodiscard]] virtual std::optional<Point2> calculateNextPoint(const Point2& in) = 0;

  /// Process the tail of points since the previous call. Default loops
  /// `calculateNextPoint`; override per-transform when O(Δsamples) is possible.
  virtual void appendTail(const std::vector<Point2>& new_raw, std::vector<Point2>& out) {
    out.reserve(out.size() + new_raw.size());
    for (const auto& p : new_raw) {
      if (auto r = calculateNextPoint(p); r) {
        out.push_back(*r);
      }
    }
  }

  /// Run from scratch over a whole series. Default: reset + appendTail.
  virtual std::vector<Point2> applyBatch(const std::vector<Point2>& input) {
    reset();
    std::vector<Point2> out;
    out.reserve(input.size());
    appendTail(input, out);
    return out;
  }

  /// JSON for the parameter set this transform owns (not the source binding).
  [[nodiscard]] virtual std::string saveParams() const {
    return "{}";
  }
  virtual void loadParams(const std::string& /*json_str*/) {}

  /// Deep copy. The host calls this so the kept instance is independent of the
  /// plugin DSO (the cloned vtable lives in the plugin's code, which stays
  /// loaded for the app session).
  [[nodiscard]] virtual std::unique_ptr<FilterTransform> clone() const = 0;
};

}  // namespace PJ::sdk
