#include <turbojpeg.h>

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>
#include <nanocdr/nanocdr.hpp>

// ---------------------------------------------------------------------------
// LRU Cache
// ---------------------------------------------------------------------------

template <typename Key, typename Value>
class LruCache {
 public:
  explicit LruCache(size_t capacity) : capacity_(capacity) {}

  const Value* get(const Key& key) {
    auto it = map_.find(key);
    if (it == map_.end()) {
      ++misses_;
      return nullptr;
    }
    ++hits_;
    order_.splice(order_.begin(), order_, it->second);
    return &it->second->second;
  }

  void put(const Key& key, Value value) {
    auto it = map_.find(key);
    if (it != map_.end()) {
      it->second->second = std::move(value);
      order_.splice(order_.begin(), order_, it->second);
      return;
    }
    if (map_.size() >= capacity_) {
      map_.erase(order_.back().first);
      order_.pop_back();
    }
    order_.emplace_front(key, std::move(value));
    map_[key] = order_.begin();
  }

  size_t hits() const {
    return hits_;
  }
  size_t misses() const {
    return misses_;
  }
  size_t size() const {
    return map_.size();
  }

 private:
  size_t capacity_;
  size_t hits_ = 0;
  size_t misses_ = 0;
  std::list<std::pair<Key, Value>> order_;
  std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> map_;
};

// ---------------------------------------------------------------------------
// McapFrameStore — builds timestamp index, resolves frames on demand
// ---------------------------------------------------------------------------

class McapFrameStore {
 public:
  McapFrameStore() : tj_(tjInitDecompress()) {}
  ~McapFrameStore() {
    if (tj_) {
      tjDestroy(tj_);
    }
  }

  McapFrameStore(const McapFrameStore&) = delete;
  McapFrameStore& operator=(const McapFrameStore&) = delete;

  bool open(const std::string& path) {
    close();

    auto status = reader_.open(path);
    if (!status.ok()) {
      return false;
    }

    status = reader_.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
    if (!status.ok()) {
      reader_.close();
      return false;
    }

    const auto& channels = reader_.channels();
    for (auto& [chan_id, chan_ptr] : channels) {
      if (!chan_ptr) {
        continue;
      }
      if (chan_ptr->topic == kTargetTopic) {
        channel_id_ = chan_id;
        break;
      }
    }
    if (channel_id_ == 0) {
      reader_.close();
      return false;
    }

    // Record timestamps only — image data is resolved lazily on demand
    auto view = reader_.readMessages([](const mcap::Status&) {}, topicOpts());
    for (auto it = view.begin(); it != view.end(); ++it) {
      timestamps_.push_back(static_cast<int64_t>(it->message.logTime));
    }

    return !timestamps_.empty();
  }

  void close() {
    timestamps_.clear();
    cache_ = LruCache<int64_t, QImage>(kCacheCapacity);
    channel_id_ = 0;
    reader_.close();
  }

  size_t frameCount() const {
    return timestamps_.size();
  }
  int64_t timestamp(size_t index) const {
    return timestamps_[index];
  }

  QImage resolve(size_t index) {
    int64_t ts = timestamps_[index];
    if (auto* cached = cache_.get(ts)) {
      return *cached;
    }

    mcap::ReadMessageOptions opts;
    opts.startTime = static_cast<mcap::Timestamp>(ts);
    opts.endTime = opts.startTime + 1;
    opts.topicFilter = topicFilter();

    auto view = reader_.readMessages([](const mcap::Status&) {}, opts);
    for (auto it = view.begin(); it != view.end(); ++it) {
      auto& msg = it->message;
      if (msg.channelId != channel_id_) {
        continue;
      }

      QImage img = decodeJpegFromCdr(reinterpret_cast<const uint8_t*>(msg.data), msg.dataSize);
      if (!img.isNull()) {
        cache_.put(ts, std::move(img));
        return *cache_.get(ts);
      }
      return img;
    }
    return {};
  }

