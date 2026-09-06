// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/source/stop_bridge.hpp"

#include <stdexcept>
#include <utility>

namespace PJ::sdk::source {

StopPoller::StopPoller(
    std::function<bool()> is_stop_requested, std::function<void()> cancel, std::chrono::milliseconds period) {
  if (!is_stop_requested || !cancel || period <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("StopPoller needs both actions and a positive period");
  }
  thread_ = std::thread([this, check = std::move(is_stop_requested), action = std::move(cancel), period] {
    std::unique_lock lock(mutex_);
    while (!stopped_) {
      lock.unlock();
      const bool requested = check();
      if (requested) {
        action();
        return;
      }
      lock.lock();
      wake_.wait_for(lock, period, [this] { return stopped_; });
    }
  });
}

StopPoller::~StopPoller() {
  stopAndJoin();
}

void StopPoller::stopAndJoin() {
  {
    std::lock_guard lock(mutex_);
    stopped_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace PJ::sdk::source
