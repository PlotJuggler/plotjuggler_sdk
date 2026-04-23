#include <turbojpeg.h>

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
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "video_widget.hpp"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>
#include <nanocdr/nanocdr.hpp>

// ---------------------------------------------------------------------------
// LazyMediaSeries — timestamps + resolve callbacks, no data stored in memory
// ---------------------------------------------------------------------------

template <typename T>
class LazyMediaSeries {
 public:
  struct Entry {
    int64_t timestamp;
    std::function<T()> resolve;
  };

  void addEntry(int64_t timestamp, std::function<T()> resolve_fn) {
    entries_.push_back({timestamp, std::move(resolve_fn)});
  }

  const Entry* latestAt(int64_t ts) const {
    if (entries_.empty()) {
      return nullptr;
    }
    auto it = std::upper_bound(
        entries_.begin(), entries_.end(), ts, [](int64_t t, const Entry& e) { return t < e.timestamp; });
    if (it == entries_.begin()) {
      return nullptr;
    }
    return &*(--it);
  }

  const Entry* at(size_t index) const {
    return index < entries_.size() ? &entries_[index] : nullptr;
  }

  size_t size() const {
    return entries_.size();
  }
  bool empty() const {
    return entries_.empty();
  }

  int64_t minTimestamp() const {
    return entries_.empty() ? 0 : entries_.front().timestamp;
  }
  int64_t maxTimestamp() const {
    return entries_.empty() ? 0 : entries_.back().timestamp;
  }

  void clear() {
    entries_.clear();
  }

 private:
  std::vector<Entry> entries_;
};

// ===========================================================================
// DataSource: McapDataSource
//
// Encapsulates MCAP file access. Mirrors the DataSource plugin role:
//   - open/close lifecycle
//   - topic discovery
//   - message iteration (for indexing)
//   - re-read a specific message by timestamp (for resolve callbacks)
// ===========================================================================

class McapDataSource {
 public:
  struct TopicInfo {
    uint16_t channel_id;
    std::string topic;
    std::string encoding;
    std::string schema_name;
  };

  struct RawMessage {
    int64_t timestamp;
    const uint8_t* data;
    size_t size;
  };

  bool open(const std::string& path) {
    close();
    reader_ = std::make_shared<mcap::McapReader>();

    auto status = reader_->open(path);
    if (!status.ok()) {
      reader_.reset();
      return false;
    }

    status = reader_->readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
    if (!status.ok()) {
      reader_->close();
      reader_.reset();
      return false;
    }

    // Discover topics
    for (auto& [chan_id, chan_ptr] : reader_->channels()) {
      if (!chan_ptr) {
        continue;
      }
      TopicInfo info;
      info.channel_id = chan_id;
      info.topic = chan_ptr->topic;
      info.encoding = chan_ptr->messageEncoding;
      // Look up schema name
      auto schema_it = reader_->schemas().find(chan_ptr->schemaId);
      if (schema_it != reader_->schemas().end() && schema_it->second) {
        info.schema_name = schema_it->second->name;
      }
      topics_.push_back(std::move(info));
    }

    return true;
  }

  void close() {
    topics_.clear();
    if (reader_) {
      reader_->close();
      reader_.reset();
    }
  }

  const std::vector<TopicInfo>& topics() const {
    return topics_;
  }

  /// Iterate all messages for a topic, calling visitor(timestamp, raw_data, size) for each.
  /// Used during indexing — the visitor does NOT decode, just records timestamps.
  template <typename Visitor>
  void forEachMessage(uint16_t channel_id, Visitor&& visitor) const {
    if (!reader_) {
      return;
    }

    const auto* topic_info = findTopic(channel_id);
    if (!topic_info) {
      return;
    }

    mcap::ReadMessageOptions opts;
    opts.topicFilter = [&](std::string_view t) { return t == topic_info->topic; };

    auto view = reader_->readMessages([](const mcap::Status&) {}, opts);
    for (auto it = view.begin(); it != view.end(); ++it) {
      if (it->message.channelId == channel_id) {
        visitor(
            static_cast<int64_t>(it->message.logTime), reinterpret_cast<const uint8_t*>(it->message.data),
            it->message.dataSize);
      }
    }
  }

