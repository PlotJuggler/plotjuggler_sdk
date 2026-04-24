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

  // Compute frame interval for pacing
  if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
    frame_interval_us_ = static_cast<int64_t>(1'000'000.0 * stream->avg_frame_rate.den / stream->avg_frame_rate.num);
  } else {
    frame_interval_us_ = 33333;  // default 30 fps
  }

  if (!decoder_->open(stream->codecpar)) {
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Build frame index
  frame_index_.clear();
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt_ctx_, pkt) >= 0) {
    if (pkt->stream_index == video_stream_idx_) {
      frame_index_.push_back({pkt->pts, pkt->dts, (pkt->flags & AV_PKT_FLAG_KEY) != 0});
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);

  std::sort(
      frame_index_.begin(), frame_index_.end(), [](const FrameIndex& a, const FrameIndex& b) { return a.pts < b.pts; });

  av_seek_frame(fmt_ctx_, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
  decoder_->flush();

  // Buffer open-time events for processEvents() delivery.
  // open() runs on the caller's thread, but callbacks should be
  // delivered consistently via processEvents() polling.
  {
    std::lock_guard plock(pending_mutex_);
    pending_file_loaded_ = true;
  }

  running_.store(true);
  paused_.store(true);
  position_sec_.store(0.0);
  thread_ = std::thread(&FfmpegBackend::decodeThread, this);

  thumbnail_cache_.buildAsync(path, duration_sec_);

  return true;
}

void FfmpegBackend::close() {
  thumbnail_cache_.stop();
  if (running_.load()) {
    running_.store(false);
    paused_.store(false);  // unblock the wait
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  // Thread is fully stopped — safe to free resources
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
  if (cancel_token_) {
    cancel_token_->cancel();
  }
  cancel_token_ = makeCancelToken();
  requested_pts_ = target_pts;
  has_request_ = true;
  seek_pending_ = true;
  cv_.notify_one();
}

void FfmpegBackend::setPaused(bool paused) {
  paused_.store(paused);
  cv_.notify_one();
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
  // Read pending state from decode thread (latest-wins, like FrameSlot).
  // Swap out under lock, fire callbacks outside lock.
  std::optional<double> pos;
  bool file_loaded = false;
  DecodedFrame frame;

  {
    std::lock_guard lock(pending_mutex_);
    pos = pending_position_;
    pending_position_.reset();
    file_loaded = pending_file_loaded_;
    pending_file_loaded_ = false;
    frame = std::move(pending_frame_);
  }

  if (file_loaded) {
    if (on_duration_) {
      on_duration_(duration_sec_);
    }
  }
  if (file_loaded && on_file_loaded_) {
    on_file_loaded_();
  }
  if (pos.has_value() && on_position_) {
    on_position_(*pos);
  }
  if (!frame.isNull() && on_frame_) {
    on_frame_(frame);
  }
}

void FfmpegBackend::publishFrame(const DecodedFrame& frame) {
  if (frame.pts < 0) {
    return;  // Invalid PTS — don't publish
  }
  double pos = static_cast<double>(frame.pts) * time_base_;
  position_sec_.store(pos);
  std::lock_guard plock(pending_mutex_);
  pending_position_ = pos;
  pending_frame_ = frame;
  last_published_pos_ = pos;
}

// ---------------------------------------------------------------------------
// Background decode thread
// ---------------------------------------------------------------------------

void FfmpegBackend::decodeThread() {
  using Clock = std::chrono::steady_clock;
  auto next_frame_time = Clock::now();
  bool was_paused = true;

  while (running_.load()) {
    int64_t target = -1;
    bool do_seek = false;

    {
      std::unique_lock lock(mutex_);
      cv_.wait_for(
          lock, std::chrono::milliseconds(8), [this]() { return !running_.load() || has_request_ || !paused_.load(); });

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

    // Reset frame pacing on unpause so we don't blast stale frames
    bool is_paused = paused_.load();
    if (was_paused && !is_paused) {
      next_frame_time = Clock::now();
    }
    was_paused = is_paused;

    if (do_seek && target >= 0) {
      scrub_backward_ = (last_request_pts_ >= 0) && (target < last_request_pts_);
      last_request_pts_ = target;

      // Forward threshold (video_player_lab kForwardThreshold):
      // If target is ahead of current decoded position and within 30 frames,
      // don't seek — just continue decoding forward. Avoids flush penalty.
      static constexpr int kForwardThreshold = 100;
      int64_t forward_gap = target - last_decoded_pts_;
      int64_t threshold_pts =
          static_cast<int64_t>(kForwardThreshold) * frame_interval_us_ / static_cast<int64_t>(time_base_ * 1'000'000);
      bool need_seek = last_decoded_pts_ < 0 || target <= last_decoded_pts_ || forward_gap > threshold_pts;

      int64_t kf_pts = findKeyframeBefore(target);
      if (need_seek) {
        av_seek_frame(fmt_ctx_, video_stream_idx_, kf_pts, AVSEEK_FLAG_BACKWARD);
        decoder_->flush();
        last_decoded_pts_ = kf_pts;
      }
      // Always allow partials: decodeSkip prevents the "forward replay from
      // keyframe" artifact that the lab suppressed partials for. Intermediate
      // frames are never fully decoded, so partials are always near the target.
      decodeAndDeliver(target, /*allow_partial=*/true, kf_pts);
      next_frame_time = Clock::now();
    } else if (!paused_.load()) {
      // Frame pacing: wait until it's time for the next frame
      auto now = Clock::now();
      if (now < next_frame_time) {
        std::this_thread::sleep_until(next_frame_time);
      }

      // Check for interruption after sleep
      if (!running_.load()) {
        break;
      }
      {
        std::lock_guard lock(mutex_);
        if (has_request_) {
          continue;  // New seek request arrived during sleep
        }
      }

      AVPacket* pkt = av_packet_alloc();
      if (av_read_frame(fmt_ctx_, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx_) {
          auto result = decoder_->decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
          if (result.has_value() && !result->isNull()) {
            last_decoded_pts_ = result->pts;
            publishFrame(*result);
            next_frame_time += std::chrono::microseconds(frame_interval_us_);
          }
        }
        av_packet_unref(pkt);
      } else {
        paused_.store(true);
      }
      av_packet_free(&pkt);
    }
  }
}

void FfmpegBackend::decodeAndDeliver(int64_t target_pts, bool allow_partial, int64_t current_kf) {
  CancelTokenPtr cancel;
  {
    std::lock_guard lock(mutex_);
    cancel = cancel_token_;
  }

  using Clock = std::chrono::steady_clock;
  DecodedFrame last_good;
  auto last_full_decode = Clock::now();
  auto decode_start = Clock::now();
  static constexpr int kFullDecodeIntervalMs = 30;
  static constexpr int kMinDecodeTimeMs = 60;

  // Backward scrub: publish cached thumbnail eagerly for instant feedback.
  // Completions are filtered — only published if they don't jump forward
  // from the thumbnail (prevents settle flicker).
  // Forward scrub: publish periodic partials for smooth feedback.
  // Only publish thumbnail if genuinely moving backward from current display.
  // Prevents false triggers from rounding differences on slider release.
  if (scrub_backward_) {
    double target_sec = static_cast<double>(target_pts) * time_base_;
    if (last_published_pos_ < 0 || target_sec < last_published_pos_ - 0.1) {
      auto cached = thumbnail_cache_.lookup(target_sec);
      if (cached.has_value()) {
        cached->pts = target_pts;
        publishFrame(*cached);
      }
    }
  }

  bool published = false;
  bool broke_early = false;
  bool min_time_elapsed = false;
  AVPacket* pkt = av_packet_alloc();

  while (av_read_frame(fmt_ctx_, pkt) >= 0) {
    if (pkt->stream_index != video_stream_idx_) {
      av_packet_unref(pkt);
      continue;
    }

    int64_t pts_per_frame = frame_interval_us_ / static_cast<int64_t>(time_base_ * 1'000'000);
    bool near_target = pkt->pts >= target_pts || (target_pts - pkt->pts) <= pts_per_frame * 5;

    // For forward scrub: periodic full decode for partials.
    // For backward scrub: only near target (no intermediate partials).
    bool time_for_partial =
        !scrub_backward_ && (Clock::now() - last_full_decode) >= std::chrono::milliseconds(kFullDecodeIntervalMs);
    bool do_full_decode = near_target || time_for_partial;

    if (!do_full_decode) {
      int64_t decoded_pts = decoder_->decodeSkip(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
      av_packet_unref(pkt);
      if (decoded_pts >= 0) {
        last_decoded_pts_ = decoded_pts;
      }
    } else {
      bool cancel_allowed = (Clock::now() - decode_start) >= std::chrono::milliseconds(kMinDecodeTimeMs);
      auto effective_cancel = cancel_allowed ? cancel : nullptr;
      auto result = decoder_->decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts, effective_cancel);
      av_packet_unref(pkt);

      if (!result.has_value()) {
        if (effective_cancel && effective_cancel->isCancelled()) {
          broke_early = true;
          break;
        }
        continue;
      }

      if (!result->isNull()) {
        last_good = *result;
        last_decoded_pts_ = result->pts;
        last_full_decode = Clock::now();

        if (result->pts >= target_pts) {
          // Completion: publish unless it would jump forward from the
          // thumbnail during backward scrub (prevents settle flicker).
          double completion_pos = static_cast<double>(result->pts) * time_base_;
          if (!scrub_backward_ || last_published_pos_ < 0 || completion_pos < last_published_pos_) {
            publishFrame(*result);
          }
          published = true;
          broke_early = true;
          break;
        }

        // Forward scrub only: publish periodic partials for smooth feedback.
        // Backward scrub: no partials — only completions are published,
        // guaranteeing monotonically decreasing positions.
        if (!scrub_backward_ && allow_partial) {
          publishFrame(*result);
          published = true;
        }
      }
    }

    // Cancellation checks — only after minimum decode time
    if (!running_.load()) {
      broke_early = true;
      break;
    }
    min_time_elapsed = (Clock::now() - decode_start) >= std::chrono::milliseconds(kMinDecodeTimeMs);
    if (min_time_elapsed) {
      if (cancel && cancel->isCancelled()) {
        broke_early = true;
        break;
      }
      {
        std::lock_guard lock(mutex_);
        if (has_request_) {
          int64_t new_target = requested_pts_;
          int64_t new_kf = findKeyframeBefore(new_target);
          // Target refinement: same GOP, closer target → just lower target
          if (new_kf == current_kf && new_target < target_pts && new_target > last_decoded_pts_) {
            target_pts = new_target;
            has_request_ = false;
            seek_pending_ = false;
            last_request_pts_ = new_target;
            cancel = cancel_token_;
            decode_start = Clock::now();
          } else {
            broke_early = true;
            break;
          }
        }
      }
    }
  }

  // EOF drain: only if loop exited naturally (not via break/cancel).
  // Drains remaining buffered frames (B-frames waiting for reference).
  if (!broke_early && !published) {
    auto trailing = decoder_->drain();
    for (auto& frame : trailing) {
      if (!frame.isNull()) {
        last_decoded_pts_ = frame.pts;
        if (frame.pts >= target_pts) {
          publishFrame(frame);
          published = true;
          break;
        }
        last_good = frame;
      }
    }
    // If drain didn't reach target but we have the best frame, publish it
    if (!published && !last_good.isNull()) {
      publishFrame(last_good);
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
