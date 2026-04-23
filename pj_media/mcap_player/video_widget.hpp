#pragma once

#include <rhi/qrhi.h>

#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRhiWidget>
#include <QWheelEvent>
#include <mutex>

class VideoWidget : public QRhiWidget {
  Q_OBJECT

 public:
  explicit VideoWidget(QWidget* parent = nullptr) : QRhiWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
  }

  void setFrame(const QImage& img) {
    std::lock_guard lock(frame_mutex_);
    pending_frame_ = img;
    has_pending_ = true;
    update();
  }

 protected:
  void initialize(QRhiCommandBuffer* /*cb*/) override {
    if (initialized_) {
      return;
    }

    auto* rhi = this->rhi();
    if (!rhi) {
      return;
    }

    auto vert = loadShader(":/shaders/texture.vert.qsb");
    auto frag = loadShader(":/shaders/texture.frag.qsb");
    if (!vert.isValid() || !frag.isValid()) {
      qWarning("VideoWidget: failed to load shaders");
      return;
    }

    uniform_buf_ = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
    uniform_buf_->create();

    sampler_ = rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
        QRhiSampler::ClampToEdge);
    sampler_->create();

    texture_ = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    texture_->create();

    srb_ = rhi->newShaderResourceBindings();
    srb_->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniform_buf_),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture_, sampler_)});
    srb_->create();

    pipeline_ = rhi->newGraphicsPipeline();
    pipeline_->setShaderStages(
        {QRhiShaderStage(QRhiShaderStage::Vertex, vert), QRhiShaderStage(QRhiShaderStage::Fragment, frag)});

    QRhiVertexInputLayout inputLayout;
    pipeline_->setVertexInputLayout(inputLayout);
    pipeline_->setShaderResourceBindings(srb_);
    pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!pipeline_->create()) {
      qWarning("VideoWidget: failed to create graphics pipeline");
      pipeline_ = nullptr;
      return;
    }
    initialized_ = true;
  }

  void render(QRhiCommandBuffer* cb) override {
    if (!pipeline_) {
      return;
    }
    auto* rhi = this->rhi();
    if (!rhi) {
      return;
    }

    auto* rt = renderTarget();
    const QSize output_size = rt->pixelSize();
    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();

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
    cb->setViewport(QRhiViewport(0, 0, output_size.width(), output_size.height()));
    cb->setShaderResources(srb_);
    cb->draw(3);
    cb->endPass();
  }

  void wheelEvent(QWheelEvent* e) override {
    float old_zoom = zoom_;
    float delta = e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f;
    zoom_ = std::clamp(zoom_ * delta, 1.0f, 20.0f);

    if (zoom_ <= 1.0f) {
      pan_x_ = 0.0f;
      pan_y_ = 0.0f;
    } else {
      float mx = (2.0f * e->position().x() / width() - 1.0f);
      float my = (2.0f * e->position().y() / height() - 1.0f);
      pan_x_ += mx * (1.0f / zoom_ - 1.0f / old_zoom);
      pan_y_ += my * (1.0f / zoom_ - 1.0f / old_zoom);
    }

    update();
    e->accept();
  }

  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() == Qt::LeftButton && zoom_ > 1.0f) {
      last_mouse_pos_ = e->position();
      e->accept();
    }
  }

  void mouseMoveEvent(QMouseEvent* e) override {
    if ((e->buttons() & Qt::LeftButton) && zoom_ > 1.0f) {
      float dx = (e->position().x() - last_mouse_pos_.x()) / width() * 2.0f / zoom_;
      float dy = (e->position().y() - last_mouse_pos_.y()) / height() * 2.0f / zoom_;
      pan_x_ += dx;
      pan_y_ -= dy;
      last_mouse_pos_ = e->position();
      update();
      e->accept();
    }
  }

  void mouseDoubleClickEvent(QMouseEvent* e) override {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    update();
    e->accept();
  }

 private:
  QMatrix4x4 buildViewTransform(QSize output_size) {
    QMatrix4x4 m;
    float widget_aspect = static_cast<float>(output_size.width()) / static_cast<float>(output_size.height());
    float sx = 1.0f, sy = 1.0f;
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

  static QShader loadShader(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      qWarning("Failed to load shader: %s", qPrintable(path));
      return {};
    }
    return QShader::fromSerialized(f.readAll());
  }

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
