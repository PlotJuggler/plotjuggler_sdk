#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

// Built-in Filter Transform catalogue — concrete classes vendored in the SDK
// so the Filter Editor plugin and the host (PJ4 app) consume the SAME math
// from ONE source. Each class implements PJ::sdk::FilterTransform from
// filter_transform.hpp.
//
// Mirrors PJ3's TransformFunction_SISO catalogue: each class owns its
// parameters, implements calculateNextPoint() for SISO streaming, persists
// itself via saveParams() / loadParams(), and clones via clone(). Pure C++20 —
// no Qt, no Lua, no datastore — fully unit-testable in isolation.
//
// Registration: call registerAllTransforms() once per DSO (idempotent). Both
// the plugin and the host call it to populate their own factory instances —
// the singletons are per-DSO because plugins are dlopen'd RTLD_LOCAL, so
// neither side ever sees the other's instances; only the JSON wire format
// (saved params) crosses the boundary.

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// nlohmann/json is required for per-transform parameter persistence. Guard the
// include so the classes can be exercised through the computation API alone
// (e.g. from a test that does not link nlohmann_json).
#ifdef NLOHMANN_JSON_HPP
#define PJ_TRANSFORM_HAS_JSON 1
#else
#ifdef __has_include
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define PJ_TRANSFORM_HAS_JSON 1
#endif
#endif
#endif

#include "pj_plugins/sdk/filter_transform.hpp"
#include "pj_plugins/sdk/filter_transform_factory.hpp"

namespace PJ::sdk {

// ---------------------------------------------------------------------------
// Concrete transforms
// ---------------------------------------------------------------------------

// --- None (passthrough) ---

class NoneTransform : public FilterTransform {
 public:
  const char* id() const override {
    return "none";
  }
  const char* label() const override {
    return "-- No Transform --";
  }
  const char* bracketLabel() const override {
    return "copy";
  }
  bool isStreamSafe() const override {
    return true;
  }
  void reset() override {}
  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    return in;
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<NoneTransform>(*this);
  }
};

// --- Absolute ---

class AbsoluteTransform : public FilterTransform {
 public:
  const char* id() const override {
    return "absolute";
  }
  const char* label() const override {
    return "Absolute";
  }
  const char* bracketLabel() const override {
    return "Absolute";
  }
  bool isStreamSafe() const override {
    return true;
  }
  void reset() override {}
  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    return Point2{in.x, std::abs(in.y)};
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<AbsoluteTransform>(*this);
  }
};

// --- Scale / Offset ---

class ScaleTransform : public FilterTransform {
 public:
  double value_scale = 1.0;
  double value_offset = 0.0;
  double time_offset = 0.0;

  const char* id() const override {
    return "scale";
  }
  const char* label() const override {
    return "Scale/Offset";
  }
  const char* bracketLabel() const override {
    return "Scale";
  }
  bool isStreamSafe() const override {
    return true;
  }
  void reset() override {}
  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    return Point2{in.x + time_offset, value_scale * in.y + value_offset};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["value_scale"] = value_scale;
    j["value_offset"] = value_offset;
    j["time_offset"] = time_offset;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    value_scale = j.value("value_scale", 1.0);
    value_offset = j.value("value_offset", 0.0);
    time_offset = j.value("time_offset", 0.0);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<ScaleTransform>(*this);
  }
};

// --- Derivative ---

class DerivativeTransform : public FilterTransform {
 public:
  bool use_custom_dt = false;
  double custom_dt = 1.0;

  const char* id() const override {
    return "derivative";
  }
  const char* label() const override {
    return "Derivative";
  }
  const char* bracketLabel() const override {
    return "Derivative";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    prev_ = std::nullopt;
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    if (!prev_.has_value()) {
      prev_ = in;
      return std::nullopt;
    }
    const double dt = use_custom_dt ? custom_dt : (in.x - prev_->x);
    if (dt <= 0.0) {
      prev_ = in;
      return std::nullopt;
    }
    const Point2 out{prev_->x, (in.y - prev_->y) / dt};
    prev_ = in;
    return out;
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["use_custom_dt"] = use_custom_dt;
    j["custom_dt"] = custom_dt;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    use_custom_dt = j.value("use_custom_dt", false);
    custom_dt = j.value("custom_dt", 1.0);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<DerivativeTransform>(*this);
  }

 private:
  std::optional<Point2> prev_;
};

// --- Integral (trapezoid) ---

class IntegralTransform : public FilterTransform {
 public:
  bool use_custom_dt = false;
  double custom_dt = 1.0;

