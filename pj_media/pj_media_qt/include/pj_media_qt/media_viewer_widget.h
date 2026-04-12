#pragma once

#include <rhi/qrhi.h>

#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRhiWidget>
#include <QWheelEvent>
#include <mutex>

#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// GPU-accelerated image/video viewer using QRhiWidget.
///
/// Supports two input modes:
/// - **YUV420P** (via DecodedFrame): 3-plane textures + BT.709 shader.
///   Used by FfmpegBackend and ThumbnailCache. No CPU color conversion.
/// - **RGB** (via QImage): single RGBA texture, shader passthrough.
///   Used by ImageDecoder (JPEG/PNG stills).
///
/// Zoom (mouse wheel, cursor-anchored) and pan (mouse drag) via a view
/// transform matrix in the vertex shader. See REQUIREMENTS.md §4.7.
///
/// Thread-safe: setFrame() may be called from any thread.
class MediaViewerWidget : public QRhiWidget {
  Q_OBJECT

 public:
  explicit MediaViewerWidget(QWidget* parent = nullptr);

  /// Set a decoded video frame (YUV420P preferred). Thread-safe.
  void setFrame(const DecodedFrame& frame);

  /// Set an RGB image (backward compat for image viewers). Thread-safe.
  void setFrame(const QImage& img);

  /// Reset zoom to 1x and pan to origin.
  void resetView();

 signals:
  void zoomChanged(float zoom);

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

  void wheelEvent(QWheelEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;

 private:
  [[nodiscard]] QMatrix4x4 buildViewTransform(QSize output_size) const;
  static QShader loadShader(const QString& path);

  // Pipeline for YUV→RGB shader (video frames)
  QRhi* rhi_cached_ = nullptr;
  QRhiGraphicsPipeline* pipeline_ = nullptr;
  QRhiBuffer* uniform_buf_ = nullptr;
  QRhiSampler* sampler_ = nullptr;
  QRhiShaderResourceBindings* srb_ = nullptr;

  // YUV420P: 3 separate R8 textures. RGBA: tex_y_ used as RGBA8.
  QRhiTexture* tex_y_ = nullptr;
  QRhiTexture* tex_u_ = nullptr;
  QRhiTexture* tex_v_ = nullptr;

  // Pending frame (set from any thread, uploaded on render tick)
  std::mutex frame_mutex_;
  DecodedFrame pending_decoded_;  // YUV420P frame
  QImage pending_qimage_;         // RGB fallback
  bool has_pending_ = false;
  bool pending_is_yuv_ = false;

  int tex_width_ = 0;
  int tex_height_ = 0;
  float frame_aspect_ = 0.0f;
  int current_pixel_format_ = 2;  // 0=YUV420P, 1=NV12, 2=RGBA

  float zoom_ = 1.0f;
  float pan_x_ = 0.0f;
  float pan_y_ = 0.0f;
  QPointF last_mouse_pos_;

  // Uniform buffer layout (std140):
  // mat4 viewTransform  (64 bytes, offset 0)
  // mat4 colorMatrix    (64 bytes, offset 64)
  // int  pixelFormat    (4 bytes, offset 128)
  // padding             (12 bytes)
  static constexpr int kUniformBufSize = 144;
};

}  // namespace PJ
