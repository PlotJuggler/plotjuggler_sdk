#include "pj_media_core/thumbnail_cache.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace PJ {
namespace {

const std::string kTest480p = "pj_media/testdata/test_480p.mp4";
const std::string kTest1080p = "pj_media/testdata/test_1080p.mp4";

class ThumbnailCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTest480p)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
  }

  static void waitForBuild(const ThumbnailCache& cache, int max_ms = 10000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
    while (cache.isBuilding() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

TEST_F(ThumbnailCacheTest, BuildAndLookup) {
  ThumbnailCache cache;
  cache.buildAsync(kTest480p, 5.0);
  waitForBuild(cache);

  EXPECT_FALSE(cache.isBuilding());
  EXPECT_GT(cache.size(), 0u);
  EXPECT_GT(cache.memoryUsed(), 0u);

  auto frame = cache.lookup(0.5);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->width, 640);
  EXPECT_EQ(frame->height, 480);
  EXPECT_EQ(frame->format, PixelFormat::kYUV420P);
  EXPECT_TRUE(frame->isValid());
}

// H1: reopen must clear old frames, not append
TEST_F(ThumbnailCacheTest, ReopenClearsPreviousFrames) {
  if (!std::filesystem::exists(kTest1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  ThumbnailCache cache;

  // Build from 480p
  cache.buildAsync(kTest480p, 5.0);
  waitForBuild(cache);
  size_t count_480p = cache.size();
  ASSERT_GT(count_480p, 0u);

  auto frame_480 = cache.lookup(0.5);
  ASSERT_TRUE(frame_480.has_value());
  EXPECT_EQ(frame_480->width, 640);

  // Reopen with 1080p — must clear the 480p frames
  cache.buildAsync(kTest1080p, 5.0);
  waitForBuild(cache);

  // Frame count must reflect ONLY the new video, not old + new appended.
  // Before the fix, this was 10 (5 stale 480p + 5 new 1080p).
  size_t count_1080p = cache.size();
  EXPECT_GT(count_1080p, 0u);
  EXPECT_LE(count_1080p, 6u) << "stale frames from previous build were not cleared (got " << count_1080p
                             << ", expected ~5)";
}

// C4: building from a non-video or corrupt file must not crash
TEST_F(ThumbnailCacheTest, BuildFromInvalidFileNoCrash) {
  ThumbnailCache cache;

  // Use a text file (definitely not a video)
  cache.buildAsync("pj_media/PLAN.md", 5.0);
  waitForBuild(cache);

  EXPECT_FALSE(cache.isBuilding());
  EXPECT_EQ(cache.size(), 0u);
}

TEST_F(ThumbnailCacheTest, BuildFromNonexistentFileNoCrash) {
  ThumbnailCache cache;
  cache.buildAsync("/tmp/nonexistent_video.mp4", 5.0);
  waitForBuild(cache);

  EXPECT_FALSE(cache.isBuilding());
  EXPECT_EQ(cache.size(), 0u);
}

TEST_F(ThumbnailCacheTest, LookupBeforeAnyTimestampReturnsNullopt) {
  ThumbnailCache cache;
  cache.buildAsync(kTest480p, 5.0);
  waitForBuild(cache);

  // Lookup before any cached frame (negative time)
  auto frame = cache.lookup(-1.0);
  EXPECT_FALSE(frame.has_value());
}

TEST_F(ThumbnailCacheTest, ConcurrentLookupsSafe) {
  ThumbnailCache cache;
  cache.buildAsync(kTest480p, 5.0);
  waitForBuild(cache);
  ASSERT_GT(cache.size(), 0u);

  // Two threads doing lookups concurrently — must not crash or corrupt
  std::atomic<int> success_count{0};
  auto lookup_fn = [&](double start) {
    for (int i = 0; i < 500; ++i) {
      double t = start + static_cast<double>(i) * 0.01;
      auto frame = cache.lookup(t);
      if (frame.has_value() && frame->isValid()) {
        ++success_count;
      }
    }
  };

  std::thread t1(lookup_fn, 0.0);
  std::thread t2(lookup_fn, 0.5);
  t1.join();
  t2.join();

  EXPECT_GT(success_count.load(), 0);
}

}  // namespace
}  // namespace PJ