  const char* id() const override {
    return "integral";
  }
  const char* label() const override {
    return "Integral";
  }
  const char* bracketLabel() const override {
    return "Integral";
  }
  // Unbounded running accumulator: correct only when processed from the start
  // in order; not incrementally safe in the streaming sense.
  bool isStreamSafe() const override {
    return false;
  }

  void reset() override {
    acc_ = 0.0;
    prev_ = std::nullopt;
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    if (!prev_.has_value()) {
      prev_ = in;
      return std::nullopt;
    }
    const double dt = use_custom_dt ? custom_dt : (in.x - prev_->x);
    if (dt > 0.0) {
      acc_ += (in.y + prev_->y) * dt / 2.0;
    }
    const Point2 out{in.x, acc_};
    prev_ = in;
    return out;
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["use_custom_dt"] = use_custom_dt;
    j["custom_dt"] = custom_dt;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    use_custom_dt = j.value("use_custom_dt", false);
    custom_dt = j.value("custom_dt", 1.0);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<IntegralTransform>(*this);
  }

 private:
  double acc_ = 0.0;
  std::optional<Point2> prev_;
};

// --- Moving Average ---

class MovingAverageTransform : public FilterTransform {
 public:
  int window = 10;
  bool compensate_time_offset = false;

  const char* id() const override {
    return "moving_average";
  }
  const char* label() const override {
    return "Moving Average";
  }
  const char* bracketLabel() const override {
    return "Moving Average";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    buf_.clear();
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    buf_.push_back(in);
    const size_t w = static_cast<size_t>(std::max(1, window));
    while (buf_.size() > w) {
      buf_.erase(buf_.begin());
    }
    // Pad underfull window with current point (PJ3 semantics)
    double total = in.y * static_cast<double>(w - buf_.size());
    for (const auto& p : buf_) {
      total += p.y;
    }
    double t = in.x;
    if (compensate_time_offset && buf_.size() > 1) {
      t = (buf_.front().x + buf_.back().x) / 2.0;
    }
    return Point2{t, total / static_cast<double>(w)};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["window"] = window;
    j["compensate_time_offset"] = compensate_time_offset;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    window = j.value("window", 10);
    compensate_time_offset = j.value("compensate_time_offset", false);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<MovingAverageTransform>(*this);
  }

 private:
  std::vector<Point2> buf_;
};

// --- Moving RMS ---

class MovingRMSTransform : public FilterTransform {
 public:
  int window = 10;

  const char* id() const override {
    return "moving_rms";
  }
  const char* label() const override {
    return "Moving Root Mean Squared";
  }
  const char* bracketLabel() const override {
    return "Moving Root Mean Squared";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    buf_.clear();
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    buf_.push_back(in);
    const size_t w = static_cast<size_t>(std::max(1, window));
    while (buf_.size() > w) {
      buf_.erase(buf_.begin());
    }
    double total_sqr = in.y * in.y * static_cast<double>(w - buf_.size());
    for (const auto& p : buf_) {
      total_sqr += p.y * p.y;
    }
    return Point2{in.x, std::sqrt(total_sqr / static_cast<double>(w))};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["window"] = window;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    window = j.value("window", 10);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<MovingRMSTransform>(*this);
  }

 private:
  std::vector<Point2> buf_;
};

// --- Moving Variance / Stdev ---

class MovingVarianceTransform : public FilterTransform {
 public:
  int window = 10;
  bool std_dev = false;  // true → output sqrt(variance)

  const char* id() const override {
    return "moving_variance";
  }
  const char* label() const override {
    return "Moving Variance / Stdev";
  }
  const char* bracketLabel() const override {
    return "Moving Variance / Stdev";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    buf_.clear();
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    buf_.push_back(in);
    const size_t w = static_cast<size_t>(std::max(1, window));
    while (buf_.size() > w) {
      buf_.erase(buf_.begin());
    }
    const double pad = static_cast<double>(w - buf_.size());
    double total = in.y * pad;
    for (const auto& p : buf_) {
      total += p.y;
    }
    const double avg = total / static_cast<double>(w);
    double total_sqr = (in.y - avg) * (in.y - avg) * pad;
    for (const auto& p : buf_) {
      const double v = p.y - avg;
      total_sqr += v * v;
    }
    const double var = total_sqr / static_cast<double>(w);
    return Point2{in.x, std_dev ? std::sqrt(var) : var};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["window"] = window;
    j["std_dev"] = std_dev;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    window = j.value("window", 10);
    std_dev = j.value("std_dev", false);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<MovingVarianceTransform>(*this);
  }

