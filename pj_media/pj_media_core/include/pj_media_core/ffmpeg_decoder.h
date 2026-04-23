#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_media_core/cancel_token.h"
#include "pj_media_core/decoded_frame.h"

struct AVCodecContext;
struct AVCodecParameters;
struct AVBufferRef;
struct AVFrame;
struct SwsContext;

namespace PJ {

/// FFmpeg decoder wrapper: takes compressed video packets, produces DecodedFrame.
///
/// Supports hardware acceleration (VAAPI → CUDA → software fallback).
/// Single-threaded — codec state is inherently sequential. One instance per
/// video layer per viewer widget. Ported from video_player_lab/HWDecoder.
class FfmpegDecoder {
 public:
  FfmpegDecoder();
  ~FfmpegDecoder();

  FfmpegDecoder(const FfmpegDecoder&) = delete;
  FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;
  FfmpegDecoder(FfmpegDecoder&&) = delete;
  FfmpegDecoder& operator=(FfmpegDecoder&&) = delete;

  /// Open decoder from codec parameters (obtained from AVStream).
  /// Tries VAAPI → CUDA → software fallback.
  bool open(const AVCodecParameters* params);

  /// Decode a compressed packet into a YUV420P DecodedFrame.
  /// Returns empty Expected on EAGAIN (need more packets) or error.
  /// Checks cancel token between send and receive.
  Expected<DecodedFrame> decode(const uint8_t* data, size_t size, int64_t pts, const CancelTokenPtr& cancel = nullptr);

  /// Send packet and receive frame, but skip HW transfer and sws_scale.
  /// Returns only the PTS of the decoded frame (or -1 on EAGAIN/error).
  /// Use this for intermediate frames during seek-forward where we need
  /// to advance the decoder but don't need pixels.
  int64_t decodeSkip(const uint8_t* data, size_t size, int64_t pts);

  /// Flush decoder state — mandatory after seek.
  void flush();

  /// Drain remaining buffered frames (call at EOF).
  std::vector<DecodedFrame> drain();

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

 private:
  Expected<DecodedFrame> avFrameToDecodedFrame(AVFrame* frame);

  AVCodecContext* codec_ctx_ = nullptr;
  AVBufferRef* hw_device_ctx_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  int sws_src_w_ = 0;
  int sws_src_h_ = 0;
  int sws_src_fmt_ = -1;
};

}  // namespace PJ
