#pragma once

#include <QMetaObject>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>

#include "pj_media_core/video_backend.h"

namespace PJ {

class MpvBackend;

/// QOpenGLWidget that renders video via a VideoBackend (currently libmpv).
///
/// The backend handles all decode, seek, caching, and HW-accel internally.
/// This widget owns the backend and renders each frame into its default
/// FBO. Position/duration changes are emitted as Qt signals.
///
/// The backend implementation is an internal detail — callers interact
/// through the VideoBackend abstract interface returned by backend().
class VideoViewerWidget : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit VideoViewerWidget(QWidget* parent = nullptr);
  ~VideoViewerWidget() override;

  VideoViewerWidget(const VideoViewerWidget&) = delete;
  VideoViewerWidget& operator=(const VideoViewerWidget&) = delete;
  VideoViewerWidget(VideoViewerWidget&&) = delete;
  VideoViewerWidget& operator=(VideoViewerWidget&&) = delete;

  /// Access the video backend for playback control (open, seek, pause, etc.).
  [[nodiscard]] VideoBackend* backend() const;

 signals:
  void positionChanged(double seconds);
  void durationChanged(double seconds);
  void fileLoaded();

 protected:
  void initializeGL() override;
  void paintGL() override;

 private:
  static void* getProcAddress(void* ctx, const char* name);
  static void onMpvUpdate(void* ctx);
  static void onMpvWakeup(void* ctx);

  std::unique_ptr<MpvBackend> backend_;
};

}  // namespace PJ
