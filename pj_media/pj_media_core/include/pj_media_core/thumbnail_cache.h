#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// Pre-decoded JPEG thumbnail cache for instant scrub feedback.
///
/// At open time, a background thread decodes 1 frame per second from the
/// video, downscales to max 1920 width, compresses to JPEG (~90KB/frame),
/// and stores in a sorted vector. During scrub, the nearest cached thumbnail
/// at-or-before the requested time is decompressed (~2ms) and returned.
///
/// Thread-safe: the build thread writes, the decode thread reads.
class ThumbnailCache {
 public:
  ThumbnailCache();
  ~ThumbnailCache();

  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;
  ThumbnailCache(ThumbnailCache&&) = delete;
  ThumbnailCache& operator=(ThumbnailCache&&) = delete;

  /// Start building the cache in a background thread.
  /// Returns immediately. Call stop() or destroy to cancel.
  void buildAsync(const std::string& video_path, double duration_sec);

  /// Stop the background build thread (if running).
  void stop();

  /// Look up the nearest cached frame at-or-before the given time.
  /// Returns a decompressed DecodedFrame, or nullopt if no cached frame.
  /// Thread-safe — can be called while buildAsync is still running.
  [[nodiscard]] std::optional<DecodedFrame> lookup(double seconds) const;

  /// Number of frames currently cached.
  [[nodiscard]] size_t size() const;

  /// Total JPEG memory used in bytes.
  [[nodiscard]] size_t memoryUsed() const;

  /// Is the background build still running?
  [[nodiscard]] bool isBuilding() const;

 private:
  struct CachedFrame {
    double timestamp = 0;
    std::vector<uint8_t> jpeg_data;
    int width = 0;
    int height = 0;
  };

  void buildThread(std::string video_path, double duration_sec);

  mutable std::mutex mutex_;
  std::vector<CachedFrame> frames_;  // sorted by timestamp
  size_t total_bytes_ = 0;

  std::thread thread_;
  std::atomic<bool> running_{false};
  void* tj_decompress_ = nullptr;  // tjhandle, cached for reuse across lookups

  static constexpr int kMaxWidth = 1920;
  static constexpr int kJpegQuality = 85;
};

}  // namespace PJ
