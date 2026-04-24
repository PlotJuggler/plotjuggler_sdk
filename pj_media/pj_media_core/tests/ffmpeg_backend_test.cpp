/// FfmpegBackend integration tests — playback, forward scrub, backward scrub.
///
/// These tests drive the real decode pipeline with test_480p.mp4.
/// Invariants ported from video_player_lab test suite:
/// - Play: positions monotonically increase at ~real-time rate
/// - Forward scrub: at least N deliveries, no large backward jumps
/// - Backward scrub: no forward jumps, final position near target
/// - After scrub stops: position settles to the requested target

#include "pj_media_core/ffmpeg_backend.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace PJ {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// ---------------------------------------------------------------------------
// Video path constants
// ---------------------------------------------------------------------------

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";
const std::string kTestVideo1080p = "pj_media/testdata/test_1080p.mp4";
const std::string kTestVideoBframes = "pj_media/testdata/test_1080p_bframes.mp4";
const std::string kTestVideo1920 = std::string(getenv("HOME") ? getenv("HOME") : "") + "/ws_plotjuggler/video_1920.mp4";
const std::string kTestVideo4k = std::string(getenv("HOME") ? getenv("HOME") : "") + "/ws_plotjuggler/video_4k.mp4";

// ---------------------------------------------------------------------------
// TestBackend — standalone backend helper for non-fixture tests
// ---------------------------------------------------------------------------

struct TestBackend {
  FfmpegBackend backend;
  std::vector<double> positions;
  bool loaded = false;

  TestBackend() {
    backend.setPositionCallback([this](double s) { positions.push_back(s); });
    backend.setFrameCallback([](const DecodedFrame&) {});
    backend.setFileLoadedCallback([this]() { loaded = true; });
    backend.setDurationCallback([](double) {});
  }

