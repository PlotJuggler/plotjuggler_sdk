#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace PJ {

/// Pixel buffer output from the compositor (all layers blended).
struct CompositeFrame {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int channels = 0;
};

/// Value returned by FrameSlot::take().
struct SlotResult {
  int64_t timestamp_ns = 0;
  CompositeFrame frame;
};

/// Single-slot latest-wins mailbox for decoded frame delivery.
///
/// The worker thread calls store() after compositing; the UI thread
/// polls take() at render rate. A new store() overwrites any unread
/// frame — stale frames can never reach the display.
/// See ARCHITECTURE.md §3.1.
class FrameSlot {
 public:
  /// Write a frame (worker thread). Overwrites any previous unread frame.
  void store(int64_t timestamp_ns, CompositeFrame frame) {
    std::lock_guard lock(mutex_);
    frame_ = std::move(frame);
    timestamp_ns_ = timestamp_ns;
    has_new_ = true;
  }

  /// Read the latest frame (UI thread). Returns nullopt if no new frame since last take().
  std::optional<SlotResult> take() {
    std::lock_guard lock(mutex_);
    if (!has_new_) {
      return std::nullopt;
    }
    has_new_ = false;
    return SlotResult{timestamp_ns_, std::move(frame_)};
  }

 private:
  std::mutex mutex_;
  CompositeFrame frame_;
  int64_t timestamp_ns_ = 0;
  bool has_new_ = false;
};

}  // namespace PJ
