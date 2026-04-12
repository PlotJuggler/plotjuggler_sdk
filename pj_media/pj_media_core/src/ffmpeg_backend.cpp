#include "pj_media_core/ffmpeg_backend.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <chrono>

namespace PJ {

FfmpegBackend::FfmpegBackend() : decoder_(std::make_unique<FfmpegDecoder>()) {}

FfmpegBackend::~FfmpegBackend() {
  close();
}

bool FfmpegBackend::open(const std::string& path) {
  close();

  if (avformat_open_input(&fmt_ctx_, path.c_str(), nullptr, nullptr) < 0) {
    return false;
  }
  if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Find video stream
  for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
    if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_idx_ = static_cast<int>(i);
      break;
    }
  }
  if (video_stream_idx_ < 0) {
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  auto* stream = fmt_ctx_->streams[video_stream_idx_];
  time_base_ = av_q2d(stream->time_base);
  duration_sec_ = static_cast<double>(stream->duration) * time_base_;
  if (duration_sec_ <= 0 && fmt_ctx_->duration > 0) {
    duration_sec_ = static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
  }

  // Open decoder
  if (!decoder_->open(stream->codecpar)) {
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Build frame index: scan all packets for PTS and keyframe flags
  frame_index_.clear();
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt_ctx_, pkt) >= 0) {
    if (pkt->stream_index == video_stream_idx_) {
      frame_index_.push_back({pkt->pts, pkt->dts, (pkt->flags & AV_PKT_FLAG_KEY) != 0});
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);

  // Sort by PTS (should already be sorted for no-B-frame streams)
  std::sort(
      frame_index_.begin(), frame_index_.end(), [](const FrameIndex& a, const FrameIndex& b) { return a.pts < b.pts; });

  // Seek back to start
  av_seek_frame(fmt_ctx_, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
  decoder_->flush();

  // Callbacks
  if (on_duration_) {
    on_duration_(duration_sec_);
  }
  if (on_file_loaded_) {
    on_file_loaded_();
  }

  // Start decode thread
  running_.store(true);
  paused_.store(true);
  position_sec_.store(0.0);
  thread_ = std::thread(&FfmpegBackend::decodeThread, this);

  return true;
}

void FfmpegBackend::close() {
  if (running_.load()) {
    running_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  decoder_ = std::make_unique<FfmpegDecoder>();
  if (fmt_ctx_ != nullptr) {
    avformat_close_input(&fmt_ctx_);
  }
  frame_index_.clear();
  video_stream_idx_ = -1;
}

void FfmpegBackend::seek(double seconds) {
  if (fmt_ctx_ == nullptr) {
    return;
  }
  auto target_pts = static_cast<int64_t>(seconds / time_base_);
  std::lock_guard lock(mutex_);
  requested_pts_ = target_pts;
  has_request_ = true;
  seek_pending_ = true;
  cv_.notify_one();
}

void FfmpegBackend::setPaused(bool paused) {
  bool was_paused = paused_.exchange(paused);
  if (was_paused && !paused) {
    // Resuming: wake the decode thread
    cv_.notify_one();
  }
}

bool FfmpegBackend::isPaused() const {
  return paused_.load();
}

double FfmpegBackend::duration() const {
  return duration_sec_;
}

double FfmpegBackend::position() const {
  return position_sec_.load();
}

void FfmpegBackend::stepForward() {
  double step = frame_index_.size() > 1 ? static_cast<double>(frame_index_[1].pts - frame_index_[0].pts) * time_base_
                                        : 1.0 / 30.0;
  seek(position() + step);
}

void FfmpegBackend::stepBackward() {
  double step = frame_index_.size() > 1 ? static_cast<double>(frame_index_[1].pts - frame_index_[0].pts) * time_base_
                                        : 1.0 / 30.0;
  seek(std::max(0.0, position() - step));
}

void FfmpegBackend::setPositionCallback(PositionCallback cb) {
  on_position_ = std::move(cb);
}

void FfmpegBackend::setDurationCallback(DurationCallback cb) {
  on_duration_ = std::move(cb);
}

void FfmpegBackend::setFileLoadedCallback(FileLoadedCallback cb) {
  on_file_loaded_ = std::move(cb);
}

void FfmpegBackend::setFrameCallback(FrameCallback cb) {
  on_frame_ = std::move(cb);
}

void FfmpegBackend::processEvents() {
  // Nothing — callbacks are invoked directly from the decode thread
}

// ---------------------------------------------------------------------------
// Background decode thread
// ---------------------------------------------------------------------------

void FfmpegBackend::decodeThread() {
  while (running_.load()) {
    int64_t target = -1;
    bool do_seek = false;

    {
      std::unique_lock lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(16), [this]() {
        return !running_.load() || has_request_ || !paused_.load();
      });

      if (!running_.load()) {
        break;
      }

      if (has_request_) {
        target = requested_pts_;
        do_seek = seek_pending_;
        has_request_ = false;
        seek_pending_ = false;
      }
    }

    if (do_seek && target >= 0) {
      // Seek to nearest keyframe before target
      int64_t kf_pts = findKeyframeBefore(target);
      av_seek_frame(fmt_ctx_, video_stream_idx_, kf_pts, AVSEEK_FLAG_BACKWARD);
      decoder_->flush();

      // Decode forward from keyframe to target
      decodeAndDeliver(target);
    } else if (!paused_.load()) {
      // Sequential playback: read next packet, decode, deliver
      AVPacket* pkt = av_packet_alloc();
      if (av_read_frame(fmt_ctx_, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx_) {
          auto result = decoder_->decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
          if (result.has_value() && !result->isNull()) {
            double pos = static_cast<double>(pkt->pts) * time_base_;
            position_sec_.store(pos);
            if (on_position_) {
              on_position_(pos);
            }
            if (on_frame_) {
              on_frame_(*result);
            }
          }
        }
        av_packet_unref(pkt);
      } else {
        // EOF — pause
        paused_.store(true);
      }
      av_packet_free(&pkt);
    }
  }
}

void FfmpegBackend::decodeAndDeliver(int64_t target_pts) {
  AVPacket* pkt = av_packet_alloc();

  while (av_read_frame(fmt_ctx_, pkt) >= 0) {
    if (pkt->stream_index != video_stream_idx_) {
      av_packet_unref(pkt);
      continue;
    }

    auto result = decoder_->decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
    bool past_target = pkt->pts >= target_pts;
    av_packet_unref(pkt);

    if (result.has_value() && !result->isNull() && past_target) {
      double pos = static_cast<double>(target_pts) * time_base_;
      position_sec_.store(pos);
      if (on_position_) {
        on_position_(pos);
      }
      if (on_frame_) {
        on_frame_(*result);
      }
      break;
    }

    // Check for new request (cancel current seek if user moved slider again)
    {
      std::lock_guard lock(mutex_);
      if (has_request_) {
        break;  // Abandon this seek, loop will pick up new request
      }
    }
  }
  av_packet_free(&pkt);
}

int64_t FfmpegBackend::findKeyframeBefore(int64_t pts) const {
  int64_t best = 0;
  for (const auto& fi : frame_index_) {
    if (fi.keyframe && fi.pts <= pts) {
      best = fi.pts;
    }
    if (fi.pts > pts) {
      break;
    }
  }
  return best;
}

}  // namespace PJ
