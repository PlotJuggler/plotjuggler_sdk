#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codecs.h"
#include "pj_media_core/image_pipeline_source.h"
#include "pj_media_qt/media_viewer_widget.h"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>

// ---------------------------------------------------------------------------
// McapObjectStoreLoader — loads MCAP image topic into ObjectStore
// ---------------------------------------------------------------------------

struct McapObjectStoreLoader {
  std::shared_ptr<mcap::McapReader> reader;
  PJ::ObjectTopicId topic_id{};
  size_t message_count = 0;
  std::string encoding;

  bool load(const std::string& path, PJ::ObjectStore& store, const std::string& target_topic = "") {
    reader = std::make_shared<mcap::McapReader>();
    auto status = reader->open(path);
    if (!status.ok()) {
      return false;
    }
    status = reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
    if (!status.ok()) {
      return false;
    }

    uint16_t target_chan = 0;
    std::string topic_name;

    for (const auto& [chan_id, chan_ptr] : reader->channels()) {
      if (chan_ptr == nullptr) {
        continue;
      }

      std::string schema_name;
      auto schemas = reader->schemas();
      auto schema_it = schemas.find(chan_ptr->schemaId);
      if (schema_it != schemas.end() && schema_it->second != nullptr) {
        schema_name = schema_it->second->name;
      }

      bool is_image = schema_name.find("CompressedImage") != std::string::npos;
      if (!is_image) {
        is_image = chan_ptr->topic.find("image") != std::string::npos &&
                   chan_ptr->topic.find("compressed") != std::string::npos;
      }
      if (!is_image) {
        continue;
      }
      if (chan_ptr->topic.find("Depth") != std::string::npos || chan_ptr->topic.find("depth") != std::string::npos) {
        continue;
      }

      if (!target_topic.empty() && chan_ptr->topic != target_topic) {
        continue;
      }

      target_chan = chan_id;
      topic_name = chan_ptr->topic;
      encoding = chan_ptr->messageEncoding;
      break;
    }

    if (target_chan == 0) {
      return false;
    }

    auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = topic_name, .metadata_json = "{}"});
    if (!id_or.has_value()) {
      return false;
    }
    topic_id = *id_or;

    auto local_reader = reader;
    auto local_topic = topic_name;

    mcap::ReadMessageOptions opts;
    opts.topicFilter = [&local_topic](std::string_view t) { return t == local_topic; };
    auto view = reader->readMessages([](const mcap::Status&) {}, opts);

    for (auto it = view.begin(); it != view.end(); ++it) {
      if (it->message.channelId != target_chan) {
        continue;
      }
      auto ts = static_cast<PJ::Timestamp>(it->message.logTime);

      // Push raw CDR bytes — the ImagePipelineSource's CdrJpeg pipeline
      // handles envelope stripping + JPEG decode at display time.
      store.pushLazy(topic_id, ts, [local_reader, local_topic, ts, target_chan]() -> std::vector<uint8_t> {
        mcap::ReadMessageOptions read_opts;
        read_opts.startTime = static_cast<mcap::Timestamp>(ts);
        read_opts.endTime = read_opts.startTime + 1;
        read_opts.topicFilter = [&local_topic](std::string_view t) { return t == local_topic; };
        auto v = local_reader->readMessages([](const mcap::Status&) {}, read_opts);
        for (auto vit = v.begin(); vit != v.end(); ++vit) {
          if (vit->message.channelId == target_chan) {
            const auto* d = reinterpret_cast<const uint8_t*>(vit->message.data);
            return {d, d + vit->message.dataSize};
          }
        }
        return {};
      });
      ++message_count;
    }
    return message_count > 0;
  }
};

// ---------------------------------------------------------------------------
// ImageViewerWindow
// ---------------------------------------------------------------------------

class ImageViewerWindow : public QMainWindow {
  Q_OBJECT

 public:
  void loadFile(const QString& path) {
    // Detach old source before destroying the store it points to
    image_widget_->setMediaSource(nullptr);
    source_.reset();

    setCursor(Qt::WaitCursor);
    store_ = std::make_unique<PJ::ObjectStore>();
    loader_ = McapObjectStoreLoader{};
    bool ok = loader_.load(path.toStdString(), *store_, "/camera/color/image_raw/compressed");
    if (!ok) {
      store_ = std::make_unique<PJ::ObjectStore>();
      loader_ = McapObjectStoreLoader{};
      ok = loader_.load(path.toStdString(), *store_);
    }
    setCursor(Qt::ArrowCursor);

    if (!ok || loader_.message_count == 0) {
      setWindowTitle("No image topic found");
      return;
    }

    // Create MediaSource with CDR+JPEG pipeline
    source_ = std::make_unique<PJ::ImagePipelineSource>(store_.get(), loader_.topic_id, PJ::makeCdrJpegPipeline());
    image_widget_->setMediaSource(source_.get());

    setWindowTitle(QString("Loaded %1 frames").arg(loader_.message_count));
    slider_->setRange(0, static_cast<int>(loader_.message_count - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    showFrame(0);
  }

  ImageViewerWindow() {
    setWindowTitle("MCAP Image Viewer (ObjectStore)");
    resize(900, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load MCAP", this);
    layout->addWidget(load_button_);

    // Bootstrap QRhiWidget
    auto* bootstrap = new PJ::MediaViewerWidget(this);
    bootstrap->setMaximumSize(0, 0);
    layout->addWidget(bootstrap);

    image_widget_ = new PJ::MediaViewerWidget(this);
    image_widget_->setMinimumSize(320, 240);
    image_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(image_widget_, 1);

    auto* controls = new QHBoxLayout();
    play_button_ = new QPushButton("\u25B6", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    time_label_ = new QLabel("0 / 0", this);
    time_label_->setFixedWidth(120);
    controls->addWidget(time_label_);
    layout->addLayout(controls);

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(33);

    throttle_timer_ = new QTimer(this);
    throttle_timer_->setSingleShot(true);
    throttle_.start();

    connect(load_button_, &QPushButton::clicked, this, &ImageViewerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &ImageViewerWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &ImageViewerWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &ImageViewerWindow::onTimerTick);
    connect(throttle_timer_, &QTimer::timeout, this, [this]() {
      showFrame(pending_index_);
      throttle_.restart();
    });
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
    if (next >= loader_.message_count) {
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
    if (!store_ || source_ == nullptr) {
      return;
    }
    auto entry = store_->at(loader_.topic_id, index);
    if (!entry.has_value()) {
      return;
    }

    image_widget_->setTimestamp(entry->timestamp);
    image_widget_->update();

    time_label_->setText(QString("%1 / %2").arg(index + 1).arg(loader_.message_count));
  }

  static constexpr int kMinFrameIntervalMs = 16;

  std::unique_ptr<PJ::ObjectStore> store_;
  McapObjectStoreLoader loader_;
  std::unique_ptr<PJ::ImagePipelineSource> source_;

  PJ::MediaViewerWidget* image_widget_ = nullptr;
  QSlider* slider_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QElapsedTimer throttle_;
  size_t pending_index_ = 0;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  ImageViewerWindow window;
  window.show();

  if (argc > 1) {
    window.loadFile(QString::fromUtf8(argv[1]));
  }

  return app.exec();
}

#include "mcap_image_viewer.moc"
