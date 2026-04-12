/// MP4 video player demo with switchable backend.
///
/// Usage: mp4_video_viewer [--ffmpeg] <file.mp4>
///   Default: libmpv backend (renders to OpenGL FBO)
///   --ffmpeg: FFmpeg backend (decodes to CPU, displays via QRhiWidget)

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
#include <cstring>
#include <memory>

#include "pj_media_core/video_backend.h"

#ifdef PJ_HAS_MPV
#include "pj_media_qt/video_viewer_widget.h"
#endif

#ifdef PJ_HAS_FFMPEG
#include "pj_media_core/ffmpeg_backend.h"
#include "pj_media_qt/media_viewer_widget.h"
#endif

class VideoPlayerWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit VideoPlayerWindow(bool use_ffmpeg) : use_ffmpeg_(use_ffmpeg) {
    setWindowTitle(use_ffmpeg ? "MP4 Player (FFmpeg)" : "MP4 Player (libmpv)");
    resize(900, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load Video", this);
    layout->addWidget(load_button_);

    // Create the right widget + backend based on mode
#ifdef PJ_HAS_FFMPEG
    if (use_ffmpeg) {
      // Bootstrap for QRhiWidget
      auto* bootstrap = new PJ::MediaViewerWidget(this);
      bootstrap->setMaximumSize(0, 0);
      layout->addWidget(bootstrap);

      rhi_widget_ = new PJ::MediaViewerWidget(this);
      rhi_widget_->setMinimumSize(320, 240);
      rhi_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      layout->addWidget(rhi_widget_, 1);

      auto ffmpeg = std::make_unique<PJ::FfmpegBackend>();
      ffmpeg->setFrameCallback([this](const PJ::DecodedFrame& frame) {
        if (rhi_widget_ != nullptr && !frame.isNull()) {
          QImage img(frame.pixels->data(), frame.width, frame.height, frame.width * 3, QImage::Format_RGB888);
          rhi_widget_->setFrame(img.copy());
        }
      });
      backend_ = std::move(ffmpeg);
    }
#endif

#ifdef PJ_HAS_MPV
    if (!use_ffmpeg) {
      auto* vvw = new PJ::VideoViewerWidget(this);
      vvw->setMinimumSize(320, 240);
      vvw->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      layout->addWidget(vvw, 1);
      backend_ = nullptr;  // VideoViewerWidget owns its backend
      mpv_widget_ = vvw;

      connect(vvw, &PJ::VideoViewerWidget::positionChanged, this, [this](double s) { onPositionChanged(s); });
      connect(vvw, &PJ::VideoViewerWidget::durationChanged, this, [this](double s) { duration_ = s; });
      connect(vvw, &PJ::VideoViewerWidget::fileLoaded, this, [this]() { onFileLoaded(); });
    }
#endif

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

    connect(load_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::sliderMoved, this, &VideoPlayerWindow::onSliderMoved);
    connect(slider_, &QSlider::sliderPressed, this, [this]() { slider_dragging_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this]() { slider_dragging_ = false; });
    connect(throttle_timer_, &QTimer::timeout, this, [this]() {
      activeBackend()->seek(pending_seek_);
      throttle_.restart();
    });

    // FFmpeg backend callbacks (mpv callbacks are wired via signals above)
    if (backend_) {
      backend_->setPositionCallback([this](double s) { onPositionChanged(s); });
      backend_->setDurationCallback([this](double s) { duration_ = s; });
      backend_->setFileLoadedCallback([this]() { onFileLoaded(); });
    }
  }

  void loadFile(const QString& path) {
    activeBackend()->open(path.toStdString());
    setWindowTitle(QString("%1: %2").arg(use_ffmpeg_ ? "FFmpeg" : "mpv").arg(path));
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
    auto* b = activeBackend();
    b->setPaused(!b->isPaused());
    play_button_->setText(b->isPaused() ? "\u25B6" : "\u23F8");
  }

  void onSliderMoved(int value) {
    if (duration_ <= 0) {
      return;
    }
    double seconds = static_cast<double>(value) / 10000.0 * duration_;
    pending_seek_ = seconds;

    if (throttle_.elapsed() >= kMinSeekIntervalMs) {
      activeBackend()->seek(seconds);
      throttle_.restart();
    } else if (!throttle_timer_->isActive()) {
      throttle_timer_->start(kMinSeekIntervalMs - static_cast<int>(throttle_.elapsed()));
    }
  }

 private:
  PJ::VideoBackend* activeBackend() {
    if (backend_) {
      return backend_.get();
    }
#ifdef PJ_HAS_MPV
    if (mpv_widget_ != nullptr) {
      return mpv_widget_->backend();
    }
#endif
    return nullptr;
  }

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
    activeBackend()->setPaused(true);
  }

  static constexpr int kMinSeekIntervalMs = 16;

  bool use_ffmpeg_ = false;
  std::unique_ptr<PJ::VideoBackend> backend_;

#ifdef PJ_HAS_MPV
  PJ::VideoViewerWidget* mpv_widget_ = nullptr;
#endif
#ifdef PJ_HAS_FFMPEG
  PJ::MediaViewerWidget* rhi_widget_ = nullptr;
#endif

  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* time_label_ = nullptr;
  QTimer* throttle_timer_ = nullptr;
  QElapsedTimer throttle_;
  double duration_ = 0.0;
  double pending_seek_ = 0.0;
  bool slider_dragging_ = false;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  bool use_ffmpeg = false;
  QString file_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--ffmpeg") == 0) {
      use_ffmpeg = true;
    } else {
      file_path = QString::fromUtf8(argv[i]);
    }
  }

  VideoPlayerWindow window(use_ffmpeg);
  window.show();

  if (!file_path.isEmpty()) {
    window.loadFile(file_path);
  }

  return app.exec();
}

#include "mp4_video_viewer.moc"