  bool openAndWait(const std::string& path, int timeout_ms = 5000) {
    if (!backend.open(path)) {
      return false;
    }
    auto deadline = Clock::now() + milliseconds(timeout_ms);
    while (!loaded && Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    return loaded;
  }

  bool seekAndWait(double seconds, int timeout_ms = 3000) {
    backend.seek(seconds);
    auto deadline = Clock::now() + milliseconds(timeout_ms);
    size_t before = positions.size();
    while (positions.size() == before && Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    return positions.size() > before;
  }

  void scrubRange(double from, double to, double step, int interval_ms) {
    if (from <= to) {
      for (double t = from; t <= to; t += step) {
        backend.seek(t);
        auto poll_end = Clock::now() + milliseconds(interval_ms);
        while (Clock::now() < poll_end) {
          backend.processEvents();
          std::this_thread::sleep_for(milliseconds(2));
        }
      }
    } else {
      for (double t = from; t >= to; t -= step) {
        backend.seek(t);
        auto poll_end = Clock::now() + milliseconds(interval_ms);
        while (Clock::now() < poll_end) {
          backend.processEvents();
          std::this_thread::sleep_for(milliseconds(2));
        }
      }
    }
  }

  void waitForSettle(int timeout_ms = 2000, int quiet_ms = 500) {
    auto deadline = Clock::now() + milliseconds(timeout_ms);
    size_t prev_count = positions.size();
    auto last_change = Clock::now();
    while (Clock::now() < deadline) {
      backend.processEvents();
      if (positions.size() > prev_count) {
        prev_count = positions.size();
        last_change = Clock::now();
      }
      if (Clock::now() - last_change > milliseconds(quiet_ms) && !positions.empty()) {
        break;
      }
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  void pollFor(int duration_ms) {
    auto deadline = Clock::now() + milliseconds(duration_ms);
    while (Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }
};

// ---------------------------------------------------------------------------
// Fixture for 480p tests
// ---------------------------------------------------------------------------

class FfmpegBackendTest : public ::testing::Test {
 protected:
  FfmpegBackend backend;
  std::vector<double> positions;
  int frame_count = 0;
  double last_duration = 0;
  bool file_loaded = false;

  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
    backend.setPositionCallback([this](double s) { positions.push_back(s); });
    backend.setFrameCallback([this](const DecodedFrame& f) {
      if (!f.isNull()) {
        frame_count++;
      }
    });
    backend.setDurationCallback([this](double d) { last_duration = d; });
    backend.setFileLoadedCallback([this]() { file_loaded = true; });
  }

  size_t pollUntilFrame(int timeout_ms = 3000) {
    size_t before = positions.size();
    auto deadline = Clock::now() + milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
      backend.processEvents();
      if (positions.size() > before) {
        return positions.size() - before;
      }
      std::this_thread::sleep_for(milliseconds(5));
    }
    return 0;
  }

  void pollFor(int duration_ms) {
    auto deadline = Clock::now() + milliseconds(duration_ms);
    while (Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  bool openAndWait() {
    if (!backend.open(kTestVideo)) {
      return false;
    }
    auto deadline = Clock::now() + milliseconds(2000);
    while (!file_loaded && Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    return file_loaded;
  }
};

// ===========================================================================
// Fixture-based tests (480p)
// ===========================================================================

TEST_F(FfmpegBackendTest, OpenAndFileLoaded) {
  ASSERT_TRUE(openAndWait());
  EXPECT_TRUE(file_loaded);
  EXPECT_GT(last_duration, 0.0);
  EXPECT_GT(backend.duration(), 0.0);
}

TEST_F(FfmpegBackendTest, PlayForward) {
  ASSERT_TRUE(openAndWait());

  backend.setPaused(false);
  pollFor(1500);
  backend.setPaused(true);

  ASSERT_GE(positions.size(), 5u) << "Expected at least 5 position updates during 1.5s playback";

  for (size_t i = 1; i < positions.size(); ++i) {
    EXPECT_GE(positions[i], positions[i - 1])
        << "Position went backward at index " << i << ": " << positions[i - 1] << " -> " << positions[i];
  }

  double elapsed = positions.back() - positions.front();
  EXPECT_GT(elapsed, 0.3) << "Position advanced too slowly: " << elapsed << "s in 1.5s of playback";
  EXPECT_LT(elapsed, 4.5) << "Position advanced too fast: " << elapsed << "s in 1.5s of playback";
}

TEST_F(FfmpegBackendTest, SeekWhilePaused) {
  ASSERT_TRUE(openAndWait());

  backend.seek(2.0);
  size_t delivered = pollUntilFrame(3000);

  ASSERT_GE(delivered, 1u) << "No frame delivered after seek while paused";
  EXPECT_NEAR(positions.back(), 2.0, 0.5) << "Position after seek not near 2.0s";
}

TEST_F(FfmpegBackendTest, ForwardScrub) {
  ASSERT_TRUE(openAndWait());

  backend.seek(0.5);
  pollUntilFrame(3000);
  positions.clear();

  for (double t = 0.5; t <= 3.5; t += 0.1) {
    backend.seek(t);
    pollFor(10);
  }

  pollFor(500);

  std::cout << "  ForwardScrub: " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  ASSERT_GE(positions.size(), 2u) << "Forward scrub delivered fewer than 2 frames";

  for (size_t i = 1; i < positions.size(); ++i) {
    double jump = positions[i - 1] - positions[i];
    EXPECT_LT(jump, 1.0) << "Large backward jump at index " << i << ": " << positions[i - 1] << " -> " << positions[i];
  }

  EXPECT_NEAR(positions.back(), 3.5, 0.5) << "Final position after forward scrub not near 3.5s";
}

TEST_F(FfmpegBackendTest, BackwardScrub) {
  ASSERT_TRUE(openAndWait());

  backend.seek(4.0);
  pollUntilFrame(3000);
  positions.clear();

  for (double t = 3.9; t >= 1.0; t -= 0.1) {
    backend.seek(t);
    pollFor(10);
  }

  pollFor(2000);

  std::cout << "  BackwardScrub: " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  ASSERT_GE(positions.size(), 1u) << "Backward scrub delivered zero frames";

  for (size_t i = 1; i < positions.size(); ++i) {
    double forward_jump = positions[i] - positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during backward scrub at index " << i << ": " << positions[i - 1]
                                 << " -> " << positions[i];
  }

  EXPECT_NEAR(positions.back(), 1.0, 1.0) << "Final position after backward scrub not near 1.0s";
}

TEST_F(FfmpegBackendTest, ScrubSettles) {
  ASSERT_TRUE(openAndWait());

  backend.seek(3.0);
  pollUntilFrame(3000);

  positions.clear();
  backend.seek(1.0);
  pollUntilFrame(3000);

  ASSERT_FALSE(positions.empty()) << "No position delivered after single seek";
  EXPECT_NEAR(positions.back(), 1.0, 0.5) << "Position did not settle near 1.0s";
}

TEST_F(FfmpegBackendTest, CloseWhileDecoding) {
  ASSERT_TRUE(openAndWait());

  backend.seek(2.5);
  backend.close();

  SUCCEED();
}

TEST_F(FfmpegBackendTest, PauseUnpauseNoBurst) {
  ASSERT_TRUE(openAndWait());

  backend.setPaused(false);
  pollFor(1000);
  backend.setPaused(true);

  ASSERT_FALSE(positions.empty());
  double pos_at_pause = positions.back();

  pollFor(500);

  positions.clear();
  backend.setPaused(false);
  pollFor(500);
  backend.setPaused(true);

  std::cout << "  PauseUnpauseNoBurst: pause_pos=" << pos_at_pause;
  if (!positions.empty()) {
    std::cout << " resume_first=" << positions.front() << " resume_last=" << positions.back();
  }
  std::cout << std::endl;

  ASSERT_FALSE(positions.empty()) << "No frames after unpause";

  double advance = positions.back() - pos_at_pause;
  EXPECT_LT(advance, 1.5) << "Frame burst after unpause: advanced " << advance << "s in 500ms";
  EXPECT_GT(advance, 0.0) << "No progress after unpause";
}

TEST_F(FfmpegBackendTest, RapidBidirectional) {
  ASSERT_TRUE(openAndWait());

  backend.seek(1.0);
  pollUntilFrame(3000);

  for (double t = 1.5; t <= 3.5; t += 0.5) {
    backend.seek(t);
    pollFor(10);
  }
  pollFor(50);

  positions.clear();

  for (double t = 3.0; t >= 1.0; t -= 0.5) {
    backend.seek(t);
    pollFor(10);
  }

  pollFor(1000);

  std::cout << "  RapidBidirectional (backward phase): " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  ASSERT_GE(positions.size(), 1u) << "No frames delivered in backward phase of bidirectional scrub";

  for (size_t i = 1; i < positions.size(); ++i) {
    double forward_jump = positions[i] - positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump in backward phase at index " << i << ": " << positions[i - 1]
                                 << " -> " << positions[i];
  }
}

TEST_F(FfmpegBackendTest, SeekToEnd) {
  ASSERT_TRUE(openAndWait());

  ASSERT_GT(last_duration, 0.0);

  double target = last_duration - 0.1;
  backend.seek(target);
  size_t delivered = pollUntilFrame(5000);

  std::cout << "  SeekToEnd: duration=" << last_duration << " target=" << target << " deliveries=" << delivered
            << " all_positions:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  ASSERT_GE(delivered, 1u) << "No frame delivered when seeking to end of file";
  EXPECT_NEAR(positions.back(), target, 1.0) << "Position not near end of file";
}

TEST_F(FfmpegBackendTest, ForwardScrubSettle) {
  ASSERT_TRUE(openAndWait());

  backend.seek(0.5);
  pollUntilFrame(3000);
  positions.clear();

  for (double t = 1.0; t <= 4.0; t += 0.5) {
    backend.seek(t);
    pollFor(35);
  }

  ASSERT_FALSE(positions.empty());
  double last_scrub_pos = positions.back();

  positions.clear();
  pollFor(1000);

  std::cout << "  ForwardScrubSettle: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  for (size_t i = 0; i < positions.size(); ++i) {
    double backward_jump = last_scrub_pos - positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Position jumped backward after forward scrub settle at index " << i << ": "
                                  << last_scrub_pos << " -> " << positions[i];
  }
}

// ===========================================================================
// Standalone-backend tests (1080p, 1920p, 4K)
// ===========================================================================

TEST_F(FfmpegBackendTest, BackwardScrub1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1080p, 3000));

  ASSERT_TRUE(tb.seekAndWait(8.0, 5000));
  tb.positions.clear();

  tb.scrubRange(7.5, 4.0, 0.1, 10);

  size_t prev_count = tb.positions.size();
  auto deadline = Clock::now() + milliseconds(3000);
  while (Clock::now() < deadline) {
    tb.backend.processEvents();
    if (tb.positions.size() > prev_count) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrub1080p: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 1u) << "1080p backward scrub delivered zero frames";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double forward_jump = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during 1080p backward scrub at index " << i << ": "
                                 << tb.positions[i - 1] << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 4.0, 1.5) << "Final position after 1080p backward scrub not near 4.0s";
}

TEST_F(FfmpegBackendTest, BackwardScrub4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found at " << kTestVideo4k;
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo4k, 10000));

