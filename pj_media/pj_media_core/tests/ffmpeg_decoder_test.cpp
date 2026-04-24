#include "pj_media_core/ffmpeg_decoder.h"

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PJ {
namespace {

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

class FfmpegDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
  }
};

TEST_F(FfmpegDecoderTest, DecodeFirstFrame) {
  AVFormatContext* fmt_ctx = nullptr;
  ASSERT_GE(avformat_open_input(&fmt_ctx, kTestVideo.c_str(), nullptr, nullptr), 0);
  ASSERT_GE(avformat_find_stream_info(fmt_ctx, nullptr), 0);

  int video_idx = -1;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = static_cast<int>(i);
      break;
    }
  }
  ASSERT_GE(video_idx, 0);

  FfmpegDecoder decoder;
  ASSERT_TRUE(decoder.open(fmt_ctx->streams[video_idx]->codecpar));

  // Read and decode until we get a frame
  AVPacket* pkt = av_packet_alloc();
  DecodedFrame frame;
  int packets_sent = 0;

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    auto result = decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
    av_packet_unref(pkt);
    ++packets_sent;

    if (result.has_value() && !result->isNull()) {
      frame = std::move(*result);
      break;
    }
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);

  ASSERT_FALSE(frame.isNull()) << "no frame decoded after " << packets_sent << " packets";
  EXPECT_EQ(frame.width, 640);
  EXPECT_EQ(frame.height, 480);
  EXPECT_EQ(frame.format, PixelFormat::kYUV420P);
  EXPECT_EQ(frame.pixels->size(), expectedBufferSize(640, 480, PixelFormat::kYUV420P));
  EXPECT_TRUE(frame.isValid());
}

// C2 contract: decode() must never return has_value() with isNull() frame.
// Every successful decode must produce a valid frame.
TEST_F(FfmpegDecoderTest, SuccessfulDecodeNeverReturnsNullFrame) {
  AVFormatContext* fmt_ctx = nullptr;
  ASSERT_GE(avformat_open_input(&fmt_ctx, kTestVideo.c_str(), nullptr, nullptr), 0);
  ASSERT_GE(avformat_find_stream_info(fmt_ctx, nullptr), 0);

  int video_idx = -1;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = static_cast<int>(i);
      break;
    }
  }
  ASSERT_GE(video_idx, 0);

  FfmpegDecoder decoder;
  ASSERT_TRUE(decoder.open(fmt_ctx->streams[video_idx]->codecpar));

  AVPacket* pkt = av_packet_alloc();
  int total_results = 0;

  while (av_read_frame(fmt_ctx, pkt) >= 0 && total_results < 30) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    auto result = decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
    av_packet_unref(pkt);

    if (result.has_value()) {
      // Contract: if decode returns success, the frame must not be null
      EXPECT_FALSE(result->isNull()) << "decode() returned success but null frame at result #" << total_results;
      ++total_results;
    }
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);

  EXPECT_GT(total_results, 0) << "should have decoded at least one frame";
}

TEST_F(FfmpegDecoderTest, DecodeMultipleFrames) {
  AVFormatContext* fmt_ctx = nullptr;
  ASSERT_GE(avformat_open_input(&fmt_ctx, kTestVideo.c_str(), nullptr, nullptr), 0);
  ASSERT_GE(avformat_find_stream_info(fmt_ctx, nullptr), 0);

  int video_idx = -1;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = static_cast<int>(i);
      break;
    }
  }
  ASSERT_GE(video_idx, 0);

  FfmpegDecoder decoder;
  ASSERT_TRUE(decoder.open(fmt_ctx->streams[video_idx]->codecpar));

  AVPacket* pkt = av_packet_alloc();
  int decoded_count = 0;

  while (av_read_frame(fmt_ctx, pkt) >= 0 && decoded_count < 10) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    auto result = decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
    av_packet_unref(pkt);

    if (result.has_value() && !result->isNull()) {
      ++decoded_count;
    }
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);

  EXPECT_GE(decoded_count, 5) << "should decode at least 5 frames from the first packets";
}

TEST_F(FfmpegDecoderTest, FlushAndResume) {
  AVFormatContext* fmt_ctx = nullptr;
  ASSERT_GE(avformat_open_input(&fmt_ctx, kTestVideo.c_str(), nullptr, nullptr), 0);
  ASSERT_GE(avformat_find_stream_info(fmt_ctx, nullptr), 0);

  int video_idx = -1;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = static_cast<int>(i);
      break;
    }
  }

  FfmpegDecoder decoder;
  ASSERT_TRUE(decoder.open(fmt_ctx->streams[video_idx]->codecpar));

  // Decode a few frames, then flush (simulating a seek)
  AVPacket* pkt = av_packet_alloc();
  int count = 0;
  while (av_read_frame(fmt_ctx, pkt) >= 0 && count < 5) {
    if (pkt->stream_index == video_idx) {
      decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
      ++count;
    }
    av_packet_unref(pkt);
  }

  decoder.flush();

  // Continue decoding after flush — should not crash
  int post_flush = 0;
  while (av_read_frame(fmt_ctx, pkt) >= 0 && post_flush < 5) {
    if (pkt->stream_index == video_idx) {
      auto result = decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts);
      if (result.has_value() && !result->isNull()) {
        ++post_flush;
      }
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);

  EXPECT_GT(post_flush, 0) << "should decode frames after flush";
}

TEST_F(FfmpegDecoderTest, CancelStopsEarly) {
  AVFormatContext* fmt_ctx = nullptr;
  ASSERT_GE(avformat_open_input(&fmt_ctx, kTestVideo.c_str(), nullptr, nullptr), 0);
  ASSERT_GE(avformat_find_stream_info(fmt_ctx, nullptr), 0);

  int video_idx = -1;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = static_cast<int>(i);
      break;
    }
  }

  FfmpegDecoder decoder;
  ASSERT_TRUE(decoder.open(fmt_ctx->streams[video_idx]->codecpar));

  auto token = makeCancelToken();
  token->cancel();

  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == video_idx) {
      auto result = decoder.decode(pkt->data, static_cast<size_t>(pkt->size), pkt->pts, token);
      av_packet_unref(pkt);
      EXPECT_FALSE(result.has_value());
      break;
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);
}

}  // namespace
}  // namespace PJ
