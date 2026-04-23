#include "pj_media_qt/media_viewer_widget.h"

#include <algorithm>
#include <cstring>

#include "pj_media_core/media_source.h"

void pjMediaQtInitResources() {
  Q_INIT_RESOURCE(shaders);
}

namespace PJ {

// BT.709 color matrix (HD video standard)
// clang-format off
static constexpr float kBT709[] = {
    1.0f,    1.0f,      1.0f,    0.0f,
    0.0f,   -0.18732f,  1.8556f, 0.0f,
    1.5748f, -0.46812f,  0.0f,   0.0f,
    0.0f,    0.0f,      0.0f,    1.0f
};
// clang-format on

MediaViewerWidget::MediaViewerWidget(QWidget* parent) : QRhiWidget(parent) {
  setApi(Api::OpenGL);
  setFocusPolicy(Qt::StrongFocus);
  static bool resources_initialized = [] {
    pjMediaQtInitResources();
    return true;
  }();
  (void)resources_initialized;
}

void MediaViewerWidget::setFrame(const DecodedFrame& frame) {
  if (frame.isNull()) {
    return;
  }
  std::lock_guard lock(frame_mutex_);
  pending_decoded_ = frame;
  pending_is_yuv_ = (frame.format == PixelFormat::kYUV420P);
  pending_qimage_ = QImage();  // clear any pending QImage
  has_pending_ = true;
  if (pipeline_ != nullptr) {
    update();
  }
}

void MediaViewerWidget::setFrame(const QImage& img) {
  std::lock_guard lock(frame_mutex_);
  pending_qimage_ = img;
  pending_is_yuv_ = false;
  pending_decoded_ = {};  // clear any pending DecodedFrame
  has_pending_ = true;
  if (pipeline_ != nullptr) {
    update();
  }
}

void MediaViewerWidget::resetView() {
  zoom_ = 1.0f;
  pan_x_ = 0.0f;
  pan_y_ = 0.0f;
  update();
}

void MediaViewerWidget::setMediaSource(MediaSource* source) {
  media_source_ = source;
}

void MediaViewerWidget::setTimestamp(int64_t ts_ns) {
  if (media_source_ != nullptr) {
    media_source_->setTimestamp(ts_ns);
  }
}

void MediaViewerWidget::releaseResources() {
  delete pipeline_;
  pipeline_ = nullptr;
  delete srb_;
  srb_ = nullptr;
  delete tex_y_;
  tex_y_ = nullptr;
  delete tex_u_;
  tex_u_ = nullptr;
  delete tex_v_;
  tex_v_ = nullptr;
  delete sampler_;
  sampler_ = nullptr;
  delete uniform_buf_;
  uniform_buf_ = nullptr;
  tex_width_ = 0;
  tex_height_ = 0;
}

void MediaViewerWidget::initialize(QRhiCommandBuffer* /*cb*/) {
  auto* r = rhi();
  if (r == nullptr) {
    return;
  }

  if (rhi_cached_ != r) {
    releaseResources();
    rhi_cached_ = r;
  }

  if (pipeline_ != nullptr) {
    return;
  }

  // Use YUV→RGB shader (handles both YUV and RGBA passthrough)
  auto vert = loadShader(":/shaders/yuv_to_rgb.vert.qsb");
  auto frag = loadShader(":/shaders/yuv_to_rgb.frag.qsb");
  if (!vert.isValid() || !frag.isValid()) {
    qWarning("MediaViewerWidget: failed to load yuv_to_rgb shaders");
    return;
  }

  // Uniform buffer: viewTransform (64) + colorMatrix (64) + pixelFormat (4) + padding (12) = 144
  uniform_buf_ = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufSize);
  uniform_buf_->create();