  size_t cacheHits() const {
    return cache_.hits();
  }
  size_t cacheMisses() const {
    return cache_.misses();
  }
  size_t cacheSize() const {
    return cache_.size();
  }

 private:
  static constexpr const char* kTargetTopic = "/camera/color/image_raw/compressed";
  static constexpr size_t kCacheCapacity = 30;

  static std::function<bool(std::string_view)> topicFilter() {
    return [](std::string_view topic) { return topic == kTargetTopic; };
  }

  mcap::ReadMessageOptions topicOpts() {
    mcap::ReadMessageOptions opts;
    opts.topicFilter = topicFilter();
    return opts;
  }

  static void skipCdrString(nanocdr::Decoder& dec) {
    uint32_t len;
    dec.decode(len);
    dec.jump(len);
  }

  QImage decodeJpegFromCdr(const uint8_t* raw, size_t size) {
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

    // turbojpeg takes non-const pointers (legacy C API)
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

  tjhandle tj_ = nullptr;
  mcap::McapReader reader_;
  uint16_t channel_id_ = 0;
  std::vector<int64_t> timestamps_;
  LruCache<int64_t, QImage> cache_{kCacheCapacity};
};

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

    image_label_ = new QLabel(this);
    image_label_->setAlignment(Qt::AlignCenter);
    image_label_->setMinimumSize(320, 240);
    image_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    image_label_->setStyleSheet("background-color: black;");
    layout->addWidget(image_label_, 1);

    auto* controls = new QHBoxLayout();
    play_button_ = new QPushButton("\u25B6", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    controls->addWidget(slider_);

    time_label_ = new QLabel("0 / 0", this);
    time_label_->setFixedWidth(300);
    controls->addWidget(time_label_);

    layout->addLayout(controls);

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(33);

    connect(load_button_, &QPushButton::clicked, this, &McapPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &McapPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::valueChanged, this, &McapPlayerWindow::onSliderChanged);
    connect(play_timer_, &QTimer::timeout, this, &McapPlayerWindow::onTimerTick);
  }

 private slots:
  void onLoad() {
    auto path = QFileDialog::getOpenFileName(this, "Open MCAP", QString(), "MCAP Files (*.mcap)");
    if (path.isEmpty()) {
      return;
    }

    setCursor(Qt::WaitCursor);
    bool ok = store_.open(path.toStdString());
    setCursor(Qt::ArrowCursor);

    if (!ok) {
      image_label_->setText("Failed to open MCAP or no matching channel found");
      return;
    }

    slider_->setRange(0, static_cast<int>(store_.frameCount() - 1));
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
      showFrame(static_cast<size_t>(value));
    }
  }

  void onTimerTick() {
    size_t next = static_cast<size_t>(slider_->value()) + 1;
    if (next >= store_.frameCount()) {
      stopPlayback();
      return;
    }
    slider_->blockSignals(true);
    slider_->setValue(static_cast<int>(next));
    slider_->blockSignals(false);
    showFrame(next);
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QMainWindow::resizeEvent(event);
    updatePixmap();
  }

 private:
  void stopPlayback() {
    play_timer_->stop();
    play_button_->setText("\u25B6");
  }

  void updatePixmap() {
    if (current_image_.isNull()) {
      return;
    }
    image_label_->setPixmap(
        QPixmap::fromImage(current_image_).scaled(image_label_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
  }

  void showFrame(size_t index) {
    if (index >= store_.frameCount()) {
      return;
    }

    current_image_ = store_.resolve(index);
    updatePixmap();

    time_label_->setText(QString("%1/%2  H:%3 M:%4 (%5)")
                             .arg(index + 1)
                             .arg(store_.frameCount())
                             .arg(store_.cacheHits())
                             .arg(store_.cacheMisses())
                             .arg(store_.cacheSize()));
  }

  McapFrameStore store_;
  QImage current_image_;
  QLabel* image_label_ = nullptr;
  QSlider* slider_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* play_timer_ = nullptr;
};

// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  McapPlayerWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
