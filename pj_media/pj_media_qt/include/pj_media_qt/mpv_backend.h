#pragma once

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <memory>
#include <string>

#include "pj_media_core/video_backend.h"

namespace PJ {

/// libmpv-based VideoBackend implementation.
///
/// Wraps mpv's C API for file playback with HW-accelerated decode,
/// seeking, and OpenGL FBO rendering. Handles all codec, container,
/// and HW-accel concerns internally. See ARCHITECTURE.md §4.1 and
/// video_player_lab/src/mpv_widget.cpp for the reference pattern.
class MpvBackend : public VideoBackend {
 public:
  MpvBackend();
  ~MpvBackend() override;

  MpvBackend(const MpvBackend&) = delete;
  MpvBackend& operator=(const MpvBackend&) = delete;
  MpvBackend(MpvBackend&&) = delete;
  MpvBackend& operator=(MpvBackend&&) = delete;

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

  void renderFrame(int fbo_id, int width, int height) override;
  void processEvents() override;

  /// Initialize the OpenGL render context. Must be called from a thread
  /// with a current GL context (typically in QOpenGLWidget::initializeGL).
  bool initRenderContext(void* (*get_proc_address)(void*, const char*), void* ctx = nullptr);

  [[nodiscard]] mpv_handle* mpvHandle() const {
    return mpv_;
  }
  [[nodiscard]] mpv_render_context* mpvRenderContext() const {
    return mpv_gl_;
  }

 private:
  mpv_handle* mpv_ = nullptr;
  mpv_render_context* mpv_gl_ = nullptr;

  PositionCallback on_position_;
  DurationCallback on_duration_;
  FileLoadedCallback on_file_loaded_;
};

}  // namespace PJ
