#include "pj_media_core/streaming_video_source.h"

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "pj_datastore/object_store.hpp"

namespace PJ {
namespace {

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

/// Push all H.264 packets from an MP4 file into ObjectStore as annex-B NAL units.
size_t pushVideoPackets(const std::string& path, ObjectStore& store, ObjectTopicId topic) {
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
    return 0;
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
    return 0;
  }

  auto* stream = fmt_ctx->streams[video_idx];
  double time_base = av_q2d(stream->time_base);

  const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
  AVBSFContext* bsf_ctx = nullptr;
  av_bsf_alloc(bsf, &bsf_ctx);
  avcodec_parameters_copy(bsf_ctx->par_in, stream->codecpar);
  bsf_ctx->time_base_in = stream->time_base;
  av_bsf_init(bsf_ctx);

  AVPacket* pkt = av_packet_alloc();
  AVPacket* filtered = av_packet_alloc();
  size_t count = 0;

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }
    int64_t dts_ns = static_cast<int64_t>(static_cast<double>(pkt->dts) * time_base * 1'000'000'000.0);

    if (av_bsf_send_packet(bsf_ctx, pkt) >= 0) {
      while (av_bsf_receive_packet(bsf_ctx, filtered) >= 0) {
        std::vector<uint8_t> data(filtered->data, filtered->data + filtered->size);
        store.pushOwned(topic, dts_ns, std::move(data));
        ++count;
        av_packet_unref(filtered);
      }
    }
    av_packet_unref(pkt);
  }

  av_packet_free(&pkt);
  av_packet_free(&filtered);
  av_bsf_free(&bsf_ctx);
  avformat_close_input(&fmt_ctx);
  return count;
}

class StreamingVideoSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
  }
};

TEST_F(StreamingVideoSourceTest, DecodeAtTimestamp) {
  ObjectStore store;
  auto topic_or = store.registerTopic({.dataset_id = 1, .topic_name = "video", .metadata_json = "{}"});
  ASSERT_TRUE(topic_or.has_value());
  auto topic = *topic_or;

  size_t packet_count = pushVideoPackets(kTestVideo, store, topic);
  ASSERT_GT(packet_count, 0u);

  auto [t_min, t_max] = store.timeRange(topic);

  StreamingVideoSource source(&store, topic);

  // Seek to the middle of the video
  int64_t mid_ts = (t_min + t_max) / 2;
  source.setTimestamp(mid_ts);

  // Poll for the decoded frame
  std::optional<DecodedFrame> frame;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!frame.has_value() && std::chrono::steady_clock::now() < deadline) {
    frame = source.takeFrame();
    if (!frame.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  ASSERT_TRUE(frame.has_value()) << "no frame decoded within 5 seconds";
  EXPECT_FALSE(frame->isNull());
  EXPECT_EQ(frame->width, 640);
  EXPECT_EQ(frame->height, 480);
  EXPECT_TRUE(frame->isValid());
}

TEST_F(StreamingVideoSourceTest, RapidTimestampChanges) {
  ObjectStore store;
  auto topic_or = store.registerTopic({.dataset_id = 1, .topic_name = "video", .metadata_json = "{}"});
  ASSERT_TRUE(topic_or.has_value());
  auto topic = *topic_or;

  pushVideoPackets(kTestVideo, store, topic);
  auto [t_min, t_max] = store.timeRange(topic);

  StreamingVideoSource source(&store, topic);

  // Rapid timestamp changes — should not crash
  for (int i = 0; i < 50; ++i) {
    int64_t ts = t_min + (t_max - t_min) * i / 50;
    source.setTimestamp(ts);
    source.takeFrame();  // may or may not have a result yet
  }

  // Wait for the last decode to complete
  std::optional<DecodedFrame> frame;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!frame.has_value() && std::chrono::steady_clock::now() < deadline) {
    frame = source.takeFrame();
    if (!frame.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  EXPECT_TRUE(frame.has_value()) << "should eventually produce a frame";
}

TEST_F(StreamingVideoSourceTest, IsInitializedAfterKeyframe) {
  ObjectStore store;
  auto topic_or = store.registerTopic({.dataset_id = 1, .topic_name = "video", .metadata_json = "{}"});
  ASSERT_TRUE(topic_or.has_value());
  auto topic = *topic_or;

  StreamingVideoSource source(&store, topic);
  EXPECT_FALSE(source.isInitialized());

  pushVideoPackets(kTestVideo, store, topic);
  auto [t_min, t_max] = store.timeRange(topic);
  source.setTimestamp(t_min);

  // Wait for initialization
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!source.isInitialized() && std::chrono::steady_clock::now() < deadline) {
    source.takeFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  EXPECT_TRUE(source.isInitialized());
}

}  // namespace
}  // namespace PJ
