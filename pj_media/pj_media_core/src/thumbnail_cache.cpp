#include "pj_media_core/thumbnail_cache.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <turbojpeg.h>

#include <algorithm>
#include <cstdio>

namespace PJ {

ThumbnailCache::ThumbnailCache() : tj_decompress_(tjInitDecompress()) {}

ThumbnailCache::~ThumbnailCache() {
  stop();
  if (tj_decompress_ != nullptr) {
    tjDestroy(tj_decompress_);
  }
}

void ThumbnailCache::buildAsync(const std::string& video_path, double duration_sec) {
  stop();
  running_.store(true);
  thread_ = std::thread(&ThumbnailCache::buildThread, this, video_path, duration_sec);
}

void ThumbnailCache::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::optional<DecodedFrame> ThumbnailCache::lookup(double seconds) const {
  std::lock_guard lock(mutex_);
  if (frames_.empty()) {
    return std::nullopt;
  }

  // Binary search: find the last frame at-or-before seconds
  auto it = std::upper_bound(
      frames_.begin(), frames_.end(), seconds, [](double t, const CachedFrame& f) { return t < f.timestamp; });
  if (it == frames_.begin()) {
    return std::nullopt;  // All frames are after the requested time
  }
  --it;

  // Decompress JPEG directly to YUV420P — same format as live decode.
  // No color space conversion needed; GPU shader handles YUV→RGB.
  if (tj_decompress_ == nullptr) {
    return std::nullopt;
  }

  int w = it->width;
  int h = it->height;
  auto pixels = std::make_shared<std::vector<uint8_t>>(expectedBufferSize(w, h, PixelFormat::kYUV420P));

  int ret = tjDecompressToYUV2(
      tj_decompress_, it->jpeg_data.data(), static_cast<unsigned long>(it->jpeg_data.size()), pixels->data(), w, 1, h,
      0);

  if (ret != 0) {
    return std::nullopt;
  }

  DecodedFrame frame;
  frame.pixels = std::move(pixels);
  frame.width = w;
  frame.height = h;
  frame.format = PixelFormat::kYUV420P;
  frame.pts = -1;
  return frame;
}

size_t ThumbnailCache::size() const {
  std::lock_guard lock(mutex_);
  return frames_.size();
}

size_t ThumbnailCache::memoryUsed() const {
  std::lock_guard lock(mutex_);
  return total_bytes_;
}

bool ThumbnailCache::isBuilding() const {
  return running_.load();
}

void ThumbnailCache::buildThread(std::string video_path, double duration_sec) {
  // Open a SEPARATE demuxer + decoder (independent from the playback one)
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, video_path.c_str(), nullptr, nullptr) < 0) {
    fprintf(stderr, "[ThumbnailCache] Failed to open: %s\n", video_path.c_str());
    running_.store(false);
    return;
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
    running_.store(false);
    return;
  }

  auto* stream = fmt_ctx->streams[video_idx];
  double time_base = av_q2d(stream->time_base);
  int src_w = stream->codecpar->width;
  int src_h = stream->codecpar->height;

  // Compute scaled dimensions
  int dst_w = src_w;
  int dst_h = src_h;
  if (dst_w > kMaxWidth) {
    dst_h = dst_h * kMaxWidth / dst_w;
    dst_w = kMaxWidth;
    dst_w &= ~1;
    dst_h &= ~1;
  }

  // Open decoder with VAAPI
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codec_ctx, stream->codecpar);

  AVBufferRef* hw_ctx = nullptr;
  if (av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0) >= 0) {
    codec_ctx->hw_device_ctx = av_buffer_ref(hw_ctx);
  }
  codec_ctx->thread_count = 2;
  avcodec_open2(codec_ctx, codec, nullptr);

  // sws for scaling + format conversion
  SwsContext* sws = nullptr;
  tjhandle tj = tjInitCompress();

  fprintf(stderr, "[ThumbnailCache] Building %dx%d → %dx%d, %.0fs video\n", src_w, src_h, dst_w, dst_h, duration_sec);

  for (double t = 0; t < duration_sec && running_.load(); t += 1.0) {
    int64_t target_pts = static_cast<int64_t>(t / time_base);
    av_seek_frame(fmt_ctx, video_idx, target_pts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec_ctx);

    // Decode forward to target
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got_frame = false;

    while (av_read_frame(fmt_ctx, pkt) >= 0 && running_.load()) {
      if (pkt->stream_index != video_idx) {
        av_packet_unref(pkt);
        continue;
      }
      int ret = avcodec_send_packet(codec_ctx, pkt);
      if (ret == AVERROR(ENOMEM)) {
        avcodec_flush_buffers(codec_ctx);
        ret = avcodec_send_packet(codec_ctx, pkt);
      }
      av_packet_unref(pkt);
      if (ret < 0) {
        continue;
      }

      while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
        if (frame->pts >= target_pts) {
          got_frame = true;
          break;
        }
        av_frame_unref(frame);
      }
      if (got_frame) {
        break;
      }
    }
    av_packet_free(&pkt);

    if (!got_frame || !running_.load()) {
      av_frame_free(&frame);
      continue;
    }

    // HW transfer if needed
    AVFrame* sw_frame = frame;
    AVFrame* tmp = nullptr;
    if (frame->hw_frames_ctx != nullptr) {
      tmp = av_frame_alloc();
      if (av_hwframe_transfer_data(tmp, frame, 0) < 0) {
        av_frame_free(&tmp);
        av_frame_free(&frame);
        continue;
      }
      sw_frame = tmp;
    }

    // Scale + convert to YUV420P
    auto src_fmt = static_cast<AVPixelFormat>(sw_frame->format);
    if (sws == nullptr) {
      sws = sws_getContext(
          src_w, src_h, src_fmt, dst_w, dst_h, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    }

    int dst_uv_w = (dst_w + 1) / 2;
    int dst_uv_h = (dst_h + 1) / 2;
    int y_size = dst_w * dst_h;
    int uv_size = dst_uv_w * dst_uv_h;
    std::vector<uint8_t> yuv_buf(expectedBufferSize(dst_w, dst_h, PixelFormat::kYUV420P));
    uint8_t* planes[3] = {yuv_buf.data(), yuv_buf.data() + y_size, yuv_buf.data() + y_size + uv_size};
    int strides[3] = {dst_w, dst_uv_w, dst_uv_w};
    sws_scale(sws, sw_frame->data, sw_frame->linesize, 0, src_h, planes, strides);

    if (tmp != nullptr) {
      av_frame_free(&tmp);
    }
    av_frame_free(&frame);

    // JPEG compress
    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;
    tjCompressFromYUV(
        tj, yuv_buf.data(), dst_w, 1, dst_h, TJSAMP_420, &jpeg_buf, &jpeg_size, kJpegQuality, TJFLAG_FASTDCT);

    if (jpeg_buf != nullptr) {
      CachedFrame cf;
      cf.timestamp = t;
      cf.jpeg_data.assign(jpeg_buf, jpeg_buf + jpeg_size);
      cf.width = dst_w;
      cf.height = dst_h;

      {
        std::lock_guard lock(mutex_);
        frames_.push_back(std::move(cf));
        total_bytes_ += jpeg_size;
      }

      tjFree(jpeg_buf);
    }
  }

  // Cleanup
  tjDestroy(tj);
  if (sws != nullptr) {
    sws_freeContext(sws);
  }
  avcodec_free_context(&codec_ctx);
  if (hw_ctx != nullptr) {
    av_buffer_unref(&hw_ctx);
  }
  avformat_close_input(&fmt_ctx);

  {
    std::lock_guard lock(mutex_);
    fprintf(
        stderr, "[ThumbnailCache] Done: %zu frames, %.1f MB\n", frames_.size(),
        static_cast<double>(total_bytes_) / (1024.0 * 1024.0));
  }
  running_.store(false);
}

}  // namespace PJ
