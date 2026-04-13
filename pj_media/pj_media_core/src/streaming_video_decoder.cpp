#include "pj_media_core/streaming_video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>

#include "pj_media_core/ffmpeg_decoder.h"
#include "pj_media_core/h264_utils.h"

namespace PJ {

StreamingVideoDecoder::StreamingVideoDecoder() = default;

StreamingVideoDecoder::~StreamingVideoDecoder() = default;

void StreamingVideoDecoder::attach(ObjectStore* store, ObjectTopicId topic) {
  reset();
  store_ = store;
  topic_ = topic;
}

Expected<DecodedFrame> StreamingVideoDecoder::decodeAt(Timestamp ts) {
  if (store_ == nullptr) {
    return unexpected("not attached");
  }

  updateKeyframeIndex();

  auto target_idx_opt = store_->indexAt(topic_, ts);
  if (!target_idx_opt.has_value()) {
    return unexpected("no entry at timestamp");
  }
  size_t target_idx = *target_idx_opt;

  auto target_entry = store_->at(topic_, target_idx);
  if (!target_entry.has_value()) {
    return unexpected("entry not available");
  }
  Timestamp target_ts = target_entry->timestamp;

  // Fast path: forward from current position (normal live-mode path).
  // Also handles the case where the original keyframe was evicted by retention.
  // Equal timestamp = display polling faster than push rate — return cached frame.
  if (initialized_ && last_sent_ts_.has_value() && target_ts >= *last_sent_ts_) {
    if (target_ts == *last_sent_ts_ && !last_frame_.isNull()) {
      return last_frame_;
    }
    // Find first unsent entry. If last_sent_ts_ was evicted,
    // all remaining entries are newer — start from 0.
    size_t start_idx = 0;
    auto start_idx_opt = store_->indexAt(topic_, *last_sent_ts_);
    if (start_idx_opt.has_value()) {
      start_idx = *start_idx_opt + 1;
    }
    return decodeRange(start_idx, target_idx);
  }

  // Seek path: backward seek, first decode, or uninitialized.
  if (keyframe_timestamps_.empty()) {
    return unexpected("no keyframe yet");
  }
  Timestamp kf_ts = findKeyframeBefore(target_ts);
  if (kf_ts < 0) {
    return unexpected("no keyframe before target");
  }

  auto kf_idx_opt = store_->indexAt(topic_, kf_ts);
  if (!kf_idx_opt.has_value()) {
    return unexpected("keyframe evicted — target undecodable");
  }
  size_t kf_idx = *kf_idx_opt;

  if (!initialized_) {
    auto kf_entry = store_->at(topic_, kf_idx);
    if (!kf_entry.has_value()) {
      return unexpected("keyframe entry not available");
    }
    if (!initDecoder(kf_entry->data->data(), kf_entry->data->size())) {
      return unexpected("failed to initialize decoder");
    }
  }

  decoder_->flush();
  last_sent_ts_.reset();

  return decodeRange(kf_idx, target_idx);
}

Expected<DecodedFrame> StreamingVideoDecoder::decodeRange(size_t start_idx, size_t target_idx) {
  DecodedFrame result;
  for (size_t i = start_idx; i <= target_idx; ++i) {
    auto entry = store_->at(topic_, i);
    if (!entry.has_value()) {
      // Missing entry mid-GOP — the decode chain is broken.
      // Return error rather than producing corrupt output.
      return unexpected("entry evicted mid-GOP (index " + std::to_string(i) + ")");
    }
    // Track position by ObjectStore timestamp (= DTS for B-frame videos).
    last_sent_ts_ = entry->timestamp;
    if (i < target_idx) {
      decoder_->decodeSkip(entry->data->data(), entry->data->size(), entry->timestamp);
    } else {
      auto frame = decoder_->decode(entry->data->data(), entry->data->size(), entry->timestamp);
      if (frame.has_value() && !frame->isNull()) {
        result = std::move(*frame);
      }
    }
  }
  if (!result.isNull()) {
    last_frame_ = result;
    return result;
  }
  return unexpected("forward decode produced no frame");
}

void StreamingVideoDecoder::reset() {
  decoder_.reset();
  initialized_ = false;
  keyframe_timestamps_.clear();
  last_scanned_ts_.reset();
  last_sent_ts_.reset();
  last_frame_ = {};
}

bool StreamingVideoDecoder::isInitialized() const {
  return initialized_;
}

void StreamingVideoDecoder::updateKeyframeIndex() {
  if (store_ == nullptr) {
    return;
  }

  size_t count = store_->entryCount(topic_);
  if (count == 0) {
    return;
  }

  // Prune keyframe timestamps that have been evicted
  if (!keyframe_timestamps_.empty()) {
    auto range = store_->timeRange(topic_);
    auto it = std::lower_bound(keyframe_timestamps_.begin(), keyframe_timestamps_.end(), range.first);
    if (it != keyframe_timestamps_.begin()) {
      keyframe_timestamps_.erase(keyframe_timestamps_.begin(), it);
    }
  }

  // Scan entries we haven't seen yet. Track by timestamp (not count)
  // because retention can keep count constant while new entries replace old.
  size_t start = 0;
  if (last_scanned_ts_.has_value()) {
    auto idx_opt = store_->indexAt(topic_, *last_scanned_ts_);
    if (idx_opt.has_value()) {
      start = *idx_opt + 1;
    }
  }

  for (size_t i = start; i < count; ++i) {
    auto entry = store_->at(topic_, i);
    if (!entry.has_value()) {
      continue;
    }
    if (isH264Keyframe(entry->data->data(), entry->data->size())) {
      keyframe_timestamps_.push_back(entry->timestamp);
    }
    last_scanned_ts_ = entry->timestamp;
  }
}

Timestamp StreamingVideoDecoder::findKeyframeBefore(Timestamp ts) const {
  if (keyframe_timestamps_.empty()) {
    return -1;
  }
  auto it = std::upper_bound(keyframe_timestamps_.begin(), keyframe_timestamps_.end(), ts);
  if (it == keyframe_timestamps_.begin()) {
    return -1;
  }
  --it;
  return *it;
}

bool StreamingVideoDecoder::initDecoder(const uint8_t* data, size_t size) {
  AVCodecParameters* params = makeH264CodecParams(data, size);
  if (params == nullptr) {
    return false;
  }

  decoder_ = std::make_unique<FfmpegDecoder>();
  bool ok = decoder_->open(params);
  avcodec_parameters_free(&params);

  if (!ok) {
    decoder_.reset();
    return false;
  }

  initialized_ = true;
  return true;
}

}  // namespace PJ
