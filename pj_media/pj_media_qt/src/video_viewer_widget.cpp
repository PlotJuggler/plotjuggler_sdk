#include "pj_media_qt/video_viewer_widget.h"

#include <QOpenGLContext>

#include "pj_media_qt/mpv_backend.h"

namespace PJ {

VideoViewerWidget::VideoViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
  backend_ = std::make_unique<MpvBackend>();

  backend_->setPositionCallback([this](double s) {
    QMetaObject::invokeMethod(this, [this, s]() { emit positionChanged(s); }, Qt::QueuedConnection);
  });
  backend_->setDurationCallback([this](double s) {
    QMetaObject::invokeMethod(this, [this, s]() { emit durationChanged(s); }, Qt::QueuedConnection);
  });
  backend_->setFileLoadedCallback(
      [this]() { QMetaObject::invokeMethod(this, [this]() { emit fileLoaded(); }, Qt::QueuedConnection); });
}

VideoViewerWidget::~VideoViewerWidget() {
  makeCurrent();
  backend_.reset();
  doneCurrent();
}

void* VideoViewerWidget::getProcAddress(void* /*ctx*/, const char* name) {
  QOpenGLContext* gl = QOpenGLContext::currentContext();
  if (gl == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
}

void VideoViewerWidget::initializeGL() {
  if (!backend_->initRenderContext(getProcAddress)) {
    qWarning("VideoViewerWidget: failed to create mpv render context");
    return;
  }

  mpv_render_context_set_update_callback(backend_->mpvRenderContext(), onMpvUpdate, this);
  mpv_set_wakeup_callback(backend_->mpvHandle(), onMpvWakeup, this);
}

void VideoViewerWidget::paintGL() {
  backend_->renderFrame(static_cast<int>(defaultFramebufferObject()), width(), height());
}

void VideoViewerWidget::onMpvUpdate(void* ctx) {
  QMetaObject::invokeMethod(static_cast<VideoViewerWidget*>(ctx), "update", Qt::QueuedConnection);
}

void VideoViewerWidget::onMpvWakeup(void* ctx) {
  QMetaObject::invokeMethod(
      static_cast<VideoViewerWidget*>(ctx),
      [ctx]() { static_cast<VideoViewerWidget*>(ctx)->backend_->processEvents(); }, Qt::QueuedConnection);
}

VideoBackend* VideoViewerWidget::backend() const {
  return backend_.get();
}

}  // namespace PJ
