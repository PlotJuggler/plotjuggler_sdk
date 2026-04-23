#pragma once

/// Shared test helper: extract annex-B H.264 packets from an MP4 file.
/// Used by h264_utils_test and streaming_video_decoder_test.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ::test {

struct AnnexBPacket {
  std::vector<uint8_t> data;
  Timestamp timestamp = 0;  // PTS in nanoseconds (presentation order)
  Timestamp dts = 0;        // DTS in nanoseconds (decode order, always monotonic)
  bool keyframe = false;    // from demuxer's AV_PKT_FLAG_KEY
};

/// Extract all video packets from an MP4 file, converting to annex-B format.
/// Timestamps are converted to nanoseconds.
inline std::vector<AnnexBPacket> extractAnnexBPackets(const std::string& path) {
  std::vector<AnnexBPacket> packets;

  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
    return packets;
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
    return packets;
  }

  double time_base = av_q2d(fmt_ctx->streams[video_idx]->time_base);

  const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
  AVBSFContext* bsf_ctx = nullptr;
  av_bsf_alloc(bsf, &bsf_ctx);
  avcodec_parameters_copy(bsf_ctx->par_in, fmt_ctx->streams[video_idx]->codecpar);
  bsf_ctx->time_base_in = fmt_ctx->streams[video_idx]->time_base;
  av_bsf_init(bsf_ctx);

  AVPacket* pkt = av_packet_alloc();
  AVPacket* filtered = av_packet_alloc();

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
    int64_t pts = pkt->pts;
    int64_t pkt_dts = pkt->dts;

    if (av_bsf_send_packet(bsf_ctx, pkt) >= 0) {
      while (av_bsf_receive_packet(bsf_ctx, filtered) >= 0) {
        AnnexBPacket ap;
        ap.data.assign(filtered->data, filtered->data + filtered->size);
        ap.timestamp = static_cast<Timestamp>(static_cast<double>(pts) * time_base * 1'000'000'000.0);
        ap.dts = static_cast<Timestamp>(static_cast<double>(pkt_dts) * time_base * 1'000'000'000.0);
        ap.keyframe = is_key;
        packets.push_back(std::move(ap));
        av_packet_unref(filtered);
      }
    }
    av_packet_unref(pkt);
  }

  av_packet_free(&pkt);
  av_packet_free(&filtered);
  av_bsf_free(&bsf_ctx);
  avformat_close_input(&fmt_ctx);

  return packets;
}

}  // namespace PJ::test
