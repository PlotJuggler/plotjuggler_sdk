// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>

namespace PJ::sdk::source {

/// One cumulative-byte stream, trailing five seconds. Use one per topic and
/// sum rates for concurrent transfers. Caller owns synchronization. Injected
/// times are steady_clock points; a counter/time reset starts a new window.
class RollingTransferRate {
 public:
  using Clock = std::chrono::steady_clock;

  void add(std::uint64_t cumulative_bytes, Clock::time_point now = Clock::now()) {
    if (!samples_.empty() && (now < samples_.back().time || cumulative_bytes < samples_.back().bytes)) {
      samples_.clear();
    }
    if (!samples_.empty() && now == samples_.back().time) {
      samples_.back().bytes = cumulative_bytes;
    } else {
      samples_.push_back({now, cumulative_bytes});
    }
    // Keep at least two samples so bytesPerSecond() stays valid between widely-spaced add() calls.
    while (samples_.size() > 2 && samples_.front().time < now - std::chrono::seconds{5}) {
      samples_.pop_front();
    }
  }

  /// Rate between retained samples; zero until time has advanced. To reflect
  /// an idle stream, add its unchanged cumulative count at the current time.
  [[nodiscard]] double bytesPerSecond() const {
    if (samples_.size() < 2) {
      return 0.0;
    }
    const auto elapsed = std::chrono::duration<double>(samples_.back().time - samples_.front().time).count();
    return static_cast<double>(samples_.back().bytes - samples_.front().bytes) / elapsed;
  }

 private:
  struct Sample {
    Clock::time_point time;
    std::uint64_t bytes;
  };
  std::deque<Sample> samples_;
};

}  // namespace PJ::sdk::source