  sampler_ = r->newSampler(
      QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
  sampler_->create();

  // Create placeholder textures (1x1) — resized on first frame
  tex_y_ = r->newTexture(QRhiTexture::R8, QSize(1, 1));
  tex_y_->create();
  tex_u_ = r->newTexture(QRhiTexture::R8, QSize(1, 1));
  tex_u_->create();
  tex_v_ = r->newTexture(QRhiTexture::R8, QSize(1, 1));
  tex_v_->create();

  srb_ = r->newShaderResourceBindings();
  srb_->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(
          0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_buf_),
      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, tex_y_, sampler_),
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, tex_u_, sampler_),
      QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, tex_v_, sampler_),
  });
  srb_->create();

  pipeline_ = r->newGraphicsPipeline();
  pipeline_->setShaderStages(
      {QRhiShaderStage(QRhiShaderStage::Vertex, vert), QRhiShaderStage(QRhiShaderStage::Fragment, frag)});

  QRhiVertexInputLayout input_layout;
  pipeline_->setVertexInputLayout(input_layout);
  pipeline_->setShaderResourceBindings(srb_);
  pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
  if (!pipeline_->create()) {
    qWarning("MediaViewerWidget: failed to create graphics pipeline");
    pipeline_ = nullptr;
    return;
  }

  if (has_pending_) {
    update();
  }
}

