#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "pj_media_qt/video_viewer_widget.h"

class VideoPlayerWindow : public QMainWindow {
  Q_OBJECT

 public:
  VideoPlayerWindow() {
    setWindowTitle("MP4 Video Player (libmpv)");
    resize(900, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    load_button_ = new QPushButton("Load MP4", this);
    layout->addWidget(load_button_);

    video_ = new PJ::VideoViewerWidget(this);
    video_->setMinimumSize(320, 240);
    video_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(video_, 1);

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

    connect(load_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onLoad);
    connect(play_button_, &QPushButton::clicked, this, &VideoPlayerWindow::onPlayPause);
    connect(slider_, &QSlider::sliderMoved, this, &VideoPlayerWindow::onSliderMoved);

    connect(video_, &PJ::VideoViewerWidget::positionChanged, this, [this](double s) {
      if (!slider_dragging_) {
        int val = duration_ > 0 ? static_cast<int>(s / duration_ * 10000) : 0;
        slider_->blockSignals(true);
        slider_->setValue(val);
        slider_->blockSignals(false);
      }
      time_label_->setText(QString("%1 / %2").arg(s, 0, 'f', 1).arg(duration_, 0, 'f', 1));
    });

    connect(video_, &PJ::VideoViewerWidget::durationChanged, this, [this](double s) { duration_ = s; });

    connect(video_, &PJ::VideoViewerWidget::fileLoaded, this, [this]() {
      slider_->setEnabled(true);
      play_button_->setEnabled(true);
      video_->backend()->setPaused(true);
    });

    connect(slider_, &QSlider::sliderPressed, this, [this]() { slider_dragging_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this]() { slider_dragging_ = false; });
  }

  void loadFile(const QString& path) {
    video_->backend()->open(path.toStdString());
    setWindowTitle("Playing: " + path);
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
    auto* b = video_->backend();
    b->setPaused(!b->isPaused());
    play_button_->setText(b->isPaused() ? "\u25B6" : "\u23F8");
  }

  void onSliderMoved(int value) {
    if (duration_ > 0) {
      double seconds = static_cast<double>(value) / 10000.0 * duration_;
      video_->backend()->seek(seconds);
    }
  }

 private:
  PJ::VideoViewerWidget* video_ = nullptr;
  QPushButton* load_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* time_label_ = nullptr;
  double duration_ = 0.0;
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
