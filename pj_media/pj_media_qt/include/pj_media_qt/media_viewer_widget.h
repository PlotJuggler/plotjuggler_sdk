#pragma once

#include <rhi/qrhi.h>

#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRhiWidget>
#include <QWheelEvent>
#include <mutex>

namespace PJ {

/// GPU-accelerated image viewer using QRhiWidget.
///
/// Renders decoded images via custom GLSL shaders (texture upload → GPU
/// display). Supports zoom (mouse wheel, cursor-anchored) and pan (mouse
/// drag) via a view transform matrix in the vertex shader — no CPU-side
/// pixel processing. See ARCHITECTURE.md §7.
///
/// Thread-safe: setFrame() may be called from any thread; the frame is
/// uploaded to the GPU on the next render tick.
class MediaViewerWidget : public QRhiWidget {
  Q_OBJECT

 public:
  explicit MediaViewerWidget(QWidget* parent = nullptr);

  /// Set the image to display. Thread-safe (mutex-protected).
  void setFrame(const QImage& img);

  /// Reset zoom to 1x and pan to origin.
  void resetView();

 signals:
  void zoomChanged(float zoom);

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;

  void wheelEvent(QWheelEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;

 private:
  [[nodiscard]] QMatrix4x4 buildViewTransform(QSize output_size) const;
  static QShader loadShader(const QString& path);

  bool initialized_ = false;
  QRhiGraphicsPipeline* pipeline_ = nullptr;
  QRhiBuffer* uniform_buf_ = nullptr;
  QRhiTexture* texture_ = nullptr;
  QRhiSampler* sampler_ = nullptr;
  QRhiShaderResourceBindings* srb_ = nullptr;

  std::mutex frame_mutex_;
  QImage pending_frame_;
  bool has_pending_ = false;
  int tex_width_ = 0;
  int tex_height_ = 0;
  float frame_aspect_ = 0.0f;

  float zoom_ = 1.0f;
  float pan_x_ = 0.0f;
  float pan_y_ = 0.0f;
  QPointF last_mouse_pos_;
};

}  // namespace PJ
