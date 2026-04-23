#pragma once

#include <atomic>
#include <memory>

namespace PJ {

/// Cooperative cancellation flag shared between a requester and a decoder.
///
/// The requester calls cancel(); the decoder polls isCancelled() between
/// decode units (NAL packets, JPEG scans, etc.) and returns early when set.
/// Shared via shared_ptr so both sides can hold it independently.
/// See ARCHITECTURE.md §3.3.
class CancelToken {
 public:
  [[nodiscard]] bool isCancelled() const noexcept {
    return flag_.load(std::memory_order_relaxed);
  }

  void cancel() noexcept {
    flag_.store(true, std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> flag_{false};
};

using CancelTokenPtr = std::shared_ptr<CancelToken>;

[[nodiscard]] inline CancelTokenPtr makeCancelToken() {
  return std::make_shared<CancelToken>();
}

}  // namespace PJ
