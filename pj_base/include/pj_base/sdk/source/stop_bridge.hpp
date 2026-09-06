// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace PJ::sdk::source {

/// Bridges the host Stop button to provider cancellation (not an elapsed-time
/// JobControl::armWatchdog). Checks immediately, then at the configured period.
/// Actions must not throw; cancel is invoked once on the poller thread. It must
/// not destroy/join this poller or join a producer that owns it.
///
/// Construct after the live ingest context; destroy (or stopAndJoin) BEFORE
/// releasing that context. The join establishes happens-before for its release.
/// Lifecycle operations belong to the owning thread; no concurrent joins.
///
/// Reverse direction: provider-cancel must call requestStop BEFORE joining
/// producers, using synchronization independent of any blocked push. Holding
/// the push/progress mutex while requesting Stop can deadlock the whole batch.
class StopPoller {
 public:
  StopPoller(
      std::function<bool()> is_stop_requested, std::function<void()> cancel,
      std::chrono::milliseconds period = std::chrono::milliseconds{50});
  ~StopPoller();
  StopPoller(const StopPoller&) = delete;
  StopPoller& operator=(const StopPoller&) = delete;

  /// Idempotently stop and join; wakes the timed wait immediately.
  void stopAndJoin();

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopped_ = false;
  std::thread thread_;
};

}  // namespace PJ::sdk::source
