#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "pj_media_core/cancel_token.h"
#include "pj_media_core/ffmpeg_decoder.h"
#include "pj_media_core/thumbnail_cache.h"
#include "pj_media_core/video_backend.h"

struct AVFormatContext;

namespace PJ {

/// FFmpeg-based VideoBackend: demux + decode on a background thread,
/// deliver frames via FrameCallback. Does not use OpenGL — the
/// widget displays frames via MediaViewerWidget (QRhiWidget).
///
/// Ported from video_player_lab: pull-based FrameSlot pattern, with
/// the decode thread producing frames and the UI polling via callback.
class FfmpegBackend : public VideoBackend {
 public:
  FfmpegBackend();
  ~FfmpegBackend() override;

  bool open(const std::string& path) override;
  void close() override;

  void seek(double seconds) override;
  void setPaused(bool paused) override;
  [[nodiscard]] bool isPaused() const override;

  [[nodiscard]] double duration() const override;
  [[nodiscard]] double position() const override;

  void stepForward() override;
  void stepBackward() override;

  void setPositionCallback(PositionCallback cb) override;
  void setDurationCallback(DurationCallback cb) override;
  void setFileLoadedCallback(FileLoadedCallback cb) override;
  void setFrameCallback(FrameCallback cb) override;

  void processEvents() override;

  [[nodiscard]] bool rendersToFbo() const override {
    return false;
  }

 private:
  struct FrameIndex {
    int64_t pts;
    int64_t dts;
    bool keyframe;
  };

  void decodeThread();
  void decodeAndDeliver(int64_t target_pts, bool allow_partial, int64_t current_kf);
  void publishFrame(const DecodedFrame& frame);
  int64_t findKeyframeBefore(int64_t pts) const;

  AVFormatContext* fmt_ctx_ = nullptr;
  int video_stream_idx_ = -1;
  double time_base_ = 0.0;
  double duration_sec_ = 0.0;

  std::unique_ptr<FfmpegDecoder> decoder_;
  std::vector<FrameIndex> frame_index_;
  int64_t frame_interval_us_ = 33333;  // microseconds per frame (~30fps default)

  // BG thread
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{true};

  // Request mechanism: UI sets target, BG thread picks it up
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  int64_t requested_pts_ = -1;
  bool has_request_ = false;
  bool seek_pending_ = false;
  CancelTokenPtr cancel_token_;
  int64_t last_request_pts_ = -1;     // for direction-aware cancel-store
  int64_t last_decoded_pts_ = -1;     // decoder's current position (for forward threshold)
  double last_published_pos_ = -1.0;  // last position published — for direction-based partial filter
  bool scrub_backward_ = false;       // true when user is scrubbing backward (target < previous target)

  // Current state
  std::atomic<double> position_sec_{0.0};

  // Pending state for processEvents() delivery (latest-wins, like FrameSlot).
  // Decode thread writes under pending_mutex_; processEvents() reads and
  // fires callbacks on the caller's thread. No Qt event queue involved.
  std::mutex pending_mutex_;
  std::optional<double> pending_position_;
  bool pending_file_loaded_ = false;
  DecodedFrame pending_frame_;

  // Callbacks (fired from processEvents on caller's thread, not decode thread)
  PositionCallback on_position_;
  DurationCallback on_duration_;
  FileLoadedCallback on_file_loaded_;
  FrameCallback on_frame_;

  // Pre-decoded JPEG thumbnail cache for instant scrub feedback.
  // Built on a background thread at open() time, 1 frame per second.
  ThumbnailCache thumbnail_cache_;
  std::string file_path_;  // needed for cache rebuild
};

}  // namespace PJ
