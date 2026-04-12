#include "pj_media_qt/media_viewer_widget.h"

#include <algorithm>

void pjMediaQtInitResources() {
  Q_INIT_RESOURCE(shaders);
}

namespace PJ {

MediaViewerWidget::MediaViewerWidget(QWidget* parent) : QRhiWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  static bool resources_initialized = [] {
    pjMediaQtInitResources();
    return true;
  }();
  (void)resources_initialized;
}

void MediaViewerWidget::setFrame(const QImage& img) {
  std::lock_guard lock(frame_mutex_);
  pending_frame_ = img;
  has_pending_ = true;
  update();
}

void MediaViewerWidget::resetView() {
  zoom_ = 1.0f;
  pan_x_ = 0.0f;
  pan_y_ = 0.0f;
  update();
}

void MediaViewerWidget::initialize(QRhiCommandBuffer* /*cb*/) {
  if (initialized_) {
    return;
  }

  auto* r = rhi();
  if (r == nullptr) {
    return;
  }

  auto vert = loadShader(":/shaders/texture.vert.qsb");
  auto frag = loadShader(":/shaders/texture.frag.qsb");
  if (!vert.isValid() || !frag.isValid()) {
    qWarning("MediaViewerWidget: failed to load shaders");
    return;
  }

  uniform_buf_ = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
  uniform_buf_->create();

  sampler_ = r->newSampler(
      QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
  sampler_->create();

  texture_ = r->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
  texture_->create();

  srb_ = r->newShaderResourceBindings();
  srb_->setBindings(
      {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniform_buf_),
       QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture_, sampler_)});
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
  initialized_ = true;
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
    if (has_pending_ && !pending_frame_.isNull()) {
      QImage img = pending_frame_.convertToFormat(QImage::Format_RGBA8888);
      QSize img_size = img.size();

      if (img_size != QSize(tex_width_, tex_height_)) {
        texture_->destroy();
        texture_->setPixelSize(img_size);
        texture_->create();

        srb_->destroy();
        srb_->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniform_buf_),
             QRhiShaderResourceBinding::sampledTexture(
                 1, QRhiShaderResourceBinding::FragmentStage, texture_, sampler_)});
        srb_->create();

        tex_width_ = img_size.width();
        tex_height_ = img_size.height();
      }

      QRhiTextureSubresourceUploadDescription sub_desc(img);
      updates->uploadTexture(texture_, QRhiTextureUploadDescription({0, 0, sub_desc}));

      has_pending_ = false;
      frame_aspect_ = static_cast<float>(tex_width_) / static_cast<float>(tex_height_);
    }
  }

  QMatrix4x4 view = buildViewTransform(output_size);
  updates->updateDynamicBuffer(uniform_buf_, 0, 64, view.constData());

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