  /// Re-read a single message at the given timestamp for the given channel.
  /// This is what resolve callbacks use to fetch raw bytes on demand.
  std::optional<std::vector<uint8_t>> readMessageAt(uint16_t channel_id, int64_t timestamp) const {
    if (!reader_) {
      return std::nullopt;
    }

    const auto* topic_info = findTopic(channel_id);
    if (!topic_info) {
      return std::nullopt;
    }

    mcap::ReadMessageOptions opts;
    opts.startTime = static_cast<mcap::Timestamp>(timestamp);
    opts.endTime = opts.startTime + 1;
    opts.topicFilter = [&](std::string_view t) { return t == topic_info->topic; };

    auto view = reader_->readMessages([](const mcap::Status&) {}, opts);
    for (auto it = view.begin(); it != view.end(); ++it) {
      if (it->message.channelId == channel_id) {
        auto* data = reinterpret_cast<const uint8_t*>(it->message.data);
        return std::vector<uint8_t>(data, data + it->message.dataSize);
      }
    }
    return std::nullopt;
  }

  /// Get the shared reader pointer (for capture in callbacks).
  std::shared_ptr<mcap::McapReader> sharedReader() const {
    return reader_;
  }

 private:
  const TopicInfo* findTopic(uint16_t channel_id) const {
    for (auto& t : topics_) {
      if (t.channel_id == channel_id) {
        return &t;
      }
    }
    return nullptr;
  }

  std::shared_ptr<mcap::McapReader> reader_;
  std::vector<TopicInfo> topics_;
};

// ===========================================================================
// MessageParser: CompressedImageParser
//
// Mirrors the MessageParser plugin role:
//   - decodeMedia(raw_bytes) → QImage  (the expensive on-demand decode)
//
// In the real plugin system this would be a shared library with:
//   manifest: {"name":"CompressedImage","encoding":"cdr","media_class":"image"}
//   index_media(): parse CDR header, extract width/height from JPEG header
//   decode_media(): full CDR parse + turbojpeg decode
// ===========================================================================

class CompressedImageParser {
 public:
  CompressedImageParser() : tj_(tjInitDecompress()) {}
  ~CompressedImageParser() {
    if (tj_) {
      tjDestroy(tj_);
    }
  }

  CompressedImageParser(const CompressedImageParser&) = delete;
  CompressedImageParser& operator=(const CompressedImageParser&) = delete;

