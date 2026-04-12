#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codec_pipeline.h"
#include "pj_media_core/codecs.h"
#include "pj_media_qt/media_viewer_widget.h"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>

// ---------------------------------------------------------------------------
// Channel info — each channel has its own codec pipeline
// ---------------------------------------------------------------------------

struct ChannelView {
  PJ::ObjectTopicId topic_id{};
  std::string topic_name;
  std::unique_ptr<PJ::CodecPipeline> pipeline;
  PJ::MediaViewerWidget* widget = nullptr;
  size_t entry_count = 0;
};

// ---------------------------------------------------------------------------
// MultiChannelWindow
// ---------------------------------------------------------------------------

class MultiChannelWindow : public QMainWindow {
  Q_OBJECT

 public:
  MultiChannelWindow() {
    setWindowTitle("Multi-Channel Viewer");
    resize(1200, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* main_layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load MCAP", this);
    main_layout->addWidget(load_button_);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    main_layout->addWidget(splitter_, 1);

    // Bootstrap: a hidden QRhiWidget forces Qt to set up the RHI
    // backend for this window. Without this, dynamically added
    // QRhiWidgets fail to initialize ("No QRhi").
    auto* bootstrap = new PJ::MediaViewerWidget(splitter_);
    bootstrap->setMaximumSize(0, 0);
    splitter_->addWidget(bootstrap);

    auto* controls = new QHBoxLayout();
    play_button_ = new QPushButton("\u25B6", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    info_label_ = new QLabel("", this);
    info_label_->setFixedWidth(200);
    controls->addWidget(info_label_);

    main_layout->addLayout(controls);

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(33);

    throttle_timer_ = new QTimer(this);
    throttle_timer_->setSingleShot(true);
    throttle_.start();

    connect(load_button_, &QPushButton::clicked, this, &MultiChannelWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &MultiChannelWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &MultiChannelWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &MultiChannelWindow::onTimerTick);
    connect(throttle_timer_, &QTimer::timeout, this, [this]() {
      showFrame(pending_index_);
      throttle_.restart();
    });
  }

  void loadFile(const QString& path) {
    setCursor(Qt::WaitCursor);

    for (auto& ch : channels_) {
      delete ch.widget;
    }
    channels_.clear();
    image_chan_map_.clear();
    store_ = std::make_unique<PJ::ObjectStore>();

    auto reader = std::make_shared<mcap::McapReader>();
    if (!reader->open(path.toStdString()).ok()) {
      setCursor(Qt::ArrowCursor);
      setWindowTitle("Failed to open: " + path);
      return;
    }
    if (!reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) {
      setCursor(Qt::ArrowCursor);
      setWindowTitle("Failed to read summary");
      return;
    }

    // Discover image topics and assign codec pipelines
    for (const auto& [chan_id, chan_ptr] : reader->channels()) {
      if (chan_ptr == nullptr) {
        continue;
      }
      bool has_image = chan_ptr->topic.find("image") != std::string::npos;
      if (!has_image) {
        continue;
      }

      auto id_or = store_->registerTopic({.dataset_id = 1, .topic_name = chan_ptr->topic, .metadata_json = "{}"});
      if (!id_or.has_value()) {
        continue;
      }

      ChannelView ch;
      ch.topic_id = *id_or;
      ch.topic_name = chan_ptr->topic;

      // Pick codec pipeline based on topic name
      bool is_depth =
          chan_ptr->topic.find("depth") != std::string::npos || chan_ptr->topic.find("Depth") != std::string::npos;
      if (is_depth) {
        ch.pipeline = PJ::makeDepthPipeline();
      } else {
        ch.pipeline = PJ::makeJpegPipeline();
      }

      ch.widget = new PJ::MediaViewerWidget(splitter_);
      ch.widget->setMinimumSize(200, 150);
      splitter_->addWidget(ch.widget);

      image_chan_map_[chan_id] = channels_.size();
      channels_.push_back(std::move(ch));
    }

    // Push lazy entries
    mcap::ReadMessageOptions opts;
    auto view = reader->readMessages([](const mcap::Status&) {}, opts);

    for (auto it = view.begin(); it != view.end(); ++it) {
      auto map_it = image_chan_map_.find(it->message.channelId);
      if (map_it == image_chan_map_.end()) {
        continue;
      }

      auto& ch = channels_[map_it->second];
      auto ts = static_cast<PJ::Timestamp>(it->message.logTime);
      auto chan_id = it->message.channelId;

      // Push lazy entry that strips CDR at resolve time, storing only
      // the media payload in ObjectStore (JPEG bytes or depth PNG).
      // In the proper architecture, the DataSource/parser does this at
      // ingest time -- we do it here because v1 uses direct ingest.
      store_->pushLazy(ch.topic_id, ts, [reader, chan_id, ts]() -> std::vector<uint8_t> {
        mcap::ReadMessageOptions read_opts;
        read_opts.startTime = static_cast<mcap::Timestamp>(ts);
        read_opts.endTime = read_opts.startTime + 1;
        auto v = reader->readMessages([](const mcap::Status&) {}, read_opts);
        for (auto vit = v.begin(); vit != v.end(); ++vit) {
          if (vit->message.channelId != chan_id) {
            continue;
          }
          const auto* raw = reinterpret_cast<const uint8_t*>(vit->message.data);
          auto raw_size = vit->message.dataSize;

          // Strip CDR envelope: find the media payload (JPEG SOI or PNG sig)
          PJ::DecodedFrame frame;
          frame.pixels = std::make_shared<std::vector<uint8_t>>(raw, raw + raw_size);
          PJ::CdrImageStripper stripper;
          auto result = stripper.decode(frame);
          if (result.has_value() && !result->isNull()) {
            return std::move(*result->pixels);
          }
          return {raw, raw + raw_size};
        }
        return {};
      });
      ++ch.entry_count;
    }

    if (channels_.empty()) {
      setCursor(Qt::ArrowCursor);
      setWindowTitle("No image topics found");
      return;
    }

    size_t max_count = 0;
    QString title_parts;
    for (const auto& ch : channels_) {
      max_count = std::max(max_count, ch.entry_count);
      if (!title_parts.isEmpty()) {
        title_parts += " + ";
      }
      title_parts += QString::fromStdString(ch.topic_name) + " (" + QString::number(ch.entry_count) + ")";
    }

    slider_->setRange(0, static_cast<int>(max_count - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    max_index_ = max_count;

    setWindowTitle(title_parts);
    setCursor(Qt::ArrowCursor);
    showFrame(0);
    play_timer_->start();
    play_button_->setText("\u23F8");
  }

 private slots:
  void onLoad() {
    auto path = QFileDialog::getOpenFileName(this, "Open MCAP", QString(), "MCAP Files (*.mcap)");
    if (path.isEmpty()) {
      return;
    }
    loadFile(path);
  }

  void onPlayPause() {
    if (play_timer_->isActive()) {
      play_timer_->stop();
      play_button_->setText("\u25B6");
    } else {
      play_timer_->start();
      play_button_->setText("\u23F8");
    }
  }

  void onSliderChanged(int value) {
    if (!play_timer_->isActive()) {
      pending_index_ = static_cast<size_t>(value);
      if (throttle_.elapsed() >= kMinFrameIntervalMs) {
        showFrame(pending_index_);
        throttle_.restart();
      } else if (!throttle_timer_->isActive()) {
        throttle_timer_->start(kMinFrameIntervalMs - static_cast<int>(throttle_.elapsed()));
      }
    }
  }

  void onTimerTick() {
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= max_index_) {
      play_timer_->stop();
      play_button_->setText("\u25B6");
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

 private:
  void showFrame(size_t index) {
    for (auto& ch : channels_) {
      if (index >= ch.entry_count || ch.pipeline == nullptr) {
        continue;
      }
      auto entry = store_->at(ch.topic_id, index);
      if (!entry.has_value() || !entry->data || entry->data->empty()) {
        continue;
      }

      // Run the codec pipeline: envelope strip → decode → visualization
      auto frame_or = ch.pipeline->decode(entry->data->data(), entry->data->size());
      if (!frame_or.has_value()) {
        continue;
      }

      const auto& frame = *frame_or;
      if (frame.isNull() || frame.width == 0) {
        continue;
      }

      bool has_alpha = (frame.format == PJ::PixelFormat::kRGBA8888);
      int bpp = has_alpha ? 4 : 3;
      auto qt_fmt = has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
      QImage img(frame.pixels->data(), frame.width, frame.height, frame.width * bpp, qt_fmt);
      ch.widget->setFrame(img.copy());
    }

    info_label_->setText(QString("%1 / %2").arg(index + 1).arg(max_index_));
  }

  static constexpr int kMinFrameIntervalMs = 16;

  std::unique_ptr<PJ::ObjectStore> store_;
  std::vector<ChannelView> channels_;
  std::unordered_map<uint16_t, size_t> image_chan_map_;

  QSplitter* splitter_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* info_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QElapsedTimer throttle_;
  size_t pending_index_ = 0;
  size_t max_index_ = 0;
};

int main(int argc, char* argv[]) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  MultiChannelWindow window;
  window.show();

  if (argc > 1) {
    window.loadFile(QString::fromUtf8(argv[1]));
  }

  return app.exec();
}

#include "multi_channel_viewer.moc"
