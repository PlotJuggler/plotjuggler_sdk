#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/media_source.h"

namespace PJ {

class StreamingVideoDecoder;

/// MediaSource for streaming video from ObjectStore. Wraps
/// StreamingVideoDecoder on a dedicated worker thread.
///
/// setTimestamp() posts a request to the worker; takeFrame() polls
/// for the latest decoded result. Latest-wins semantics — if a new
/// timestamp arrives while the worker is busy, it picks up the new
/// request after completing the current decode.
///
/// Thread safety: setTimestamp() and takeFrame() must be called from
/// the main thread only. The worker thread is internal.
///
/// Ownership: `store` is NOT owned (must outlive this object).
/// See ARCHITECTURE.md §5.3.
class StreamingVideoSource : public MediaSource {
 public:
  /// @param store  ObjectStore containing video entries (not owned)
  /// @param topic  Topic ID with H.264 VideoFrame entries
  StreamingVideoSource(ObjectStore* store, ObjectTopicId topic);
  ~StreamingVideoSource() override;

  StreamingVideoSource(const StreamingVideoSource&) = delete;
  StreamingVideoSource& operator=(const StreamingVideoSource&) = delete;
  StreamingVideoSource(StreamingVideoSource&&) = delete;
  StreamingVideoSource& operator=(StreamingVideoSource&&) = delete;

  void setTimestamp(int64_t ts_ns) override;
  std::optional<DecodedFrame> takeFrame() override;

  [[nodiscard]] bool isInitialized() const;

 private:
  void workerLoop();

  std::unique_ptr<StreamingVideoDecoder> decoder_;

  // Request channel (main → worker)
  std::mutex request_mutex_;
  std::condition_variable request_cv_;
  int64_t requested_ts_ = INT64_MIN;
  int64_t last_requested_ts_ = INT64_MIN;
  bool has_request_ = false;
  std::atomic<bool> running_{true};

  // Result channel (worker → main)
  std::mutex result_mutex_;
  std::optional<DecodedFrame> result_frame_;

  std::thread worker_;
};

}  // namespace PJ