 private:
  std::vector<Point2> buf_;
};

// --- Outlier Removal ---

class OutlierRemovalTransform : public FilterTransform {
 public:
  double outlier_factor = 100.0;

  const char* id() const override {
    return "outlier_removal";
  }
  const char* label() const override {
    return "Outlier Removal";
  }
  const char* bracketLabel() const override {
    return "Outlier Removal";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    buf_.clear();
  }

  // Overrides applyBatch: needs 4-sample look-ahead ring (PJ3 semantics).
  std::vector<Point2> applyBatch(const std::vector<Point2>& input) override {
    const size_t n = input.size();
    std::vector<Point2> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      if (i < 3) {
        out.push_back(input[i]);
        continue;
      }
      const double d1 = input[i - 2].y - input[i - 1].y;
      const double d2 = input[i - 1].y - input[i].y;
      bool drop = false;
      if (d1 * d2 < 0) {
        const double d0 = input[i - 3].y - input[i - 2].y;
        const double jump = std::max(std::abs(d1), std::abs(d2));
        const double ratio = (d0 == 0.0) ? std::numeric_limits<double>::infinity() : jump / std::abs(d0);
        if (ratio > outlier_factor) {
          drop = true;
        }
      }
      if (!drop) {
        // Intentional PJ3 parity: emit the *previous* sample (i-1), not the
        // current one (i). The detector needs one look-ahead sample to decide
        // whether i-1 is an outlier, so the series is delayed by one sample.
        out.push_back({input[i - 1].x, input[i - 1].y});
      }
    }
    return out;
  }

  // calculateNextPoint not used (applyBatch overridden), but required by interface.
  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    buf_.push_back(in);
    if (buf_.size() < 4) {
      return in;
    }
    const size_t i = buf_.size() - 1;
    const double d1 = buf_[i - 2].y - buf_[i - 1].y;
    const double d2 = buf_[i - 1].y - buf_[i].y;
    if (d1 * d2 < 0) {
      const double d0 = buf_[i - 3].y - buf_[i - 2].y;
      const double jump = std::max(std::abs(d1), std::abs(d2));
      const double ratio = (d0 == 0.0) ? std::numeric_limits<double>::infinity() : jump / std::abs(d0);
      if (ratio > outlier_factor) {
        return std::nullopt;
      }
    }
    return Point2{buf_[i - 1].x, buf_[i - 1].y};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["outlier_factor"] = outlier_factor;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    outlier_factor = j.value("outlier_factor", 100.0);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<OutlierRemovalTransform>(*this);
  }

 private:
  std::vector<Point2> buf_;
};

// --- Samples Counter ---

class SamplesCounterTransform : public FilterTransform {
 public:
  int samples_ms = 1000;

  const char* id() const override {
    return "samples_counter";
  }
  const char* label() const override {
    return "Samples Counter";
  }
  const char* bracketLabel() const override {
    return "Samples Counter";
  }
  // Time-windowed look-back: needs all prior samples in the window.
  bool isStreamSafe() const override {
    return false;
  }

  void reset() override {
    all_.clear();
  }

  // Overrides applyBatch for correct time-window semantics.
  std::vector<Point2> applyBatch(const std::vector<Point2>& input) override {
    const size_t n = input.size();
    const double delta = 0.001 * static_cast<double>(samples_ms);
    std::vector<Point2> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      const double min_t = input[i].x - delta;
      size_t lo = 0, hi = i + 1;
      while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (input[mid].x < min_t) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      out.push_back({input[i].x, static_cast<double>(i - lo)});
    }
    return out;
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    all_.push_back(in);
    const double delta = 0.001 * static_cast<double>(samples_ms);
    const double min_t = in.x - delta;
    size_t count = 0;
    for (size_t i = all_.size(); i-- > 0;) {
      if (all_[i].x < min_t) {
        break;
      }
      ++count;
    }
    return Point2{in.x, static_cast<double>(count - 1)};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["samples_ms"] = samples_ms;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    samples_ms = j.value("samples_ms", 1000);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<SamplesCounterTransform>(*this);
  }

 private:
  std::vector<Point2> all_;
};

// --- Binary Filter ---

enum class BinaryOp { kEqual, kLess, kLessEq, kGreater, kGreaterEq, kRange };

class BinaryFilterTransform : public FilterTransform {
 public:
  BinaryOp op = BinaryOp::kGreater;
  double a = 0.0;
  double b = 0.0;