  ASSERT_TRUE(tb.seekAndWait(30.0, 10000));
  tb.positions.clear();

  tb.scrubRange(29.0, 20.0, 0.5, 16);
  tb.waitForSettle(5000, 500);

  std::cout << "  BackwardScrub4K: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 1u) << "4K backward scrub delivered zero frames";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double forward_jump = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(forward_jump, 1.0) << "Forward jump during 4K backward scrub at index " << i << ": "
                                 << tb.positions[i - 1] << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 20.0, 3.0) << "Final position after 4K backward scrub not near 20.0s";
}

TEST_F(FfmpegBackendTest, ForwardScrub4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found at " << kTestVideo4k;
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo4k, 10000));

  ASSERT_TRUE(tb.seekAndWait(10.0, 10000));
  tb.positions.clear();

  tb.scrubRange(11.0, 25.0, 0.5, 16);
  tb.waitForSettle(5000, 500);

  std::cout << "  ForwardScrub4K: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 1u) << "4K forward scrub delivered zero frames";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double backward_jump = tb.positions[i - 1] - tb.positions[i];
    EXPECT_LT(backward_jump, 2.0) << "Large backward jump during 4K forward scrub at index " << i << ": "
                                  << tb.positions[i - 1] << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 25.0, 3.0) << "Final position after 4K forward scrub not near 25.0s";
}