void MediaViewerWidget::render(QRhiCommandBuffer* cb) {
  if (pipeline_ == nullptr) {
    return;
  }
  auto* r = rhi();
  if (r == nullptr) {
    return;
  }

  auto* rt = renderTarget();
  const QSize output_size = rt->pixelSize();
  QRhiResourceUpdateBatch* updates = r->nextResourceUpdateBatch();

  {
    std::lock_guard lock(frame_mutex_);

    // Poll MediaSource if attached
    if (media_source_ != nullptr) {
      auto frame = media_source_->takeFrame();
      if (frame && !frame->isNull()) {
        pending_decoded_ = std::move(*frame);
        pending_is_yuv_ = (pending_decoded_.format == PixelFormat::kYUV420P);
        pending_qimage_ = QImage();
        has_pending_ = true;
      }
    }

    if (has_pending_) {
      if (pending_is_yuv_ && !pending_decoded_.isNull()) {
        // YUV420P path: upload 3 planes to separate R8 textures
        int w = pending_decoded_.width;
        int h = pending_decoded_.height;
        int uv_w = (w + 1) / 2;
        int uv_h = (h + 1) / 2;
        const uint8_t* pixel_data = pending_decoded_.pixels->data();
        int y_size = w * h;
        int uv_size = uv_w * uv_h;

        if (w != tex_width_ || h != tex_height_ || current_pixel_format_ != 0) {
          // Recreate textures at correct size
          tex_y_->destroy();
          tex_y_->setFormat(QRhiTexture::R8);
          tex_y_->setPixelSize(QSize(w, h));
          tex_y_->create();

          tex_u_->destroy();
          tex_u_->setFormat(QRhiTexture::R8);
          tex_u_->setPixelSize(QSize(uv_w, uv_h));
          tex_u_->create();

          tex_v_->destroy();
          tex_v_->setFormat(QRhiTexture::R8);
          tex_v_->setPixelSize(QSize(uv_w, uv_h));
          tex_v_->create();

          // Rebuild SRB with new textures
          srb_->destroy();
          srb_->setBindings({
              QRhiShaderResourceBinding::uniformBuffer(
                  0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_buf_),
              QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, tex_y_, sampler_),
              QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, tex_u_, sampler_),
              QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, tex_v_, sampler_),
          });
          srb_->create();

          tex_width_ = w;
          tex_height_ = h;
          current_pixel_format_ = 0;
        }

        // Upload Y plane
        QRhiTextureSubresourceUploadDescription y_desc(pixel_data, y_size);
        y_desc.setSourceSize(QSize(w, h));
        updates->uploadTexture(tex_y_, QRhiTextureUploadDescription({0, 0, y_desc}));

        // Upload U plane
        QRhiTextureSubresourceUploadDescription u_desc(pixel_data + y_size, uv_size);
        u_desc.setSourceSize(QSize(uv_w, uv_h));
        updates->uploadTexture(tex_u_, QRhiTextureUploadDescription({0, 0, u_desc}));

        // Upload V plane
        QRhiTextureSubresourceUploadDescription v_desc(pixel_data + y_size + uv_size, uv_size);
        v_desc.setSourceSize(QSize(uv_w, uv_h));
        updates->uploadTexture(tex_v_, QRhiTextureUploadDescription({0, 0, v_desc}));

        frame_aspect_ = static_cast<float>(w) / static_cast<float>(h);

      } else if (!pending_is_yuv_ && !pending_decoded_.isNull()) {
        // RGB/RGBA DecodedFrame path: convert to RGBA8 and upload as single texture
        int w = pending_decoded_.width;
        int h = pending_decoded_.height;
        const uint8_t* src = pending_decoded_.pixels->data();
        size_t src_size = pending_decoded_.pixels->size();

        // Convert to RGBA8888 for GPU upload
        std::vector<uint8_t> rgba_buf;
        const uint8_t* rgba_data = nullptr;
        size_t rgba_size = 0;

        bool is_bgr =
            (pending_decoded_.format == PixelFormat::kBGR888 || pending_decoded_.format == PixelFormat::kBGRA8888);

        if (pending_decoded_.format == PixelFormat::kRGBA8888) {
          rgba_data = src;
          rgba_size = src_size;
        } else if (pending_decoded_.format == PixelFormat::kBGRA8888) {
          // BGRA→RGBA: swap R and B channels
          rgba_buf.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
          int pixel_count = w * h;
          for (int i = 0; i < pixel_count; ++i) {
            rgba_buf[i * 4 + 0] = src[i * 4 + 2];  // R ← B
            rgba_buf[i * 4 + 1] = src[i * 4 + 1];  // G
            rgba_buf[i * 4 + 2] = src[i * 4 + 0];  // B ← R
            rgba_buf[i * 4 + 3] = src[i * 4 + 3];  // A
          }
          rgba_data = rgba_buf.data();
          rgba_size = rgba_buf.size();
        } else if (pending_decoded_.format == PixelFormat::kRGB888 || pending_decoded_.format == PixelFormat::kBGR888) {
          // RGB/BGR→RGBA: insert alpha=255, swap R/B if BGR
          rgba_buf.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
          int pixel_count = w * h;
          for (int i = 0; i < pixel_count; ++i) {
            rgba_buf[i * 4 + 0] = src[i * 3 + (is_bgr ? 2 : 0)];
            rgba_buf[i * 4 + 1] = src[i * 3 + 1];
            rgba_buf[i * 4 + 2] = src[i * 3 + (is_bgr ? 0 : 2)];
            rgba_buf[i * 4 + 3] = 255;
          }
          rgba_data = rgba_buf.data();
          rgba_size = rgba_buf.size();
        }

        if (rgba_data != nullptr) {
          if (w != tex_width_ || h != tex_height_ || current_pixel_format_ != 2) {
            tex_y_->destroy();
            tex_y_->setFormat(QRhiTexture::RGBA8);
            tex_y_->setPixelSize(QSize(w, h));
            tex_y_->create();

            srb_->destroy();
            srb_->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_buf_),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage, tex_y_, sampler_),
                QRhiShaderResourceBinding::sampledTexture(
                    2, QRhiShaderResourceBinding::FragmentStage, tex_u_, sampler_),
                QRhiShaderResourceBinding::sampledTexture(
                    3, QRhiShaderResourceBinding::FragmentStage, tex_v_, sampler_),
            });
            srb_->create();

            tex_width_ = w;
            tex_height_ = h;
            current_pixel_format_ = 2;
          }

          QRhiTextureSubresourceUploadDescription sub_desc(rgba_data, static_cast<quint32>(rgba_size));
          sub_desc.setSourceSize(QSize(w, h));
          updates->uploadTexture(tex_y_, QRhiTextureUploadDescription({0, 0, sub_desc}));
          frame_aspect_ = static_cast<float>(w) / static_cast<float>(h);
        }

      } else if (!pending_qimage_.isNull()) {
        // QImage path (backward compat)
        QImage img = pending_qimage_.convertToFormat(QImage::Format_RGBA8888);
        QSize img_size = img.size();

        if (img_size.width() != tex_width_ || img_size.height() != tex_height_ || current_pixel_format_ != 2) {
          tex_y_->destroy();
          tex_y_->setFormat(QRhiTexture::RGBA8);
          tex_y_->setPixelSize(img_size);
          tex_y_->create();

          srb_->destroy();
          srb_->setBindings({
              QRhiShaderResourceBinding::uniformBuffer(
                  0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_buf_),
              QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, tex_y_, sampler_),
              QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, tex_u_, sampler_),
              QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, tex_v_, sampler_),
          });
          srb_->create();

          tex_width_ = img_size.width();
          tex_height_ = img_size.height();
          current_pixel_format_ = 2;
        }

        QRhiTextureSubresourceUploadDescription sub_desc(img);
        updates->uploadTexture(tex_y_, QRhiTextureUploadDescription({0, 0, sub_desc}));
        frame_aspect_ = static_cast<float>(tex_width_) / static_cast<float>(tex_height_);
      }

      has_pending_ = false;
    }
  }

  // Update uniforms
  QMatrix4x4 view = buildViewTransform(output_size);
  updates->updateDynamicBuffer(uniform_buf_, 0, 64, view.constData());
  updates->updateDynamicBuffer(uniform_buf_, 64, 64, kBT709);
  int32_t fmt = current_pixel_format_;
  updates->updateDynamicBuffer(uniform_buf_, 128, 4, &fmt);

  cb->beginPass(rt, QColor::fromRgbF(0.0f, 0.0f, 0.0f, 1.0f), {1.0f, 0}, updates);
  cb->setGraphicsPipeline(pipeline_);
  cb->setViewport(
      QRhiViewport(0, 0, static_cast<float>(output_size.width()), static_cast<float>(output_size.height())));
  cb->setShaderResources(srb_);
  cb->draw(3);
  cb->endPass();
}

