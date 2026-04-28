#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "pj_base/expected.hpp"
#include "pj_media_core/media_source.h"
#include "pj_media_core/video_backend.h"

namespace PJ {

class FfmpegBackend;

/// MediaSource for file-based video (MP4, MKV). Wraps FfmpegBackend,
/// which handles seeking, decode threading, thumbnail cache, and
/// direction-aware scrub internally.
///
/// The main thread calls setTimestamp() to seek and takeFrame() to
/// poll for decoded frames. takeFrame() calls processEvents()
/// internally, so no external poll timer is needed.
class FileVideoSource : public MediaSource {
 public:
  /// Open a video file. Returns error if open fails.
  static Expected<std::unique_ptr<FileVideoSource>> open(const std::string& path);

  ~FileVideoSource() override;

  void setTimestamp(int64_t ts_ns) override;
  std::optional<MediaFrame> takeFrame() override;

  // --- Additional API beyond MediaSource (for slider/transport UI) ---

  [[nodiscard]] double duration() const;  ///< Total duration in seconds
  [[nodiscard]] double position() const;  ///< Current playback position in seconds
  void setPaused(bool paused);            ///< Pause/resume playback
  [[nodiscard]] bool isPaused() const;    ///< True if paused (starts paused after open)
  void stepForward();                     ///< Advance by one frame
  void stepBackward();                    ///< Go back by one frame

  /// Callbacks fired from takeFrame() (via processEvents) on the main thread.
  void setPositionCallback(VideoBackend::PositionCallback cb);
  void setDurationCallback(VideoBackend::DurationCallback cb);
  void setFileLoadedCallback(VideoBackend::FileLoadedCallback cb);

 private:
  FileVideoSource();

  std::unique_ptr<FfmpegBackend> backend_;

  // Latest frame from the backend's FrameCallback
  std::mutex frame_mutex_;
  std::optional<DecodedFrame> pending_frame_;
};

}  // namespace PJ
