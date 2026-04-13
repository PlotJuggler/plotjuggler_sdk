#include "pj_media_core/ffmpeg_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <cstring>

namespace PJ {

namespace {

AVBufferRef* tryHwDevice(AVHWDeviceType type) {
  AVBufferRef* ctx = nullptr;
  if (av_hwdevice_ctx_create(&ctx, type, nullptr, nullptr, 0) >= 0) {
    return ctx;
  }
  return nullptr;
}

}  // namespace

FfmpegDecoder::FfmpegDecoder() = default;

FfmpegDecoder::~FfmpegDecoder() {
  if (sws_ctx_ != nullptr) {
    sws_freeContext(sws_ctx_);
  }
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
  if (hw_device_ctx_ != nullptr) {
    av_buffer_unref(&hw_device_ctx_);
  }
}

bool FfmpegDecoder::open(const AVCodecParameters* params) {
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
    codec_ctx_ = nullptr;
  }
  if (hw_device_ctx_ != nullptr) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }
  if (sws_ctx_ != nullptr) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
  }

  const AVCodec* codec = avcodec_find_decoder(params->codec_id);
  if (codec == nullptr) {
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (codec_ctx_ == nullptr) {
    return false;
  }

  if (avcodec_parameters_to_context(codec_ctx_, params) < 0) {
    avcodec_free_context(&codec_ctx_);
    return false;
  }

  // Try HW acceleration: VAAPI → CUDA → software
  static const AVHWDeviceType kHwTypes[] = {
      AV_HWDEVICE_TYPE_VAAPI,
      AV_HWDEVICE_TYPE_CUDA,
  };
  for (auto type : kHwTypes) {
    hw_device_ctx_ = tryHwDevice(type);
    if (hw_device_ctx_ != nullptr) {
      codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
      break;
    }
  }

  codec_ctx_->thread_count = 4;

  if (hw_device_ctx_ != nullptr) {
    fprintf(
        stderr, "[FfmpegDecoder] HW accel: %s\n",
        av_hwdevice_get_type_name(
            reinterpret_cast<AVHWDeviceContext*>(reinterpret_cast<AVBufferRef*>(hw_device_ctx_)->data)->type));
  } else {
    fprintf(stderr, "[FfmpegDecoder] HW accel: none (software decode)\n");
  }

  if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
    avcodec_free_context(&codec_ctx_);
    if (hw_device_ctx_ != nullptr) {
      av_buffer_unref(&hw_device_ctx_);
    }
    return false;
  }

  return true;
}

Expected<DecodedFrame> FfmpegDecoder::decode(
    const uint8_t* data, size_t size, int64_t pts, const CancelTokenPtr& cancel) {
  if (codec_ctx_ == nullptr) {
    return unexpected("decoder not open");
  }

  AVPacket* pkt = av_packet_alloc();
  pkt->data = const_cast<uint8_t*>(data);
  pkt->size = static_cast<int>(size);
  pkt->pts = pts;
  pkt->dts = pts;

  int ret = avcodec_send_packet(codec_ctx_, pkt);
  if (ret == AVERROR(ENOMEM)) {
    // Surface pool exhaustion — flush and retry once
    avcodec_flush_buffers(codec_ctx_);
    ret = avcodec_send_packet(codec_ctx_, pkt);
  }
  av_packet_free(&pkt);

  if (ret < 0 && ret != AVERROR(EAGAIN)) {
    return unexpected("avcodec_send_packet failed");
  }

  if (cancel != nullptr && cancel->isCancelled()) {
    return unexpected("cancelled");
  }

  AVFrame* frame = av_frame_alloc();
  ret = avcodec_receive_frame(codec_ctx_, frame);
  if (ret < 0) {
    av_frame_free(&frame);
    if (ret == AVERROR(EAGAIN)) {
      return unexpected("need more packets");
    }
    return unexpected("avcodec_receive_frame failed");
  }

  auto result = avFrameToDecodedFrame(frame);
  result.pts = frame->pts;
  av_frame_free(&frame);
  return result;
}

int64_t FfmpegDecoder::decodeSkip(const uint8_t* data, size_t size, int64_t pts) {
  if (codec_ctx_ == nullptr) {
    return -1;
  }

  AVPacket* pkt = av_packet_alloc();
  pkt->data = const_cast<uint8_t*>(data);
  pkt->size = static_cast<int>(size);
  pkt->pts = pts;
  pkt->dts = pts;

  int ret = avcodec_send_packet(codec_ctx_, pkt);
  av_packet_free(&pkt);

  if (ret < 0 && ret != AVERROR(EAGAIN)) {
    return -1;
  }

  AVFrame* frame = av_frame_alloc();
  ret = avcodec_receive_frame(codec_ctx_, frame);
  int64_t result_pts = (ret >= 0) ? frame->pts : -1;
  av_frame_free(&frame);
  return result_pts;
}

