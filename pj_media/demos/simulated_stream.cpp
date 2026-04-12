/// Simulated live stream demo: pushes synthetic JPEG frames into
/// ObjectStore at 30 Hz, displays the latest frame via MediaViewerWidget.
/// Pause freezes the buffer for scrub; resume returns to live edge.

#include <turbojpeg.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <memory>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/image_decoder.h"
#include "pj_media_qt/media_viewer_widget.h"

namespace {

/// Generate a synthetic JPEG: solid color that changes over time.
std::vector<uint8_t> makeSyntheticJpeg(int width, int height, int frame_num) {
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  auto r = static_cast<uint8_t>((frame_num * 3) % 256);
  auto g = static_cast<uint8_t>((frame_num * 7 + 80) % 256);
  auto b = static_cast<uint8_t>((frame_num * 13 + 160) % 256);
  for (size_t i = 0; i < rgb.size(); i += 3) {
    rgb[i] = r;
    rgb[i + 1] = g;
    rgb[i + 2] = b;
  }

  tjhandle compressor = tjInitCompress();
  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;  // NOLINT(google-runtime-int)
  tjCompress2(
      compressor, rgb.data(), width, width * 3, height, TJPF_RGB, &jpeg_buf, &jpeg_size, TJSAMP_420, 50,
      TJFLAG_FASTUPSAMPLE);
  std::vector<uint8_t> result(jpeg_buf, jpeg_buf + jpeg_size);
  tjFree(jpeg_buf);
  tjDestroy(compressor);
  return result;
}

}  // namespace

class StreamWindow : public QMainWindow {
  Q_OBJECT

 public:
  StreamWindow() {
    setWindowTitle("Simulated Stream (30 Hz)");
    resize(800, 650);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    // Bootstrap QRhiWidget for the window's backing store
    auto* bootstrap = new PJ::MediaViewerWidget(this);
    bootstrap->setMaximumSize(0, 0);
    layout->addWidget(bootstrap);

    viewer_ = new PJ::MediaViewerWidget(this);
    viewer_->setMinimumSize(320, 240);
    viewer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewer_, 1);

    auto* controls = new QHBoxLayout();
    live_button_ = new QPushButton("Live", this);
    live_button_->setCheckable(true);
    live_button_->setChecked(true);
    controls->addWidget(live_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    info_label_ = new QLabel("", this);
    info_label_->setFixedWidth(200);
    controls->addWidget(info_label_);
    layout->addLayout(controls);

    // ObjectStore with 3-second retention
    store_ = std::make_unique<PJ::ObjectStore>();
    auto id_or = store_->registerTopic({.dataset_id = 1, .topic_name = "sim/camera", .metadata_json = "{}"});
    topic_ = *id_or;
    store_->setRetentionBudget(topic_, {.time_window_ns = 3'000'000'000, .max_memory_bytes = 0});

    // Simulated camera: push a frame every 33 ms
    push_timer_ = new QTimer(this);
    push_timer_->setInterval(33);
    elapsed_.start();

    connect(push_timer_, &QTimer::timeout, this, &StreamWindow::onPushFrame);
    connect(live_button_, &QPushButton::toggled, this, &StreamWindow::onLiveToggled);
    connect(slider_, &QSlider::valueChanged, this, &StreamWindow::onSliderChanged);

    push_timer_->start();
  }

 private slots:
  void onPushFrame() {
    auto ts = static_cast<PJ::Timestamp>(elapsed_.nsecsElapsed());
    auto jpeg = makeSyntheticJpeg(640, 480, frame_num_++);
    store_->pushOwned(topic_, ts, std::move(jpeg));

    if (is_live_) {
      showLatest();
    }

    // Update slider range
    auto count = store_->entryCount(topic_);
    if (count > 0) {
      slider_->blockSignals(true);
      slider_->setRange(0, static_cast<int>(count - 1));
      if (is_live_) {
        slider_->setValue(static_cast<int>(count - 1));
      }
      slider_->blockSignals(false);
    }
  }

  void onLiveToggled(bool live) {
    is_live_ = live;
    slider_->setEnabled(!live);
    live_button_->setText(live ? "Live" : "Scrub");
    if (live) {
      showLatest();
    }
  }

  void onSliderChanged(int value) {
    if (!is_live_) {
      showIndex(static_cast<size_t>(value));
    }
  }

 private:
  void showLatest() {
    auto [t_min, t_max] = store_->timeRange(topic_);
    auto entry = store_->latestAt(topic_, t_max);
    if (!entry.has_value() || entry->data->empty()) {
      return;
    }
    decodeAndDisplay(*entry->data);
    auto count = store_->entryCount(topic_);
    info_label_->setText(
        QString("Live | %1 frames | %2 MB").arg(count).arg(store_->memoryUsage(topic_) / (1024 * 1024)));
  }

  void showIndex(size_t index) {
    auto entry = store_->at(topic_, index);
    if (!entry.has_value() || entry->data->empty()) {
      return;
    }
    decodeAndDisplay(*entry->data);
    auto count = store_->entryCount(topic_);
    info_label_->setText(QString("Scrub %1/%2").arg(index + 1).arg(count));
  }

  void decodeAndDisplay(const std::vector<uint8_t>& jpeg) {
    auto frame = decoder_.decodeJpeg(jpeg.data(), jpeg.size());
    if (!frame.has_value()) {
      return;
    }
    QImage img(frame->pixels->data(), frame->width, frame->height, frame->width * 3, QImage::Format_RGB888);
    viewer_->setFrame(img.copy());
  }

  std::unique_ptr<PJ::ObjectStore> store_;
  PJ::ObjectTopicId topic_{};
  PJ::ImageDecoder decoder_;
  int frame_num_ = 0;
  bool is_live_ = true;

  PJ::MediaViewerWidget* viewer_ = nullptr;
  QPushButton* live_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* info_label_ = nullptr;
  QTimer* push_timer_ = nullptr;
  QElapsedTimer elapsed_;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  StreamWindow window;
  window.show();
  return app.exec();
}

#include "simulated_stream.moc"
