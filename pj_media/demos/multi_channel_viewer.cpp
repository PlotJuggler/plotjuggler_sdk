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
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codecs.h"
#include "pj_media_core/image_pipeline_source.h"
#include "pj_media_qt/media_viewer_widget.h"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>

#include "mcap_helpers.hpp"

// ---------------------------------------------------------------------------
// MultiChannelMcapLoader — two-phase async loader for MCAP image topics.
// open() returns immediately after registering all image topics; a single
// background thread iterates the MCAP once and dispatches per-channel.
// Mirrors the pattern in mcap_image_viewer.cpp but with N channels.
// ---------------------------------------------------------------------------

struct MultiChannelMcapLoader {
  struct Channel {
    PJ::ObjectTopicId topic_id{};
    std::string topic_name;
    uint16_t mcap_chan_id = 0;
    bool is_depth = false;
    size_t expected_count = 0;
  };

  std::vector<Channel> channels;

  // index_reader is touched only by the indexer thread; fetch_reader is
  // shared by all closure executions and protected by fetch_mutex (mcap
  // readers are not thread-safe across concurrent readMessages calls).
  std::shared_ptr<mcap::McapReader> index_reader;
  std::shared_ptr<mcap::McapReader> fetch_reader;
  std::shared_ptr<std::mutex> fetch_mutex;

  std::atomic<size_t> indexed_count{0};   // total across all channels
  std::atomic<bool> stop_requested{false};
  std::thread indexer_thread;

  MultiChannelMcapLoader() = default;
  MultiChannelMcapLoader(const MultiChannelMcapLoader&) = delete;
  MultiChannelMcapLoader& operator=(const MultiChannelMcapLoader&) = delete;

  ~MultiChannelMcapLoader() {
    stop_requested.store(true, std::memory_order_relaxed);
    if (indexer_thread.joinable()) {
      indexer_thread.join();
    }
  }

  size_t totalExpected() const {
    size_t total = 0;
    for (const auto& ch : channels) {
      total += ch.expected_count;
    }
    return total;
  }

  [[nodiscard]] bool isIndexing() const {
    return indexed_count.load(std::memory_order_relaxed) < totalExpected();
  }

  bool open(const std::string& path, PJ::ObjectStore& store) {
    auto summary_reader = std::make_shared<mcap::McapReader>();
    if (!summary_reader->open(path).ok()) {
      return false;
    }
    if (!summary_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) {
      return false;
    }

    auto stats = summary_reader->statistics();

    for (const auto& [chan_id, chan_ptr] : summary_reader->channels()) {
      if (chan_ptr == nullptr) {
        continue;
      }
      bool has_image = chan_ptr->topic.find("image") != std::string::npos;
      if (!has_image) {
        continue;
      }

      auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = chan_ptr->topic, .metadata_json = "{}"});
      if (!id_or.has_value()) {
        continue;
      }

      Channel ch;
      ch.topic_id = *id_or;
      ch.topic_name = chan_ptr->topic;
      ch.mcap_chan_id = chan_id;
      ch.is_depth =
          chan_ptr->topic.find("depth") != std::string::npos || chan_ptr->topic.find("Depth") != std::string::npos;

      if (stats.has_value()) {
        auto count_it = stats->channelMessageCounts.find(chan_id);
        if (count_it != stats->channelMessageCounts.end()) {
          ch.expected_count = static_cast<size_t>(count_it->second);
        }
      }