TEST_F(FfmpegBackendTest, PlayForwardBframes) {
  if (!std::filesystem::exists(kTestVideoBframes)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideoBframes, 3000));

  tb.backend.setPaused(false);
  tb.pollFor(2000);
  tb.backend.setPaused(true);

  std::cout << "  PlayForwardBframes: " << tb.positions.size() << " deliveries, first=" << tb.positions.front()
            << " last=" << tb.positions.back() << std::endl;

  ASSERT_GE(tb.positions.size(), 10u) << "Too few frames during B-frame playback";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    EXPECT_GE(tb.positions[i], tb.positions[i - 1])
        << "B-frame playback not monotonic at index " << i << ": " << tb.positions[i - 1] << " -> " << tb.positions[i];
  }
}

TEST_F(FfmpegBackendTest, BackwardScrubDelivers1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1080p, 3000));

  ASSERT_TRUE(tb.seekAndWait(8.0, 5000));
  tb.positions.clear();

  tb.scrubRange(7.5, 5.0, 0.1, 10);
  tb.waitForSettle(2000, 500);

  std::cout << "  BackwardScrubDelivers1080p: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 3u) << "Backward scrub delivered too few frames — user sees frozen display";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double forward_jump = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during backward scrub at index " << i << ": " << tb.positions[i - 1]
                                 << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 5.0, 1.5);
}

TEST_F(FfmpegBackendTest, ForwardScrubNoLargeGaps1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1080p, 3000));

  ASSERT_TRUE(tb.seekAndWait(1.0, 3000));
  tb.positions.clear();

  tb.scrubRange(1.5, 6.0, 0.5, 30);
  tb.waitForSettle(1000, 300);

  std::cout << "  ForwardScrubNoLargeGaps1080p: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 3u) << "Forward scrub delivered too few frames";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double gap = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(gap, 1.5) << "Large gap in forward scrub at index " << i << ": " << tb.positions[i - 1] << " -> "
                        << tb.positions[i];
  }

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    EXPECT_GE(tb.positions[i], tb.positions[i - 1]) << "Backward jump in forward scrub at index " << i;
  }
}

TEST_F(FfmpegBackendTest, ForwardScrubBframes1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1920, 5000));

  ASSERT_TRUE(tb.seekAndWait(5.0, 5000));
  tb.positions.clear();

  tb.scrubRange(6.0, 15.0, 0.5, 30);
  tb.waitForSettle(1000, 300);

  std::cout << "  ForwardScrubBframes1920: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 100) / 100;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 3u);

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    EXPECT_GE(tb.positions[i], tb.positions[i - 1]) << "Backward jump in B-frame forward scrub at index " << i << ": "
                                                    << tb.positions[i - 1] << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 15.0, 2.0);
}

