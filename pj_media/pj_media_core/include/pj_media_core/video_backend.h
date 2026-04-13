#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// Abstract interface for video playback backends.
///
/// Primary implementation: FfmpegBackend (delivers decoded frames via
/// FrameCallback, polled by processEvents on the main thread).
///
/// New code should use FileVideoSource (MediaSource adapter) rather
/// than VideoBackend directly. See ARCHITECTURE.md §5.2.
class VideoBackend {
 public:
  virtual ~VideoBackend() = default;

  VideoBackend(const VideoBackend&) = delete;
  VideoBackend& operator=(const VideoBackend&) = delete;
  VideoBackend(VideoBackend&&) = delete;
  VideoBackend& operator=(VideoBackend&&) = delete;

  /// Open a video file. Returns true on success.
  virtual bool open(const std::string& path) = 0;
  virtual void close() = 0;

  /// Seek to absolute position in seconds.
  virtual void seek(double seconds) = 0;
  virtual void setPaused(bool paused) = 0;
  [[nodiscard]] virtual bool isPaused() const = 0;

  /// Total duration in seconds (0 if unknown/live).
  [[nodiscard]] virtual double duration() const = 0;
  /// Current playback position in seconds.
  [[nodiscard]] virtual double position() const = 0;

  virtual void stepForward() = 0;
  virtual void stepBackward() = 0;

  using PositionCallback = std::function<void(double seconds)>;
  using DurationCallback = std::function<void(double seconds)>;
  using FileLoadedCallback = std::function<void()>;
  using FrameCallback = std::function<void(const DecodedFrame&)>;

  virtual void setPositionCallback(PositionCallback cb) = 0;
  virtual void setDurationCallback(DurationCallback cb) = 0;
  virtual void setFileLoadedCallback(FileLoadedCallback cb) = 0;
  virtual void setFrameCallback(FrameCallback cb) = 0;

  /// Process pending backend events (call from Qt event loop).
  virtual void processEvents() = 0;

 protected:
  VideoBackend() = default;
};

}  // namespace PJ
