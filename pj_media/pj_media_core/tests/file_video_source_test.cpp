#include "pj_media_core/file_video_source.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace PJ {
namespace {

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

class FileVideoSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
  }
};

TEST_F(FileVideoSourceTest, OpenAndQueryDuration) {
  auto source_or = FileVideoSource::open(kTestVideo);
  ASSERT_TRUE(source_or.has_value()) << source_or.error();
  auto& source = *source_or;

  EXPECT_GT(source->duration(), 0.0);
}

TEST_F(FileVideoSourceTest, OpenNonexistentFile) {
  auto source_or = FileVideoSource::open("/tmp/nonexistent_video.mp4");
  EXPECT_FALSE(source_or.has_value());
}

TEST_F(FileVideoSourceTest, SetTimestampAndTakeFrame) {
  auto source_or = FileVideoSource::open(kTestVideo);
  ASSERT_TRUE(source_or.has_value());
  auto& source = *source_or;

  // Seek to 1 second
  source->setTimestamp(1'000'000'000);

  // Poll until we get a frame (decode is async)
  std::optional<DecodedFrame> frame;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!frame.has_value() && std::chrono::steady_clock::now() < deadline) {
    frame = source->takeFrame();
    if (!frame.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  ASSERT_TRUE(frame.has_value()) << "no frame received within 5 seconds";
  EXPECT_FALSE(frame->isNull());
  EXPECT_EQ(frame->width, 640);
  EXPECT_EQ(frame->height, 480);
  EXPECT_EQ(frame->format, PixelFormat::kYUV420P);
  EXPECT_TRUE(frame->isValid());
}

TEST_F(FileVideoSourceTest, PauseResume) {
  auto source_or = FileVideoSource::open(kTestVideo);
  ASSERT_TRUE(source_or.has_value());
  auto& source = *source_or;

  // FfmpegBackend opens in paused state
  EXPECT_TRUE(source->isPaused());
  source->setPaused(false);
  EXPECT_FALSE(source->isPaused());
  source->setPaused(true);
  EXPECT_TRUE(source->isPaused());
}

}  // namespace
}  // namespace PJ