TEST_F(FfmpegBackendTest, BackwardScrubBframes1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1920, 5000));

  ASSERT_TRUE(tb.seekAndWait(15.0, 5000));
  tb.positions.clear();

  tb.scrubRange(14.5, 5.0, 0.2, 35);
  tb.waitForSettle(2000, 500);

  std::cout << "  BackwardScrubBframes1920: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 100) / 100;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 3u) << "B-frame backward scrub delivered too few frames";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double forward_jump = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(forward_jump, 0.2) << "Forward jump in B-frame backward scrub at index " << i << ": "
                                 << tb.positions[i - 1] << " -> " << tb.positions[i];
  }

  EXPECT_NEAR(tb.positions.back(), 5.0, 2.0);
}

TEST_F(FfmpegBackendTest, BackwardScrubDenseDelivery4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo4k, 10000));

  ASSERT_TRUE(tb.seekAndWait(45.0, 10000));
  tb.positions.clear();

  tb.scrubRange(44.0, 30.0, 1.0, 40);
  tb.waitForSettle(5000, 500);

  std::cout << "  BackwardScrubDenseDelivery4K: " << tb.positions.size() << " deliveries:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(tb.positions.size(), 3u) << "4K backward scrub too sparse — user sees frozen display";

  for (size_t i = 1; i < tb.positions.size(); ++i) {
    double forward_jump = tb.positions[i] - tb.positions[i - 1];
    EXPECT_LT(forward_jump, 1.0) << "Forward jump in 4K backward scrub at index " << i;
  }

  EXPECT_NEAR(tb.positions.back(), 30.0, 3.0);
}

TEST_F(FfmpegBackendTest, BackwardScrubResponsiveness1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1920, 5000));

  ASSERT_TRUE(tb.seekAndWait(20.0, 5000));
  std::this_thread::sleep_for(milliseconds(2000));

  std::vector<std::pair<double, double>> seek_vs_display;
  double latest_display = tb.positions.back();

  tb.backend.setPositionCallback([&](double s) {
    tb.positions.push_back(s);
    latest_display = s;
  });

  for (double t = 19.5; t >= 10.0; t -= 0.5) {
    tb.backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(35);
    while (Clock::now() < poll_end) {
      tb.backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    seek_vs_display.emplace_back(t, latest_display);
  }

  tb.pollFor(2000);

  std::cout << "  BackwardScrubResponsiveness1920:" << std::endl;
  double max_lag = 0;
  for (const auto& [target, display] : seek_vs_display) {
    double lag = display - target;
    if (lag > max_lag) {
      max_lag = lag;
    }
    std::cout << "    target=" << std::round(target * 10) / 10 << " display=" << std::round(display * 100) / 100
              << " lag=" << std::round(lag * 100) / 100 << std::endl;
  }

  EXPECT_LT(max_lag, 2.0) << "Backward scrub display lags too far behind slider (max lag " << max_lag << "s)";
}

TEST_F(FfmpegBackendTest, ForwardScrubSettle1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo1920, 5000));

  ASSERT_TRUE(tb.seekAndWait(0.5, 3000));
  tb.positions.clear();

  tb.scrubRange(1.0, 4.0, 0.5, 35);

  ASSERT_FALSE(tb.positions.empty());
  double last_scrub_pos = tb.positions.back();

  tb.positions.clear();
  tb.pollFor(1000);

  std::cout << "  ForwardScrubSettle1920: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : tb.positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  for (size_t i = 0; i < tb.positions.size(); ++i) {
    double backward_jump = last_scrub_pos - tb.positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Rewind after forward scrub at index " << i << ": was at " << last_scrub_pos
                                  << ", jumped to " << tb.positions[i];
  }
}

TEST_F(FfmpegBackendTest, ForwardScrubSettle4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found";
  }

  TestBackend tb;
  ASSERT_TRUE(tb.openAndWait(kTestVideo4k, 10000));

  ASSERT_TRUE(tb.seekAndWait(0.5, 5000));
  tb.positions.clear();

  tb.scrubRange(0.5, 4.0, 0.07, 40);

  ASSERT_FALSE(tb.positions.empty());
  double last_scrub_pos = tb.positions.back();

  tb.positions.clear();
  tb.pollFor(2000);

  std::cout << "  ForwardScrubSettle4K: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : tb.positions) {
    std::cout << " " << std::round(p * 1000) / 1000;
  }
  std::cout << std::endl;

  for (size_t i = 0; i < tb.positions.size(); ++i) {
    double backward_jump = last_scrub_pos - tb.positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Rewind after 4K forward scrub at index " << i << ": was at " << last_scrub_pos
                                  << ", jumped to " << tb.positions[i];
  }
}

}  // namespace
}  // namespace PJ
