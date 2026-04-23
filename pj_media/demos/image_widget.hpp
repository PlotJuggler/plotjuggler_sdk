#pragma once

#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>

class ImageWidget : public QLabel {
  Q_OBJECT

 public:
  explicit ImageWidget(QWidget* parent = nullptr) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background-color: black;");
  }

  void setFrame(const QImage& img) {
    source_ = img;
    updateDisplay();
  }

 protected:
  void resizeEvent(QResizeEvent* e) override {
    QLabel::resizeEvent(e);
    updateDisplay();
  }

  void wheelEvent(QWheelEvent* e) override {
    float delta = e->angleDelta().y() > 0 ? 1.15f : 1.0f / 1.15f;
    zoom_ = std::clamp(zoom_ * delta, 1.0f, 20.0f);
    if (zoom_ <= 1.0f) {
      pan_x_ = 0;
      pan_y_ = 0;
    }
    updateDisplay();
    e->accept();
  }

  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() == Qt::LeftButton && zoom_ > 1.0f) {
      last_pos_ = e->pos();
      e->accept();
    }
  }

  void mouseMoveEvent(QMouseEvent* e) override {
    if ((e->buttons() & Qt::LeftButton) && zoom_ > 1.0f) {
      pan_x_ += e->pos().x() - last_pos_.x();
      pan_y_ += e->pos().y() - last_pos_.y();
      last_pos_ = e->pos();
      updateDisplay();
      e->accept();
    }
  }

  void mouseDoubleClickEvent(QMouseEvent* e) override {
    zoom_ = 1.0f;
    pan_x_ = 0;
    pan_y_ = 0;
    updateDisplay();
    e->accept();
  }

 private:
  void updateDisplay() {
    if (source_.isNull()) {
      return;
    }
    QSize target = size();
    float aspect = static_cast<float>(source_.width()) / static_cast<float>(source_.height());
    float widget_aspect = static_cast<float>(target.width()) / static_cast<float>(target.height());

    int scaled_w = 0;
    int scaled_h = 0;
    if (widget_aspect > aspect) {
      scaled_h = static_cast<int>(static_cast<float>(target.height()) * zoom_);
      scaled_w = static_cast<int>(static_cast<float>(scaled_h) * aspect);
    } else {
      scaled_w = static_cast<int>(static_cast<float>(target.width()) * zoom_);
      scaled_h = static_cast<int>(static_cast<float>(scaled_w) / aspect);
    }

    QImage scaled = source_.scaled(scaled_w, scaled_h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QImage output(target, QImage::Format_RGB888);
    output.fill(Qt::black);

    int x = (target.width() - scaled_w) / 2 + pan_x_;
    int y = (target.height() - scaled_h) / 2 + pan_y_;

    QPainter painter(&output);
    painter.drawImage(x, y, scaled);
    painter.end();

    setPixmap(QPixmap::fromImage(output));
  }

  QImage source_;
  float zoom_ = 1.0f;
  int pan_x_ = 0;
  int pan_y_ = 0;
  QPoint last_pos_;
};
