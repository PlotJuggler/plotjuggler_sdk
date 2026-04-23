#include "pj_media_core/streaming_video_source.h"

#include "pj_media_core/streaming_video_decoder.h"

namespace PJ {

StreamingVideoSource::StreamingVideoSource(ObjectStore* store, ObjectTopicId topic)
    : decoder_(std::make_unique<StreamingVideoDecoder>()) {
  decoder_->attach(store, topic);
  worker_ = std::thread(&StreamingVideoSource::workerLoop, this);
}

StreamingVideoSource::~StreamingVideoSource() {
  running_.store(false);
  request_cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void StreamingVideoSource::setTimestamp(int64_t ts_ns) {
  if (ts_ns == last_requested_ts_) {
    return;
  }
  last_requested_ts_ = ts_ns;

  {
    std::lock_guard lock(request_mutex_);
    requested_ts_ = ts_ns;
    has_request_ = true;
  }
  request_cv_.notify_one();
}

std::optional<DecodedFrame> StreamingVideoSource::takeFrame() {
  std::lock_guard lock(result_mutex_);
  if (!result_frame_.has_value()) {
    return std::nullopt;
  }
  auto frame = std::move(*result_frame_);
  result_frame_.reset();
  return frame;
}

bool StreamingVideoSource::isInitialized() const {
  return decoder_->isInitialized();
}

void StreamingVideoSource::workerLoop() {
  while (running_.load()) {
    int64_t ts = 0;
    {
      std::unique_lock lock(request_mutex_);
      request_cv_.wait(lock, [this] { return has_request_ || !running_.load(); });
      if (!running_.load()) {
        break;
      }
      ts = requested_ts_;
      has_request_ = false;
    }

    auto result = decoder_->decodeAt(ts);
    if (result.has_value() && !result->isNull()) {
      std::lock_guard lock(result_mutex_);
      result_frame_ = std::move(*result);
    }

    // Check for a newer request that arrived during decode
    {
      std::lock_guard lock(request_mutex_);
      if (has_request_) {
        continue;  // immediately process the newer request
      }
    }
  }
}

}  // namespace PJ
