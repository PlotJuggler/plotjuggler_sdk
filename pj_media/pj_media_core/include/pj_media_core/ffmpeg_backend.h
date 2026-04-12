#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pj_media_core/ffmpeg_decoder.h"
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
  void decodeAndDeliver(int64_t target_pts);
  int64_t findKeyframeBefore(int64_t pts) const;

  AVFormatContext* fmt_ctx_ = nullptr;
  int video_stream_idx_ = -1;
  double time_base_ = 0.0;
  double duration_sec_ = 0.0;

  std::unique_ptr<FfmpegDecoder> decoder_;
  std::vector<FrameIndex> frame_index_;

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

  // Current state
  std::atomic<double> position_sec_{0.0};

  // Callbacks
  PositionCallback on_position_;
  DurationCallback on_duration_;
  FileLoadedCallback on_file_loaded_;
  FrameCallback on_frame_;
};

}  // namespace PJ