void FfmpegDecoder::flush() {
  if (codec_ctx_ != nullptr) {
    avcodec_flush_buffers(codec_ctx_);
  }
}

std::vector<DecodedFrame> FfmpegDecoder::drain() {
  std::vector<DecodedFrame> frames;
  if (codec_ctx_ == nullptr) {
    return frames;
  }

  // Send NULL packet to drain
  avcodec_send_packet(codec_ctx_, nullptr);

  AVFrame* frame = av_frame_alloc();
  while (avcodec_receive_frame(codec_ctx_, frame) >= 0) {
    frames.push_back(avFrameToDecodedFrame(frame));
    av_frame_unref(frame);
  }
  av_frame_free(&frame);
  return frames;
}

int FfmpegDecoder::width() const {
  return codec_ctx_ != nullptr ? codec_ctx_->width : 0;
}

int FfmpegDecoder::height() const {
  return codec_ctx_ != nullptr ? codec_ctx_->height : 0;
}

DecodedFrame FfmpegDecoder::avFrameToDecodedFrame(AVFrame* frame) {
  AVFrame* sw_frame = frame;
  AVFrame* tmp_frame = nullptr;

  // Transfer HW frame to CPU if needed
  if (frame->hw_frames_ctx != nullptr) {
    tmp_frame = av_frame_alloc();
    if (av_hwframe_transfer_data(tmp_frame, frame, 0) < 0) {
      av_frame_free(&tmp_frame);
      return {};
    }
    sw_frame = tmp_frame;
  }

  int w = sw_frame->width;
  int h = sw_frame->height;
  auto src_fmt = static_cast<AVPixelFormat>(sw_frame->format);

  // Output YUV420P — no color conversion on CPU.
  // The GPU shader handles YUV→RGB with the correct BT.709 matrix.
  if (src_fmt == AV_PIX_FMT_YUV420P) {
    // Already YUV420P — just copy planes to contiguous buffer
    int uv_w = (w + 1) / 2;
    int uv_h = (h + 1) / 2;
    auto buf_size = expectedBufferSize(w, h, PixelFormat::kYUV420P);
    auto pixels = std::make_shared<std::vector<uint8_t>>(buf_size);
    uint8_t* dst = pixels->data();
    int y_size = w * h;
    int uv_size = uv_w * uv_h;

    // Y plane
    for (int row = 0; row < h; ++row) {
      std::memcpy(dst + row * w, sw_frame->data[0] + row * sw_frame->linesize[0], static_cast<size_t>(w));
    }
    // U plane
    for (int row = 0; row < uv_h; ++row) {
      std::memcpy(
          dst + y_size + row * uv_w, sw_frame->data[1] + row * sw_frame->linesize[1], static_cast<size_t>(uv_w));
    }
    // V plane
    for (int row = 0; row < uv_h; ++row) {
      std::memcpy(
          dst + y_size + uv_size + row * uv_w, sw_frame->data[2] + row * sw_frame->linesize[2],
          static_cast<size_t>(uv_w));
    }

    if (tmp_frame != nullptr) {
      av_frame_free(&tmp_frame);
    }

    DecodedFrame result;
    result.pixels = std::move(pixels);
    result.width = w;
    result.height = h;
    result.format = PixelFormat::kYUV420P;
    return result;
  }

  // Non-YUV420P (NV12, etc.) — convert to YUV420P via sws_scale
  if (sws_ctx_ == nullptr || w != sws_src_w_ || h != sws_src_h_ || static_cast<int>(src_fmt) != sws_src_fmt_) {
    if (sws_ctx_ != nullptr) {
      sws_freeContext(sws_ctx_);
    }
    sws_ctx_ = sws_getContext(w, h, src_fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    sws_src_w_ = w;
    sws_src_h_ = h;
    sws_src_fmt_ = static_cast<int>(src_fmt);
  }

  if (sws_ctx_ == nullptr) {
    if (tmp_frame != nullptr) {
      av_frame_free(&tmp_frame);
    }
    return {};
  }

  int uv_w = (w + 1) / 2;
  int uv_h = (h + 1) / 2;
  int y_size = w * h;
  int uv_size = uv_w * uv_h;
  auto pixels = std::make_shared<std::vector<uint8_t>>(expectedBufferSize(w, h, PixelFormat::kYUV420P));
  uint8_t* dst_planes[3] = {pixels->data(), pixels->data() + y_size, pixels->data() + y_size + uv_size};
  int dst_linesize[3] = {w, uv_w, uv_w};
  sws_scale(sws_ctx_, sw_frame->data, sw_frame->linesize, 0, h, dst_planes, dst_linesize);

  if (tmp_frame != nullptr) {
    av_frame_free(&tmp_frame);
  }

  DecodedFrame result;
  result.pixels = std::move(pixels);
  result.width = w;
  result.height = h;
  result.format = PixelFormat::kYUV420P;
  return result;
}

}  // namespace PJ
