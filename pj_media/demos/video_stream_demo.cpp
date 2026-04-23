/// Video stream demo: demuxes an MP4 file, pushes H.264 NAL units into
/// ObjectStore at real-time rate, and displays via StreamingVideoDecoder.
///
/// Live mode: push timer active, slider pinned to live edge.
/// Scrub mode: push timer stopped, buffer frozen, slider-driven seek.
///
/// Usage: video_stream_demo <file.mp4>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

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
#include <deque>
#include <memory>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/streaming_video_source.h"
#include "pj_media_qt/media_viewer_widget.h"

namespace {

struct PacketData {
  std::vector<uint8_t> data;
  int64_t dts_ns = 0;  // decode timestamp (monotonic, used as ObjectStore key)
};

/// Pre-load all packets from an MP4 file, converting to annex-B format.
struct DemuxedVideo {
  std::deque<PacketData> packets;
  int64_t frame_interval_us = 33333;
  double duration_sec = 0;

  bool load(const char* path) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path, nullptr, nullptr) < 0) {
      return false;
    }
    avformat_find_stream_info(fmt_ctx, nullptr);

    int video_idx = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
      if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_idx = static_cast<int>(i);
        break;
      }
    }
    if (video_idx < 0) {
      avformat_close_input(&fmt_ctx);
      return false;
    }

    auto* stream = fmt_ctx->streams[video_idx];
    double time_base = av_q2d(stream->time_base);
    duration_sec = static_cast<double>(stream->duration) * time_base;
    if (duration_sec <= 0 && fmt_ctx->duration > 0) {
      duration_sec = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }

    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
      frame_interval_us = static_cast<int64_t>(1'000'000.0 * stream->avg_frame_rate.den / stream->avg_frame_rate.num);
    }

    // Set up h264_mp4toannexb bitstream filter
    const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
    AVBSFContext* bsf_ctx = nullptr;
    av_bsf_alloc(bsf, &bsf_ctx);
    avcodec_parameters_copy(bsf_ctx->par_in, stream->codecpar);
    bsf_ctx->time_base_in = stream->time_base;
    av_bsf_init(bsf_ctx);

    AVPacket* pkt = av_packet_alloc();
    AVPacket* filtered = av_packet_alloc();

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
      if (pkt->stream_index != video_idx) {
        av_packet_unref(pkt);
        continue;
      }

      int64_t pkt_dts = pkt->dts;

      if (av_bsf_send_packet(bsf_ctx, pkt) >= 0) {
        while (av_bsf_receive_packet(bsf_ctx, filtered) >= 0) {
          PacketData pd;
          pd.data.assign(filtered->data, filtered->data + filtered->size);
          pd.dts_ns = static_cast<int64_t>(static_cast<double>(pkt_dts) * time_base * 1'000'000'000.0);
          packets.push_back(std::move(pd));
          av_packet_unref(filtered);
        }
      }
      av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_packet_free(&filtered);
    av_bsf_free(&bsf_ctx);
    avformat_close_input(&fmt_ctx);

    return !packets.empty();
  }
};

}  // namespace

class VideoStreamWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit VideoStreamWindow(const char* video_path) {
    setWindowTitle("Video Stream Demo");
    resize(900, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    // Bootstrap QRhiWidget
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

    info_label_ = new QLabel("Loading...", this);
    info_label_->setFixedWidth(300);
    controls->addWidget(info_label_);
    layout->addLayout(controls);

    // Demux the video
    if (!demuxed_.load(video_path)) {
      info_label_->setText("Failed to load video");
      return;
    }

    // ObjectStore with 500-frame retention buffer
    store_ = std::make_unique<PJ::ObjectStore>();
    auto topic_or = store_->registerTopic(
        {.dataset_id = 1,
         .topic_name = "video/stream",
         .metadata_json = R"({"media_class":"video","encoding":"h264"})"});
    topic_ = *topic_or;
    retention_ns_ = 500 * demuxed_.frame_interval_us * 1000;
    store_->setRetentionBudget(topic_, {.time_window_ns = retention_ns_, .max_memory_bytes = 0});

    // StreamingVideoSource (MediaSource adapter with worker thread)
    source_ = std::make_unique<PJ::StreamingVideoSource>(store_.get(), topic_);
    viewer_->setMediaSource(source_.get());

    // Push timer: simulates real-time streaming at the video's frame rate
    push_timer_ = new QTimer(this);
    push_timer_->setInterval(static_cast<int>(demuxed_.frame_interval_us / 1000));

    // Display timer: poll for new frames at 60 Hz
    display_timer_ = new QTimer(this);
    display_timer_->setInterval(16);

    connect(push_timer_, &QTimer::timeout, this, &VideoStreamWindow::onPushFrame);
    connect(display_timer_, &QTimer::timeout, this, &VideoStreamWindow::onDisplayTick);
    connect(live_button_, &QPushButton::toggled, this, &VideoStreamWindow::onLiveToggled);
    connect(slider_, &QSlider::valueChanged, this, &VideoStreamWindow::onSliderChanged);

    // Burst the first 120 packets to fill the decoder's reorder buffer
    // instantly. Without this, B-frame videos take ~3 seconds to show
    // the first frame (96 packets at 33ms each). I+P videos are unaffected
    // (first frame appears after ~4 packets regardless).
    constexpr size_t kBurstCount = 120;
    for (size_t i = 0; i < kBurstCount && push_index_ < demuxed_.packets.size(); ++i) {
      onPushFrame();
    }

    push_timer_->start();
    display_timer_->start();

    info_label_->setText(
        QString("Loaded %1 frames, %2 s").arg(demuxed_.packets.size()).arg(demuxed_.duration_sec, 0, 'f', 1));
  }