  const char* id() const override {
    return "binary_filter";
  }
  const char* label() const override {
    return "Binary Filter";
  }
  const char* bracketLabel() const override {
    return "Binary Filter";
  }
  bool isStreamSafe() const override {
    return true;
  }
  void reset() override {}

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    double r = 0.0;
    switch (op) {
      case BinaryOp::kEqual:
        r = (std::abs(in.y - a) <= 1e-9 * std::max(1.0, std::abs(a))) ? 1.0 : 0.0;
        break;
      case BinaryOp::kLess:
        r = (in.y < a) ? 1.0 : 0.0;
        break;
      case BinaryOp::kLessEq:
        r = (in.y <= a) ? 1.0 : 0.0;
        break;
      case BinaryOp::kGreater:
        r = (in.y > a) ? 1.0 : 0.0;
        break;
      case BinaryOp::kGreaterEq:
        r = (in.y >= a) ? 1.0 : 0.0;
        break;
      case BinaryOp::kRange: {
        const double lo = std::min(a, b), hi = std::max(a, b);
        r = (in.y >= lo && in.y <= hi) ? 1.0 : 0.0;
        break;
      }
    }
    return Point2{in.x, r};
  }
  std::string saveParams() const override {
#ifdef PJ_TRANSFORM_HAS_JSON
    nlohmann::json j;
    j["binary_op"] = static_cast<int>(op);
    j["binary_a"] = a;
    j["binary_b"] = b;
    return j.dump();
#else
    return "{}";
#endif
  }
  void loadParams(const std::string& json_str) override {
    (void)json_str;
#ifdef PJ_TRANSFORM_HAS_JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
      return;
    }
    op = static_cast<BinaryOp>(j.value("binary_op", static_cast<int>(BinaryOp::kGreater)));
    a = j.value("binary_a", 0.0);
    b = j.value("binary_b", 0.0);
#endif
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<BinaryFilterTransform>(*this);
  }
};

// --- Time Since Previous Point2 ---

class TimeSincePreviousTransform : public FilterTransform {
 public:
  const char* id() const override {
    return "time_since_previous";
  }
  const char* label() const override {
    return "Time Since Previous Point2";
  }
  const char* bracketLabel() const override {
    return "Time Since Previous Point2";
  }
  bool isStreamSafe() const override {
    return true;
  }

  void reset() override {
    prev_t_ = std::nullopt;
  }

  std::optional<Point2> calculateNextPoint(const Point2& in) override {
    if (!prev_t_.has_value()) {
      prev_t_ = in.x;
      return std::nullopt;
    }
    const Point2 out{in.x, in.x - *prev_t_};
    prev_t_ = in.x;
    return out;
  }
  std::unique_ptr<FilterTransform> clone() const override {
    return std::make_unique<TimeSincePreviousTransform>(*this);
  }

 private:
  std::optional<double> prev_t_;
};

// ---------------------------------------------------------------------------
// Registration — all transforms in display order (mirrors PJ3 dropdown order)
// ---------------------------------------------------------------------------

// Idempotent: caller can invoke this on every build path without worry. Each
// DSO has its own factory instance and its own `registered` guard.
inline void registerAllTransforms() {
  auto& f = FilterTransformFactory::instance();
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  f.registerTransform(NoneTransform{}.id(), [] { return std::make_unique<NoneTransform>(); });
  f.registerTransform(AbsoluteTransform{}.id(), [] { return std::make_unique<AbsoluteTransform>(); });
  f.registerTransform(ScaleTransform{}.id(), [] { return std::make_unique<ScaleTransform>(); });
  f.registerTransform(DerivativeTransform{}.id(), [] { return std::make_unique<DerivativeTransform>(); });
  f.registerTransform(IntegralTransform{}.id(), [] { return std::make_unique<IntegralTransform>(); });
  f.registerTransform(MovingAverageTransform{}.id(), [] { return std::make_unique<MovingAverageTransform>(); });
  f.registerTransform(MovingRMSTransform{}.id(), [] { return std::make_unique<MovingRMSTransform>(); });
  f.registerTransform(MovingVarianceTransform{}.id(), [] { return std::make_unique<MovingVarianceTransform>(); });
  f.registerTransform(OutlierRemovalTransform{}.id(), [] { return std::make_unique<OutlierRemovalTransform>(); });
  f.registerTransform(SamplesCounterTransform{}.id(), [] { return std::make_unique<SamplesCounterTransform>(); });
  f.registerTransform(BinaryFilterTransform{}.id(), [] { return std::make_unique<BinaryFilterTransform>(); });
  f.registerTransform(TimeSincePreviousTransform{}.id(), [] { return std::make_unique<TimeSincePreviousTransform>(); });
}

}  // namespace PJ::sdk
