#include "pj_media_core/h264_utils.h"

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <filesystem>
#include <string>

#include "test_mp4_demux.h"

namespace PJ {
namespace {

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

class H264UtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
    packets_ = test::extractAnnexBPackets(kTestVideo);
    ASSERT_GT(packets_.size(), 10u) << "need at least 10 packets for meaningful tests";
  }

  std::vector<test::AnnexBPacket> packets_;
};

TEST_F(H264UtilsTest, KeyframeDetectionAgreesWithDemuxer) {
  int keyframes_found = 0;
  int total_checked = 0;

  for (const auto& pkt : packets_) {
    bool detected = isH264Keyframe(pkt.data.data(), pkt.data.size());
    EXPECT_EQ(detected, pkt.keyframe) << "mismatch at packet ts=" << pkt.timestamp;
    if (detected) {
      ++keyframes_found;
    }
    ++total_checked;
  }

  EXPECT_GT(keyframes_found, 0) << "should find at least one keyframe";
  EXPECT_GT(total_checked, keyframes_found) << "should have some non-keyframes too";
}

TEST_F(H264UtilsTest, PFrameReturnsFalse) {
  for (const auto& pkt : packets_) {
    if (!pkt.keyframe) {
      EXPECT_FALSE(isH264Keyframe(pkt.data.data(), pkt.data.size()));
      return;
    }
  }
  FAIL() << "no P-frame found in test video";
}

TEST_F(H264UtilsTest, EmptyDataReturnsFalse) {
  EXPECT_FALSE(isH264Keyframe(nullptr, 0));
  EXPECT_FALSE(isH264Keyframe(nullptr, 100));

  std::array<uint8_t, 4> empty = {0, 0, 0, 0};
  EXPECT_FALSE(isH264Keyframe(empty.data(), 0));
  EXPECT_FALSE(isH264Keyframe(empty.data(), 3));
}

TEST_F(H264UtilsTest, MakeH264CodecParams) {
  AVCodecParameters* params = makeH264CodecParams();
  ASSERT_NE(params, nullptr);
  EXPECT_EQ(params->codec_id, AV_CODEC_ID_H264);
  EXPECT_EQ(params->codec_type, AVMEDIA_TYPE_VIDEO);
  avcodec_parameters_free(&params);
}

TEST_F(H264UtilsTest, AnnexBStartCodePresent) {
  for (const auto& pkt : packets_) {
    if (pkt.keyframe) {
      ASSERT_GE(pkt.data.size(), 5u);
      bool has_start_code =
          (pkt.data[0] == 0x00 && pkt.data[1] == 0x00 &&
           ((pkt.data[2] == 0x01) || (pkt.data[2] == 0x00 && pkt.data[3] == 0x01)));
      EXPECT_TRUE(has_start_code) << "keyframe should have annex-B start code";
      return;
    }
  }
}

}  // namespace
}  // namespace PJ
