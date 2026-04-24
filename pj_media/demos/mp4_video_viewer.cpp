/// MP4 video player demo using FileVideoSource (MediaSource adapter
/// for FfmpegBackend). Slider scrub, play/pause, step forward/backward.
///
/// Usage: mp4_video_viewer <file.mp4>

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
#include <memory>

#include "pj_media_core/file_video_source.h"
#include "pj_media_qt/media_viewer_widget.h"

class VideoPlayerWindow : public QMainWindow {
  Q_OBJECT

 public:
  VideoPlayerWindow() {
    setWindowTitle("MP4 Player");
    resize(900, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load Video", this);
    layout->addWidget(load_button_);

    // Bootstrap QRhiWidget
    auto* bootstrap = new PJ::MediaViewerWidget(this);
    bootstrap->setMaximumSize(0, 0);
    layout->addWidget(bootstrap);

    viewer_ = new PJ::MediaViewerWidget(this);
    viewer_->setMinimumSize(320, 240);
    viewer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewer_, 1);

    auto* controls = new QHBoxLayout();
    play_button_ = new QPushButton("\u25B6", this);
    play_button_->setFixedWidth(40);
    play_button_->setEnabled(false);
    controls->addWidget(play_button_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setEnabled(false);
    slider_->setRange(0, 10000);
    controls->addWidget(slider_);

    time_label_ = new QLabel("0.0 / 0.0", this);
    time_label_->setFixedWidth(140);
    controls->addWidget(time_label_);
    layout->addLayout(controls);

    throttle_timer_ = new QTimer(this);
    throttle_timer_->setSingleShot(true);
    throttle_.start();

    // Display timer: trigger render → takeFrame polling at 60 Hz
    display_timer_ = new QTimer(this);
    display_timer_->setInterval(16);
    connect(display_timer_, &QTimer::timeout, this, [this]() { viewer_->update(); });

    connect(load_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::sliderMoved, this, &VideoPlayerWindow::onSliderMoved);
    connect(slider_, &QSlider::sliderPressed, this, [this]() { slider_dragging_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this]() { slider_dragging_ = false; });
    connect(throttle_timer_, &QTimer::timeout, this, [this]() {
      if (source_ != nullptr && pending_seek_ != last_seek_fired_) {
        source_->setTimestamp(static_cast<int64_t>(pending_seek_ * 1'000'000'000.0));
        last_seek_fired_ = pending_seek_;
      }
      throttle_.restart();
    });
  }

  void loadFile(const QString& path) {
    // Detach old source
    viewer_->setMediaSource(nullptr);
    source_.reset();
    display_timer_->stop();

    auto source_or = PJ::FileVideoSource::open(path.toStdString());
    if (!source_or.has_value()) {
      setWindowTitle("Failed to open: " + path);
      return;
    }
    source_ = std::move(*source_or);

    source_->setPositionCallback([this](double s) { onPositionChanged(s); });
    source_->setDurationCallback([this](double s) { duration_ = s; });
    source_->setFileLoadedCallback([this]() { onFileLoaded(); });

    viewer_->setMediaSource(source_.get());
    display_timer_->start();
    setWindowTitle("MP4 Player: " + path);
  }

 private slots:
  void onLoad() {
    auto path = QFileDialog::getOpenFileName(this, "Open Video", QString(), "Video Files (*.mp4 *.mkv *.avi *.mov)");
    if (path.isEmpty()) {
      return;
    }
    loadFile(path);
  }

  void onPlayPause() {
    if (source_ == nullptr) {
      return;
    }
    source_->setPaused(!source_->isPaused());
    play_button_->setText(source_->isPaused() ? "\u25B6" : "\u23F8");
  }

  void onSliderMoved(int value) {
    if (duration_ <= 0 || source_ == nullptr) {
      return;
    }
    double seconds = static_cast<double>(value) / 10000.0 * duration_;
    pending_seek_ = seconds;

    if (throttle_.elapsed() >= kMinSeekIntervalMs) {
      source_->setTimestamp(static_cast<int64_t>(seconds * 1'000'000'000.0));
      last_seek_fired_ = seconds;
      throttle_.restart();
    } else if (!throttle_timer_->isActive()) {
      throttle_timer_->start(kMinSeekIntervalMs - static_cast<int>(throttle_.elapsed()));
    }
  }

 private:
  void onPositionChanged(double s) {
    if (!slider_dragging_) {
      int val = duration_ > 0 ? static_cast<int>(s / duration_ * 10000) : 0;
      slider_->blockSignals(true);
      slider_->setValue(val);
      slider_->blockSignals(false);
    }
    time_label_->setText(QString("%1 / %2").arg(s, 0, 'f', 1).arg(duration_, 0, 'f', 1));
  }

  void onFileLoaded() {
    slider_->setEnabled(true);
    play_button_->setEnabled(true);
    source_->setPaused(true);
  }

  static constexpr int kMinSeekIntervalMs = 33;

  std::unique_ptr<PJ::FileVideoSource> source_;

  PJ::MediaViewerWidget* viewer_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QTimer* display_timer_ = nullptr;
  QElapsedTimer throttle_;
  double duration_ = 0.0;
  double pending_seek_ = 0.0;
  double last_seek_fired_ = -1.0;
  bool slider_dragging_ = false;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  VideoPlayerWindow window;
  window.show();

  if (argc > 1) {
    window.loadFile(QString::fromUtf8(argv[1]));
  }

  return app.exec();
}

#include "mp4_video_viewer.moc"