      channels.push_back(std::move(ch));
    }

    if (channels.empty()) {
      return false;
    }

    index_reader = std::make_shared<mcap::McapReader>();
    if (!index_reader->open(path).ok()) {
      return false;
    }
    if (!index_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) {
      return false;
    }
    fetch_reader = std::make_shared<mcap::McapReader>();
    if (!fetch_reader->open(path).ok()) {
      return false;
    }
    if (!fetch_reader->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan).ok()) {
      return false;
    }
    fetch_mutex = std::make_shared<std::mutex>();

    indexer_thread = std::thread(&MultiChannelMcapLoader::indexLoop, this, std::ref(store));
    return true;
  }

 private:
  void indexLoop(PJ::ObjectStore& store) {
    std::unordered_map<uint16_t, size_t> chan_lookup;
    for (size_t i = 0; i < channels.size(); ++i) {
      chan_lookup[channels[i].mcap_chan_id] = i;
    }

    mcap::ReadMessageOptions opts;
    // ObjectStore::pushLazy requires non-decreasing timestamps; mcap's default
    // read order is FileOrder which can surface messages out-of-order on bags
    // with chunk layouts that aren't chronological (rosbag2 message-mode
    // compressed bags do this). Force chronological iteration here.
    opts.readOrder = mcap::ReadMessageOptions::ReadOrder::LogTimeOrder;
    auto view = index_reader->readMessages([](const mcap::Status&) {}, opts);

    for (auto it = view.begin(); it != view.end(); ++it) {
      if (stop_requested.load(std::memory_order_relaxed)) {
        return;
      }
      auto map_it = chan_lookup.find(it->message.channelId);
      if (map_it == chan_lookup.end()) {
        continue;
      }
      const auto& ch = channels[map_it->second];
      auto ts = static_cast<PJ::Timestamp>(it->message.logTime);
      auto chan_id = it->message.channelId;

      auto status = store.pushLazy(
          ch.topic_id, ts,
          [reader = fetch_reader, mtx = fetch_mutex, chan_id, ts]() -> std::vector<uint8_t> {
            std::lock_guard<std::mutex> lock(*mtx);
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

              // Decompress zstd-message-compressed payloads (rosbag2
              // compression_mode=MESSAGE) before any further parsing.
              auto bytes = pj_demos::maybeDecompressZstd({raw, raw + raw_size});

              // Strip CDR envelope: pipeline expects raw JPEG/PNG bytes.
              PJ::DecodedFrame frame;
              frame.pixels = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
              PJ::CdrImageStripper stripper;
              auto result = stripper.decode(frame);
              if (result.has_value() && !result->isNull()) {
                return std::move(*result->pixels);
              }
              return *frame.pixels;
            }
            return {};
          });

      if (status.has_value()) {
        indexed_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Channel info — UI-side state per channel (one widget + one MediaSource).
// ---------------------------------------------------------------------------

struct ChannelView {
  PJ::ObjectTopicId topic_id{};
  std::string topic_name;
  std::unique_ptr<PJ::ImagePipelineSource> source;
  PJ::MediaViewerWidget* widget = nullptr;
  size_t expected_count = 0;
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

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(200);  // refresh "Indexed X / Y" 5x per second

    connect(load_button_, &QPushButton::clicked, this, &MultiChannelWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &MultiChannelWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &MultiChannelWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &MultiChannelWindow::onTimerTick);
    connect(progress_timer_, &QTimer::timeout, this, &MultiChannelWindow::refreshProgress);
    connect(throttle_timer_, &QTimer::timeout, this, [this]() {
      showFrame(pending_index_);
      throttle_.restart();
    });
  }

  void loadFile(const QString& path) {
    // Detach old sources before destroying the store / loader.
    for (auto& ch : channels_) {
      ch.widget->setMediaSource(nullptr);
      delete ch.widget;
    }
    channels_.clear();
    loader_.reset();
    progress_timer_->stop();
    play_timer_->stop();

    store_ = std::make_unique<PJ::ObjectStore>();
    loader_ = std::make_unique<MultiChannelMcapLoader>();
    if (!loader_->open(path.toStdString(), *store_)) {
      setWindowTitle("Failed to load: " + path);
      loader_.reset();
      return;
    }

    // Build a UI ChannelView per loader channel: one widget + one pipeline.
    size_t max_count = 0;
    for (const auto& lch : loader_->channels) {
      ChannelView cv;
      cv.topic_id = lch.topic_id;
      cv.topic_name = lch.topic_name;
      cv.expected_count = lch.expected_count;
      max_count = std::max(max_count, cv.expected_count);

      std::unique_ptr<PJ::CodecPipeline> pipeline =
          lch.is_depth ? PJ::makeDepthPipeline() : PJ::makeJpegPipeline();

      cv.widget = new PJ::MediaViewerWidget(splitter_);
      cv.widget->setMinimumSize(200, 150);
      splitter_->addWidget(cv.widget);

      cv.source = std::make_unique<PJ::ImagePipelineSource>(store_.get(), cv.topic_id, std::move(pipeline));
      cv.widget->setMediaSource(cv.source.get());

      channels_.push_back(std::move(cv));
    }

    if (max_count == 0) {
      setWindowTitle("No image topics found");
      loader_.reset();
      return;
    }
    max_index_ = max_count;

    slider_->setRange(0, static_cast<int>(max_count - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);

    setWindowTitle(QString("Indexing %1 channel(s)…").arg(channels_.size()));
    progress_timer_->start();
    refreshProgress();
    showFrame(0);
    play_timer_->start();
    play_button_->setText("⏸");
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
    if (!loader_) {
      play_timer_->stop();
      return;
    }
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= max_index_) {
      play_timer_->stop();
      play_button_->setText("\u25B6");
      return;
    }
    // Hold position if NO channel has indexed this frame yet (otherwise
    // showFrame just skips the channels that aren't ready).
    bool any_ready = false;
    for (const auto& ch : channels_) {
      if (next < ch.expected_count && next < store_->entryCount(ch.topic_id)) {
        any_ready = true;
        break;
      }
    }
    if (!any_ready) {
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

  void refreshProgress() {
    if (!loader_) {
      return;
    }
    size_t indexed = loader_->indexed_count.load(std::memory_order_relaxed);
    size_t expected = loader_->totalExpected();
    if (indexed >= expected) {
      QString title;
      for (const auto& ch : channels_) {
        if (!title.isEmpty()) {
          title += " + ";
        }
        title += QString::fromStdString(ch.topic_name) + " (" + QString::number(ch.expected_count) + ")";
      }
      setWindowTitle(title);
      progress_timer_->stop();
    } else {
      setWindowTitle(QString("Indexing %1 / %2").arg(indexed).arg(expected));
    }
  }

 private:
  void showFrame(size_t index) {
    if (!loader_) {
      return;
    }
    for (auto& ch : channels_) {
      if (index >= ch.expected_count || ch.source == nullptr) {
        continue;
      }
      // Look up timestamp WITHOUT resolving payload — pipeline fetches it
      // itself when the widget renders. Saves a round-trip through the closure.
      int64_t ts = 0;
      {
        auto view = store_->entryTimestamps(ch.topic_id);
        if (index >= view.size()) {
          continue;  // not yet indexed for this channel
        }
        ts = view[index];
      }  // release series lock before setTimestamp triggers latestAt()
      ch.widget->setTimestamp(ts);
      ch.widget->update();
    }

    info_label_->setText(QString("%1 / %2").arg(index + 1).arg(max_index_));
  }

  static constexpr int kMinFrameIntervalMs = 16;

  std::unique_ptr<PJ::ObjectStore> store_;
  std::vector<ChannelView> channels_;
  std::unique_ptr<MultiChannelMcapLoader> loader_;

  QSplitter* splitter_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* info_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QTimer* progress_timer_ = nullptr;
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