  /// Full decode: CDR envelope → JPEG bytes → RGB pixels.
  QImage decodeMedia(const uint8_t* raw, size_t size) const {
    if (size < 16 || !tj_) {
      return {};
    }

    nanocdr::Decoder dec(nanocdr::ConstBuffer(raw, size));
    dec.jump(8);         // skip stamp (sec + nsec)
    skipCdrString(dec);  // skip frame_id
    skipCdrString(dec);  // skip format

    uint32_t data_len;
    dec.decode(data_len);
    auto buf = dec.currentBuffer();
    if (buf.size() < data_len) {
      return {};
    }

    auto* jpeg_ptr = const_cast<uint8_t*>(buf.data());

    int width = 0, height = 0, subsamp = 0;
    if (tjDecompressHeader2(tj_, jpeg_ptr, data_len, &width, &height, &subsamp) != 0) {
      return {};
    }

    QImage img(width, height, QImage::Format_RGB888);
    if (tjDecompress2(
            tj_, jpeg_ptr, data_len, img.bits(), width, img.bytesPerLine(), height, TJPF_RGB,
            TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
      return {};
    }
    return img;
  }

 private:
  static void skipCdrString(nanocdr::Decoder& dec) {
    uint32_t len;
    dec.decode(len);
    dec.jump(len);
  }

  tjhandle tj_ = nullptr;
};

// ===========================================================================
// Host: buildLazyImageSeries
//
// Mirrors what the host does when a DataSource pushes messages through a
// media-class parser binding:
//   1. Iterate messages from the DataSource (indexing phase)
//   2. For each message, create a resolve callback that composes
//      DataSource::readMessageAt() with Parser::decodeMedia()
//   3. Store {timestamp, callback} in a LazyMediaSeries
//
// The result holds only timestamps + lightweight closures. No image data
// in memory. The DataSource and Parser stay alive via shared_ptr.
// ===========================================================================

struct MediaSource {
  LazyMediaSeries<QImage> series;
  std::shared_ptr<McapDataSource> data_source;
  std::shared_ptr<CompressedImageParser> parser;
};

MediaSource buildLazyImageSeries(const std::string& path, const std::string& target_topic) {
  auto source = std::make_shared<McapDataSource>();
  if (!source->open(path)) {
    return {};
  }

  // Find the target channel
  uint16_t channel_id = 0;
  for (auto& topic : source->topics()) {
    if (topic.topic == target_topic) {
      channel_id = topic.channel_id;
      break;
    }
  }
  if (channel_id == 0) {
    return {};
  }

  auto parser = std::make_shared<CompressedImageParser>();

  MediaSource result;
  result.data_source = source;
  result.parser = parser;

  // Index: walk all messages, record timestamp + resolve callback
  source->forEachMessage(channel_id, [&](int64_t ts, const uint8_t*, size_t) {
    // Callback captures shared_ptrs (lightweight) + scalars.
    // On invocation: re-reads raw bytes from MCAP, then decodes.
    result.series.addEntry(ts, [source, parser, channel_id, ts]() -> QImage {
      auto raw = source->readMessageAt(channel_id, ts);
      if (!raw) {
        return {};
      }
      return parser->decodeMedia(raw->data(), raw->size());
    });
  });

  return result;
}

// ---------------------------------------------------------------------------
// McapPlayerWindow
// ---------------------------------------------------------------------------

class McapPlayerWindow : public QMainWindow {
  Q_OBJECT

 public:
  McapPlayerWindow() {
    setWindowTitle("MCAP Player");
    resize(800, 650);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load MCAP", this);
    layout->addWidget(load_button_);

    video_widget_ = new VideoWidget(this);
    video_widget_->setMinimumSize(320, 240);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(video_widget_, 1);

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

    connect(load_button_, &QPushButton::clicked, this, &McapPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &McapPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &McapPlayerWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &McapPlayerWindow::onTimerTick);
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

    setCursor(Qt::WaitCursor);
    source_ = buildLazyImageSeries(path.toStdString(), "/camera/color/image_raw/compressed");
    setCursor(Qt::ArrowCursor);

    if (source_.series.empty()) {
      return;
    }

    slider_->setRange(0, static_cast<int>(source_.series.size() - 1));
    slider_->setValue(0);
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    showFrame(0);
  }

  void onPlayPause() {
    if (play_timer_->isActive()) {
      stopPlayback();
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
        // Schedule a deferred update so the final slider position is always shown
        throttle_timer_->start(kMinFrameIntervalMs - static_cast<int>(throttle_.elapsed()));
      }
    }
  }

  void onTimerTick() {
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= source_.series.size()) {
      stopPlayback();
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

 private:
  void stopPlayback() {
    play_timer_->stop();
    play_button_->setText("\u25B6");
  }

  void showFrame(size_t index) {
    auto* entry = source_.series.at(index);
    if (!entry) {
      return;
    }

    QImage img = entry->resolve();
    if (!img.isNull()) {
      video_widget_->setFrame(img);
    }

    time_label_->setText(QString("%1 / %2").arg(index + 1).arg(source_.series.size()));
  }

  static constexpr int kMinFrameIntervalMs = 16;  // ~60 Hz

  MediaSource source_;
  VideoWidget* video_widget_ = nullptr;
  QSlider* slider_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QElapsedTimer throttle_;
  size_t pending_index_ = 0;
};

// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  McapPlayerWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
