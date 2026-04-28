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
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codecs.h"
#include "pj_media_core/composite_media_source.h"
#include "pj_media_core/image_pipeline_source.h"
#include "pj_media_core/scene_decoder.h"
#include "pj_media_core/scene_pipeline_source.h"
#include "pj_media_qt/media_viewer_widget.h"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>

#include "mcap_helpers.hpp"

// ---------------------------------------------------------------------------
// McapTopicLoader — generic two-phase async loader for ONE MCAP topic.
//
// open() runs synchronously: opens summary, finds a topic that matches a
// caller-supplied predicate, registers it in the ObjectStore, and spawns a
// background indexer thread that pushes one lazy closure per message.
//
// Two reader instances per loader: index_reader (touched only by the indexer
// thread) and fetch_reader (shared by all closure executions, guarded by
// fetch_mutex). Required because mcap::McapReader is not thread-safe across
// concurrent readMessages calls, and each instance needs its own
// readSummary() for fast chunk lookup.
// ---------------------------------------------------------------------------

/// Predicate returning true if the channel/schema is the one to load.
using TopicMatchFn = std::function<bool(const mcap::Channel&, const mcap::Schema&)>;

struct McapTopicLoader {
  std::shared_ptr<mcap::McapReader> index_reader;
  std::shared_ptr<mcap::McapReader> fetch_reader;
  std::shared_ptr<std::mutex> fetch_mutex;

  PJ::ObjectTopicId topic_id{};
  size_t expected_count = 0;
  std::string topic_name;
  std::string schema_name;  ///< populated from the matched channel for caller use

  std::atomic<size_t> indexed_count{0};
  std::atomic<bool> stop_requested{false};
  std::thread indexer_thread;

  McapTopicLoader() = default;
  McapTopicLoader(const McapTopicLoader&) = delete;
  McapTopicLoader& operator=(const McapTopicLoader&) = delete;

  ~McapTopicLoader() {
    stop_requested.store(true, std::memory_order_relaxed);
    if (indexer_thread.joinable()) {
      indexer_thread.join();
    }
  }