void MediaViewerWidget::wheelEvent(QWheelEvent* e) {
  float old_zoom = zoom_;
  float delta = e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f;
  zoom_ = std::clamp(zoom_ * delta, 1.0f, 20.0f);

  if (zoom_ <= 1.0f) {
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
  } else {
    float mx = (2.0f * static_cast<float>(e->position().x()) / static_cast<float>(width()) - 1.0f);
    float my = (2.0f * static_cast<float>(e->position().y()) / static_cast<float>(height()) - 1.0f);
    pan_x_ += mx * (1.0f / zoom_ - 1.0f / old_zoom);
    pan_y_ += my * (1.0f / zoom_ - 1.0f / old_zoom);
  }

  update();
  emit zoomChanged(zoom_);
  e->accept();
}

void MediaViewerWidget::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && zoom_ > 1.0f) {
    last_mouse_pos_ = e->position();
    e->accept();
  }
}

void MediaViewerWidget::mouseMoveEvent(QMouseEvent* e) {
  if ((e->buttons() & Qt::LeftButton) != 0 && zoom_ > 1.0f) {
    auto dx = static_cast<float>(e->position().x() - last_mouse_pos_.x()) / static_cast<float>(width()) * 2.0f / zoom_;
    auto dy = static_cast<float>(e->position().y() - last_mouse_pos_.y()) / static_cast<float>(height()) * 2.0f / zoom_;
    pan_x_ += dx;
    pan_y_ -= dy;
    last_mouse_pos_ = e->position();
    update();
    e->accept();
  }
}

void MediaViewerWidget::mouseDoubleClickEvent(QMouseEvent* e) {
  resetView();
  e->accept();
}

QMatrix4x4 MediaViewerWidget::buildViewTransform(QSize output_size) const {
  QMatrix4x4 m;
  float widget_aspect = static_cast<float>(output_size.width()) / static_cast<float>(output_size.height());
  float sx = 1.0f;
  float sy = 1.0f;
  if (frame_aspect_ > 0.0f) {
    if (widget_aspect > frame_aspect_) {
      sx = frame_aspect_ / widget_aspect;
    } else {
      sy = widget_aspect / frame_aspect_;
    }
  }
  m.scale(sx * zoom_, sy * zoom_);
  m.translate(pan_x_, pan_y_);
  return m;
}

QShader MediaViewerWidget::loadShader(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning("Failed to load shader: %s", qPrintable(path));
    return {};
  }
  return QShader::fromSerialized(f.readAll());
}

}  // namespace PJ