 private slots:
  void onPushFrame() {
    if (push_index_ >= demuxed_.packets.size()) {
      // Loop: reset to beginning
      push_index_ = 0;
      store_->clear();
      auto topic_or = store_->registerTopic(
          {.dataset_id = 1,
           .topic_name = "video/stream",
           .metadata_json = R"({"media_class":"video","encoding":"h264"})"});
      topic_ = *topic_or;
      store_->setRetentionBudget(topic_, {.time_window_ns = retention_ns_, .max_memory_bytes = 0});
      // Recreate the source for the new topic
      viewer_->setMediaSource(nullptr);
      source_ = std::make_unique<PJ::StreamingVideoSource>(store_.get(), topic_);
      viewer_->setMediaSource(source_.get());
      pts_offset_ += demuxed_.packets.back().dts_ns + demuxed_.frame_interval_us * 1000;
    }

    const auto& pkt = demuxed_.packets[push_index_];
    PJ::Timestamp ts = pkt.dts_ns + pts_offset_;
    store_->pushOwned(topic_, ts, pkt.data);
    ++push_index_;
  }

  void onDisplayTick() {
    if (is_live_) {
      auto count = store_->entryCount(topic_);
      if (count == 0) {
        return;
      }

      auto range = store_->timeRange(topic_);
      source_->setTimestamp(range.second);
      viewer_->update();

      // Update slider range (disabled in live mode)
      slider_->blockSignals(true);
      slider_->setRange(0, static_cast<int>(count - 1));
      slider_->setValue(static_cast<int>(count - 1));
      slider_->blockSignals(false);

      info_label_->setText(QString("Live | %1 frames | %2 KB").arg(count).arg(store_->memoryUsage(topic_) / 1024));
    }
  }

  void onLiveToggled(bool live) {
    is_live_ = live;
    slider_->setEnabled(!live);
    live_button_->setText(live ? "Live" : "Scrub");

    if (live) {
      // Resume: restart push timer
      push_timer_->start();
    } else {
      // Pause: stop push timer, freeze buffer
      push_timer_->stop();
    }
  }

  void onSliderChanged(int value) {
    if (is_live_) {
      return;
    }

    auto entry = store_->at(topic_, static_cast<size_t>(value));
    if (!entry.has_value()) {
      return;
    }

    source_->setTimestamp(entry->timestamp);
    viewer_->update();

    auto count = store_->entryCount(topic_);
    info_label_->setText(QString("Scrub %1/%2").arg(value + 1).arg(count));
  }

 private:
  DemuxedVideo demuxed_;
  std::unique_ptr<PJ::ObjectStore> store_;
  PJ::ObjectTopicId topic_{};
  std::unique_ptr<PJ::StreamingVideoSource> source_;

  size_t push_index_ = 0;
  int64_t pts_offset_ = 0;
  int64_t retention_ns_ = 0;
  bool is_live_ = true;

  PJ::MediaViewerWidget* viewer_ = nullptr;
  QPushButton* live_button_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* info_label_ = nullptr;
  QTimer* push_timer_ = nullptr;
  QTimer* display_timer_ = nullptr;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: video_stream_demo <file.mp4>\n");
    return 1;
  }

  QApplication app(argc, argv);
  VideoStreamWindow window(argv[1]);
  window.show();
  return app.exec();
}

#include "video_stream_demo.moc"