  bool open(const std::string& path, PJ::ObjectStore& store, const TopicMatchFn& match) {
    auto summary_reader = std::make_shared<mcap::McapReader>();
    if (!summary_reader->open(path).ok()) return false;
    if (!summary_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) return false;

    uint16_t target_chan = 0;
    for (const auto& [chan_id, chan_ptr] : summary_reader->channels()) {
      if (chan_ptr == nullptr) continue;
      auto schemas = summary_reader->schemas();
      auto schema_it = schemas.find(chan_ptr->schemaId);
      if (schema_it == schemas.end() || schema_it->second == nullptr) continue;
      if (!match(*chan_ptr, *schema_it->second)) continue;

      target_chan = chan_id;
      topic_name = chan_ptr->topic;
      schema_name = schema_it->second->name;
      break;
    }
    if (target_chan == 0) return false;

    if (auto stats = summary_reader->statistics(); stats.has_value()) {
      auto count_it = stats->channelMessageCounts.find(target_chan);
      if (count_it != stats->channelMessageCounts.end()) {
        expected_count = static_cast<size_t>(count_it->second);
      }
    }

    auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = topic_name, .metadata_json = "{}"});
    if (!id_or.has_value()) return false;
    topic_id = *id_or;

    index_reader = std::make_shared<mcap::McapReader>();
    if (!index_reader->open(path).ok()) return false;
    if (!index_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) return false;
    fetch_reader = std::make_shared<mcap::McapReader>();
    if (!fetch_reader->open(path).ok()) return false;
    if (!fetch_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) return false;
    fetch_mutex = std::make_shared<std::mutex>();

    indexer_thread = std::thread(&McapTopicLoader::indexLoop, this, std::ref(store), target_chan);
    return true;
  }

  [[nodiscard]] bool isIndexing() const {
    return indexed_count.load(std::memory_order_relaxed) < expected_count;
  }

 private:
  void indexLoop(PJ::ObjectStore& store, uint16_t target_chan) {
    mcap::ReadMessageOptions opts;
    opts.topicFilter = [this](std::string_view t) { return t == topic_name; };
    // ObjectStore::pushLazy requires monotonically non-decreasing timestamps.
    // mcap's default readOrder (FileOrder) does NOT sort by log time and can
    // surface out-of-order messages on bags where chunks aren't laid out
    // chronologically (rosbag2 message-mode compressed bags exhibit this).
    opts.readOrder = mcap::ReadMessageOptions::ReadOrder::LogTimeOrder;
    auto view = index_reader->readMessages([](const mcap::Status&) {}, opts);

    for (auto it = view.begin(); it != view.end(); ++it) {
      if (stop_requested.load(std::memory_order_relaxed)) return;
      if (it->message.channelId != target_chan) continue;
      auto ts = static_cast<PJ::Timestamp>(it->message.logTime);

      auto status = store.pushLazy(
          topic_id, ts,
          [reader = fetch_reader, mtx = fetch_mutex, topic = topic_name, ts, target_chan]()
              -> std::vector<uint8_t> {
            std::lock_guard<std::mutex> lock(*mtx);
            mcap::ReadMessageOptions read_opts;
            read_opts.startTime = static_cast<mcap::Timestamp>(ts);
            read_opts.endTime = read_opts.startTime + 1;
            read_opts.topicFilter = [&topic](std::string_view t) { return t == topic; };
            auto v = reader->readMessages([](const mcap::Status&) {}, read_opts);
            for (auto vit = v.begin(); vit != v.end(); ++vit) {
              if (vit->message.channelId == target_chan) {
                const auto* d = reinterpret_cast<const uint8_t*>(vit->message.data);
                return pj_demos::maybeDecompressZstd({d, d + vit->message.dataSize});
              }
            }
            return {};
          });

      if (status.has_value()) {
        indexed_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
};

// Topic-discovery predicates. Each looks at one channel/schema in the MCAP
// summary and decides whether it's the topic of interest.
namespace {
TopicMatchFn imageMatchFn(const std::string& target_topic) {
  return [target_topic](const mcap::Channel& chan, const mcap::Schema& schema) {
    bool is_image = schema.name.find("CompressedImage") != std::string::npos;
    if (!is_image) {
      is_image = chan.topic.find("image") != std::string::npos &&
                 chan.topic.find("compressed") != std::string::npos;
    }
    if (!is_image) return false;
    if (chan.topic.find("Depth") != std::string::npos || chan.topic.find("depth") != std::string::npos) {
      return false;
    }
    if (!target_topic.empty() && chan.topic != target_topic) return false;
    return true;
  };
}
TopicMatchFn annotationsMatchFn() {
  return [](const mcap::Channel& /*chan*/, const mcap::Schema& schema) {
    return PJ::isSupportedSceneSchema(schema.name);
  };
}
}  // namespace

// ---------------------------------------------------------------------------
// ImageViewerWindow
// ---------------------------------------------------------------------------

class ImageViewerWindow : public QMainWindow {
  Q_OBJECT

 public:
  void loadFile(const QString& path) {
    // Detach old source before destroying the store / loader.
    image_widget_->setMediaSource(nullptr);
    active_source_.reset();
    annotations_loader_.reset();
    image_loader_.reset();
    progress_timer_->stop();

    store_ = std::make_unique<PJ::ObjectStore>();
    image_loader_ = std::make_unique<McapTopicLoader>();
    bool ok = image_loader_->open(path.toStdString(), *store_,
                                   imageMatchFn("/camera/color/image_raw/compressed"));
    if (!ok) {
      store_ = std::make_unique<PJ::ObjectStore>();
      image_loader_ = std::make_unique<McapTopicLoader>();
      ok = image_loader_->open(path.toStdString(), *store_, imageMatchFn(""));
    }

    if (!ok || image_loader_->expected_count == 0) {
      setWindowTitle("No image topic found");
      image_loader_.reset();
      return;
    }

    auto image_src = std::make_unique<PJ::ImagePipelineSource>(
        store_.get(), image_loader_->topic_id, PJ::makeCdrJpegPipeline());

    // Try to discover an annotations topic. If found, wrap image+scene in a
    // composite; otherwise the image source itself is the active source.
    annotations_loader_ = std::make_unique<McapTopicLoader>();
    std::unique_ptr<PJ::ISceneDecoder> decoder;
    if (annotations_loader_->open(path.toStdString(), *store_, annotationsMatchFn())) {
      decoder = PJ::makeSceneDecoder(annotations_loader_->schema_name);
    }
    if (decoder != nullptr) {
      auto scene_src = std::make_unique<PJ::ScenePipelineSource>(
          store_.get(), annotations_loader_->topic_id, std::move(decoder));
      auto composite = std::make_unique<PJ::CompositeMediaSource>();
      composite->addLayer(std::move(image_src));
      composite->addLayer(std::move(scene_src));
      active_source_ = std::move(composite);
    } else {
      annotations_loader_.reset();
      active_source_ = std::move(image_src);
    }
    image_widget_->setMediaSource(active_source_.get());

    setWindowTitle(QString("Indexing %1 frames…").arg(image_loader_->expected_count));
    slider_->setRange(0, static_cast<int>(image_loader_->expected_count - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    progress_timer_->start();
    refreshProgress();
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
    play_button_ = new QPushButton("▶", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    time_label_ = new QLabel("0 / 0", this);
    time_label_->setFixedWidth(180);
    controls->addWidget(time_label_);
    layout->addLayout(controls);

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(33);

    throttle_timer_ = new QTimer(this);
    throttle_timer_->setSingleShot(true);
    throttle_.start();

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(200);  // refresh "Indexed X / Y" 5x per second

    connect(load_button_, &QPushButton::clicked, this, &ImageViewerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &ImageViewerWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &ImageViewerWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &ImageViewerWindow::onTimerTick);
    connect(progress_timer_, &QTimer::timeout, this, &ImageViewerWindow::refreshProgress);
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
      play_button_->setText("▶");
    } else {
      play_timer_->start();
      play_button_->setText("⏸");
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
    if (!image_loader_) {
      play_timer_->stop();
      return;
    }
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= image_loader_->expected_count) {
      play_timer_->stop();
      play_button_->setText("▶");
      return;
    }
    // If play has caught up to the indexer, hold position until more entries
    // are pushed. Without this, store_->at(next) returns nullopt and frames
    // are silently dropped, producing a stuttering display.
    size_t indexed = image_loader_->indexed_count.load(std::memory_order_relaxed);
    if (next >= indexed) {
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

  void refreshProgress() {
    if (!image_loader_) {
      return;
    }
    size_t indexed = image_loader_->indexed_count.load(std::memory_order_relaxed);
    size_t expected = image_loader_->expected_count;
    if (indexed >= expected) {
      time_label_->setText(QString("%1 / %2").arg(slider_->value() + 1).arg(expected));
      setWindowTitle(QString("Loaded %1 frames").arg(expected));
      progress_timer_->stop();
    } else {
      time_label_->setText(QString("Indexing %1 / %2").arg(indexed).arg(expected));
    }
  }

 private:
  void showFrame(size_t index) {
    if (!store_ || !image_loader_) {
      return;
    }
    // Look up the timestamp WITHOUT resolving the payload — we just need the ts
    // to drive the MediaSource, which will fetch+decode the bytes itself via
    // latestAt → closure. Without this, every scrub did 2x MCAP reads per frame.
    int64_t ts = 0;
    {
      auto view = store_->entryTimestamps(image_loader_->topic_id);
      if (index >= view.size()) {
        time_label_->setText(
            QString("Indexing… %1 / %2")
                .arg(image_loader_->indexed_count.load(std::memory_order_relaxed))
                .arg(image_loader_->expected_count));
        return;
      }
      ts = view[index];
    }  // shared_lock released here, before the synchronous decode that follows.

    image_widget_->setTimestamp(ts);
    image_widget_->update();

    if (!image_loader_->isIndexing()) {
      time_label_->setText(QString("%1 / %2").arg(index + 1).arg(image_loader_->expected_count));
    }
  }

  static constexpr int kMinFrameIntervalMs = 16;

  std::unique_ptr<PJ::ObjectStore> store_;
  std::unique_ptr<McapTopicLoader> image_loader_;
  std::unique_ptr<McapTopicLoader> annotations_loader_;
  std::unique_ptr<PJ::MediaSource> active_source_;  ///< image or composite, owned by the window

  PJ::MediaViewerWidget* image_widget_ = nullptr;
  QSlider* slider_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QTimer* progress_timer_ = nullptr;
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
