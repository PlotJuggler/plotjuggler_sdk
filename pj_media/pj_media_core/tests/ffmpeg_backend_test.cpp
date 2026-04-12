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

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

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

  /// Poll processEvents() until at least one new position arrives.
  /// Returns the number of new positions delivered, or 0 on timeout.
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

  /// Poll processEvents() for a fixed duration, collecting all deliveries.
  void pollFor(int duration_ms) {
    auto deadline = Clock::now() + milliseconds(duration_ms);
    while (Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  /// Open the test video and wait for file-loaded event.
  bool openAndWait() {
    if (!backend.open(kTestVideo)) {
      return false;
    }
    // Poll until file_loaded fires via processEvents
    auto deadline = Clock::now() + milliseconds(2000);
    while (!file_loaded && Clock::now() < deadline) {
      backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    return file_loaded;
  }
};

// -----------------------------------------------------------------------
// Test 1: Open file, verify metadata delivered
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, OpenAndFileLoaded) {
  ASSERT_TRUE(openAndWait());
  EXPECT_TRUE(file_loaded);
  EXPECT_GT(last_duration, 0.0);
  EXPECT_GT(backend.duration(), 0.0);
}

// -----------------------------------------------------------------------
// Test 2: Play forward — positions increase monotonically
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, PlayForward) {
  ASSERT_TRUE(openAndWait());

  backend.setPaused(false);
  pollFor(1500);  // play for 1.5 seconds
  backend.setPaused(true);

  // Must have delivered frames
  ASSERT_GE(positions.size(), 5u) << "Expected at least 5 position updates during 1.5s playback";

  // Positions must be monotonically non-decreasing
  for (size_t i = 1; i < positions.size(); ++i) {
    EXPECT_GE(positions[i], positions[i - 1])
        << "Position went backward at index " << i << ": " << positions[i - 1] << " -> " << positions[i];
  }

  // Position should have advanced at roughly real-time (within 3x tolerance)
  double elapsed = positions.back() - positions.front();
  EXPECT_GT(elapsed, 0.3) << "Position advanced too slowly: " << elapsed << "s in 1.5s of playback";
  EXPECT_LT(elapsed, 4.5) << "Position advanced too fast: " << elapsed << "s in 1.5s of playback";
}

// -----------------------------------------------------------------------
// Test 3: Seek while paused — single frame delivered
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, SeekWhilePaused) {
  ASSERT_TRUE(openAndWait());

  backend.seek(2.0);
  size_t delivered = pollUntilFrame(3000);

  ASSERT_GE(delivered, 1u) << "No frame delivered after seek while paused";
  EXPECT_NEAR(positions.back(), 2.0, 0.5) << "Position after seek not near 2.0s";
}

// -----------------------------------------------------------------------
// Test 4: Forward scrub — smooth feedback, final position correct
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrub) {
  ASSERT_TRUE(openAndWait());

  // Seek to start position and wait for it
  backend.seek(0.5);
  pollUntilFrame(3000);
  positions.clear();

  // Rapid forward scrub: 0.5 → 0.6 → ... → 3.5 (31 seeks, 10ms apart)
  // Aggressive rate simulates fast slider drag at ~100Hz
  for (double t = 0.5; t <= 3.5; t += 0.1) {
    backend.seek(t);
    pollFor(10);
  }

  // Wait for final delivery to settle
  pollFor(500);

  // Diagnostics
  std::cout << "  ForwardScrub: " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  // Must have delivered at least 2 frames (partials + final)
  ASSERT_GE(positions.size(), 2u) << "Forward scrub delivered fewer than 2 frames";

  // No large backward jumps (> 1.0s) in delivered positions
  for (size_t i = 1; i < positions.size(); ++i) {
    double jump = positions[i - 1] - positions[i];
    EXPECT_LT(jump, 1.0) << "Large backward jump at index " << i << ": " << positions[i - 1] << " -> " << positions[i];
  }

  // Final position should be near 3.5s
  EXPECT_NEAR(positions.back(), 3.5, 0.5) << "Final position after forward scrub not near 3.5s";
}

// -----------------------------------------------------------------------
// Test 5: Backward scrub — no forward jumps, final position correct
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, BackwardScrub) {
  ASSERT_TRUE(openAndWait());

  // Seek to 4.0s and wait for delivery
  backend.seek(4.0);
  pollUntilFrame(3000);
  positions.clear();

  // Rapid backward scrub: 3.9 → 3.8 → ... → 1.0 (30 seeks, 10ms apart)
  // Aggressive rate simulates fast slider drag at ~100Hz
  for (double t = 3.9; t >= 1.0; t -= 0.1) {
    backend.seek(t);
    pollFor(10);
  }

  // Wait for final delivery to settle
  pollFor(2000);

  // Diagnostics
  std::cout << "  BackwardScrub: " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  // Must have delivered at least 1 frame (the final completion)
  ASSERT_GE(positions.size(), 1u) << "Backward scrub delivered zero frames";

  // No forward jumps: each delivered position ≤ previous + small tolerance
  // (tolerance accounts for B-frame PTS jitter within a GOP)
  for (size_t i = 1; i < positions.size(); ++i) {
    double forward_jump = positions[i] - positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during backward scrub at index " << i << ": " << positions[i - 1]
                                 << " -> " << positions[i];
  }

  // Final position should be near 1.0s
  EXPECT_NEAR(positions.back(), 1.0, 1.0) << "Final position after backward scrub not near 1.0s";
}

// -----------------------------------------------------------------------
// Test 6: Scrub settles — after motion stops, correct position displayed
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ScrubSettles) {
  ASSERT_TRUE(openAndWait());

  // Seek to 3.0s, wait for it
  backend.seek(3.0);
  pollUntilFrame(3000);

  // Now seek to 1.0s and wait — no further seeks
  positions.clear();
  backend.seek(1.0);
  pollUntilFrame(3000);

  ASSERT_FALSE(positions.empty()) << "No position delivered after single seek";
  EXPECT_NEAR(positions.back(), 1.0, 0.5) << "Position did not settle near 1.0s";
}

// -----------------------------------------------------------------------
// Test 7: Close while decoding — no crash, no hang
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, CloseWhileDecoding) {
  ASSERT_TRUE(openAndWait());

  // Seek to middle of file, then immediately close
  backend.seek(2.5);
  // Don't wait — close right away
  backend.close();

  // If we get here without crash or hang, the test passes
  SUCCEED();
}

// -----------------------------------------------------------------------
// Test 8: 1080p backward scrub stress — slower decode, more cancellation
// -----------------------------------------------------------------------

const std::string kTestVideo1080p = "pj_media/testdata/test_1080p.mp4";

TEST_F(FfmpegBackendTest, BackwardScrub1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  // Use 1080p video — must open separately (fixture opened 480p)
  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo1080p));
  auto deadline = Clock::now() + milliseconds(3000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 8.0s, wait for delivery
  hd_backend.seek(8.0);
  deadline = Clock::now() + milliseconds(5000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_FALSE(hd_positions.empty()) << "No frame delivered at initial seek";
  hd_positions.clear();

  // Aggressive backward scrub: 7.5 → 7.4 → ... → 4.0 (36 seeks, 10ms apart)
  for (double t = 7.5; t >= 4.0; t -= 0.1) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(10);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(2));
    }
  }

  // Wait for final delivery
  deadline = Clock::now() + milliseconds(3000);
  size_t prev_count = hd_positions.size();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      break;  // Got a new delivery
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrub1080p: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  // Must have delivered at least 1 frame
  ASSERT_GE(hd_positions.size(), 1u) << "1080p backward scrub delivered zero frames";

  // No forward jumps > 0.5s
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double forward_jump = hd_positions[i] - hd_positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during 1080p backward scrub at index " << i << ": "
                                 << hd_positions[i - 1] << " -> " << hd_positions[i];
  }

  // Final position should be near 4.0s
  EXPECT_NEAR(hd_positions.back(), 4.0, 1.5) << "Final position after 1080p backward scrub not near 4.0s";
}

// -----------------------------------------------------------------------
// Test 9: 4K backward scrub stress — real-world worst case
// -----------------------------------------------------------------------

const std::string kTestVideo4k = std::string(getenv("HOME") ? getenv("HOME") : "") + "/ws_plotjuggler/video_4k.mp4";

TEST_F(FfmpegBackendTest, BackwardScrub4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found at " << kTestVideo4k;
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo4k));
  auto deadline = Clock::now() + milliseconds(10000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 30s, wait for delivery
  hd_backend.seek(30.0);
  deadline = Clock::now() + milliseconds(10000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_FALSE(hd_positions.empty()) << "No frame delivered at initial 4K seek";
  hd_positions.clear();

  // Aggressive backward scrub: 29 → 28.5 → ... → 20 (19 seeks, 16ms apart)
  // This simulates slider drag at ~60Hz, matching the demo's throttle
  for (double t = 29.0; t >= 20.0; t -= 0.5) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(16);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(2));
    }
  }

  // Wait for final delivery to settle
  deadline = Clock::now() + milliseconds(5000);
  size_t prev_count = hd_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      prev_count = hd_positions.size();
      last_change = Clock::now();
    }
    // Settle: no new delivery for 500ms
    if (Clock::now() - last_change > milliseconds(500) && !hd_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrub4K: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  // Must have delivered at least 1 frame
  ASSERT_GE(hd_positions.size(), 1u) << "4K backward scrub delivered zero frames";

  // No forward jumps > 1.0s
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double forward_jump = hd_positions[i] - hd_positions[i - 1];
    EXPECT_LT(forward_jump, 1.0) << "Forward jump during 4K backward scrub at index " << i << ": "
                                 << hd_positions[i - 1] << " -> " << hd_positions[i];
  }

  // Final position should be near 20.0s
  EXPECT_NEAR(hd_positions.back(), 20.0, 3.0) << "Final position after 4K backward scrub not near 20.0s";
}

// -----------------------------------------------------------------------
// Test 10: 4K forward scrub stress
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrub4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found at " << kTestVideo4k;
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo4k));
  auto deadline = Clock::now() + milliseconds(10000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 10s start position
  hd_backend.seek(10.0);
  deadline = Clock::now() + milliseconds(10000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_FALSE(hd_positions.empty());
  hd_positions.clear();

  // Aggressive forward scrub: 11 → 11.5 → ... → 25 (29 seeks, 16ms apart)
  for (double t = 11.0; t <= 25.0; t += 0.5) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(16);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(2));
    }
  }

  // Wait for settle
  deadline = Clock::now() + milliseconds(5000);
  size_t prev_count = hd_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      prev_count = hd_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(500) && !hd_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  ForwardScrub4K: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  // Must have delivered at least 1 frame
  ASSERT_GE(hd_positions.size(), 1u) << "4K forward scrub delivered zero frames";

  // No large backward jumps (> 2.0s)
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double backward_jump = hd_positions[i - 1] - hd_positions[i];
    EXPECT_LT(backward_jump, 2.0) << "Large backward jump during 4K forward scrub at index " << i << ": "
                                  << hd_positions[i - 1] << " -> " << hd_positions[i];
  }

  // Final position should be near 25.0s
  EXPECT_NEAR(hd_positions.back(), 25.0, 3.0) << "Final position after 4K forward scrub not near 25.0s";
}

// -----------------------------------------------------------------------
// Test 11: Play forward with B-frames — PTS must be monotonic
// -----------------------------------------------------------------------

const std::string kTestVideoBframes = "pj_media/testdata/test_1080p_bframes.mp4";

TEST_F(FfmpegBackendTest, PlayForwardBframes) {
  if (!std::filesystem::exists(kTestVideoBframes)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }

  FfmpegBackend bf_backend;
  std::vector<double> bf_positions;
  bf_backend.setPositionCallback([&](double s) { bf_positions.push_back(s); });
  bf_backend.setFrameCallback([](const DecodedFrame&) {});
  bool bf_loaded = false;
  bf_backend.setFileLoadedCallback([&]() { bf_loaded = true; });
  bf_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(bf_backend.open(kTestVideoBframes));
  auto deadline = Clock::now() + milliseconds(3000);
  while (!bf_loaded && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(bf_loaded);

  bf_backend.setPaused(false);
  deadline = Clock::now() + milliseconds(2000);
  while (Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  bf_backend.setPaused(true);

  std::cout << "  PlayForwardBframes: " << bf_positions.size() << " deliveries, first=" << bf_positions.front()
            << " last=" << bf_positions.back() << std::endl;

  ASSERT_GE(bf_positions.size(), 10u) << "Too few frames during B-frame playback";

  // Strict monotonicity — catches pkt->pts vs frame->pts bug
  for (size_t i = 1; i < bf_positions.size(); ++i) {
    EXPECT_GE(bf_positions[i], bf_positions[i - 1])
        << "B-frame playback not monotonic at index " << i << ": " << bf_positions[i - 1] << " -> " << bf_positions[i];
  }
}

// -----------------------------------------------------------------------
// Test 12: Backward scrub delivers frames (1080p) — not just completion
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, BackwardScrubDelivers1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo1080p));
  auto deadline = Clock::now() + milliseconds(3000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 8.0s, wait for delivery
  hd_backend.seek(8.0);
  deadline = Clock::now() + milliseconds(5000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_FALSE(hd_positions.empty());
  hd_positions.clear();

  // Aggressive backward scrub: 7.5 → 7.4 → ... → 5.0 (26 seeks, 10ms apart)
  for (double t = 7.5; t >= 5.0; t -= 0.1) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(10);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(2));
    }
  }

  // Wait for settle
  deadline = Clock::now() + milliseconds(2000);
  size_t prev_count = hd_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      prev_count = hd_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(500) && !hd_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrubDelivers1080p: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  // Must deliver at least 3 frames DURING scrub, not just final completion
  ASSERT_GE(hd_positions.size(), 3u) << "Backward scrub delivered too few frames — user sees frozen display";

  // No forward jumps > 0.5s
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double forward_jump = hd_positions[i] - hd_positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump during backward scrub at index " << i << ": " << hd_positions[i - 1]
                                 << " -> " << hd_positions[i];
  }

  EXPECT_NEAR(hd_positions.back(), 5.0, 1.5);
}

// -----------------------------------------------------------------------
// Test 12: Forward scrub no large gaps (1080p) — catches missing partials
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrubNoLargeGaps1080p) {
  if (!std::filesystem::exists(kTestVideo1080p)) {
    GTEST_SKIP() << "test_1080p.mp4 not found";
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo1080p));
  auto deadline = Clock::now() + milliseconds(3000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 1.0s start, wait
  hd_backend.seek(1.0);
  deadline = Clock::now() + milliseconds(3000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  hd_positions.clear();

  // Forward scrub: 1.5 → 2.0 → ... → 6.0 (10 seeks, 30ms apart)
  for (double t = 1.5; t <= 6.0; t += 0.5) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(30);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  // Wait for settle
  deadline = Clock::now() + milliseconds(1000);
  size_t prev_count = hd_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      prev_count = hd_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(300) && !hd_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  ForwardScrubNoLargeGaps1080p: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  ASSERT_GE(hd_positions.size(), 3u) << "Forward scrub delivered too few frames";

  // No gap > 1.5s between consecutive deliveries
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double gap = hd_positions[i] - hd_positions[i - 1];
    EXPECT_LT(gap, 1.5) << "Large gap in forward scrub at index " << i << ": " << hd_positions[i - 1] << " -> "
                        << hd_positions[i];
  }

  // Strictly monotonic
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    EXPECT_GE(hd_positions[i], hd_positions[i - 1]) << "Backward jump in forward scrub at index " << i;
  }
}

// -----------------------------------------------------------------------
// Test 13: Pause/unpause — no frame burst after resume
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, PauseUnpauseNoBurst) {
  ASSERT_TRUE(openAndWait());

  // Play for 1s
  backend.setPaused(false);
  pollFor(1000);
  backend.setPaused(true);

  ASSERT_FALSE(positions.empty());
  double pos_at_pause = positions.back();

  // Wait 500ms while paused (decoder idle, frame timer goes stale)
  pollFor(500);

  // Resume and play for 500ms
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

  // The first 500ms after unpause should not advance more than 1.5s
  // (would indicate a frame burst from stale frame timer)
  double advance = positions.back() - pos_at_pause;
  EXPECT_LT(advance, 1.5) << "Frame burst after unpause: advanced " << advance << "s in 500ms";
  EXPECT_GT(advance, 0.0) << "No progress after unpause";
}

// -----------------------------------------------------------------------
// Test 14: Rapid bidirectional scrub — forward then immediately backward
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, RapidBidirectional) {
  ASSERT_TRUE(openAndWait());

  // Seek to 1.0s start
  backend.seek(1.0);
  pollUntilFrame(3000);

  // Forward scrub: 1.5 → 2.0 → 2.5 → 3.0 → 3.5
  for (double t = 1.5; t <= 3.5; t += 0.5) {
    backend.seek(t);
    pollFor(10);
  }
  pollFor(50);

  // Now immediately backward — clear positions to track backward phase
  positions.clear();

  // Backward scrub: 3.0 → 2.5 → 2.0 → 1.5 → 1.0
  for (double t = 3.0; t >= 1.0; t -= 0.5) {
    backend.seek(t);
    pollFor(10);
  }

  // Wait for settle
  pollFor(1000);

  std::cout << "  RapidBidirectional (backward phase): " << positions.size() << " deliveries:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  // Must deliver at least 1 frame in backward phase
  ASSERT_GE(positions.size(), 1u) << "No frames delivered in backward phase of bidirectional scrub";

  // No forward jumps > 0.5s in backward phase
  for (size_t i = 1; i < positions.size(); ++i) {
    double forward_jump = positions[i] - positions[i - 1];
    EXPECT_LT(forward_jump, 0.5) << "Forward jump in backward phase at index " << i << ": " << positions[i - 1]
                                 << " -> " << positions[i];
  }
}

// -----------------------------------------------------------------------
// Test 16: Forward scrub monotonic with B-frame video (1920p external)
// -----------------------------------------------------------------------

const std::string kTestVideo1920 = std::string(getenv("HOME") ? getenv("HOME") : "") + "/ws_plotjuggler/video_1920.mp4";

TEST_F(FfmpegBackendTest, ForwardScrubBframes1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  FfmpegBackend bf_backend;
  std::vector<double> bf_positions;
  bf_backend.setPositionCallback([&](double s) { bf_positions.push_back(s); });
  bf_backend.setFrameCallback([](const DecodedFrame&) {});
  bool bf_loaded = false;
  bf_backend.setFileLoadedCallback([&]() { bf_loaded = true; });
  bf_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(bf_backend.open(kTestVideo1920));
  auto deadline = Clock::now() + milliseconds(5000);
  while (!bf_loaded && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(bf_loaded);

  // Seek to 5.0, wait
  bf_backend.seek(5.0);
  deadline = Clock::now() + milliseconds(5000);
  while (bf_positions.empty() && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  bf_positions.clear();

  // Forward scrub: 6 → 7 → ... → 15 (19 seeks, 30ms apart)
  for (double t = 6.0; t <= 15.0; t += 0.5) {
    bf_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(30);
    while (Clock::now() < poll_end) {
      bf_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  // Settle
  deadline = Clock::now() + milliseconds(1000);
  size_t prev_count = bf_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    bf_backend.processEvents();
    if (bf_positions.size() > prev_count) {
      prev_count = bf_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(300) && !bf_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  ForwardScrubBframes1920: " << bf_positions.size() << " deliveries:";
  for (double p : bf_positions) {
    std::cout << " " << std::round(p * 100) / 100;
  }
  std::cout << std::endl;

  ASSERT_GE(bf_positions.size(), 3u);

  // Strictly monotonic — catches B-frame PTS ordering bug
  for (size_t i = 1; i < bf_positions.size(); ++i) {
    EXPECT_GE(bf_positions[i], bf_positions[i - 1]) << "Backward jump in B-frame forward scrub at index " << i << ": "
                                                    << bf_positions[i - 1] << " -> " << bf_positions[i];
  }

  EXPECT_NEAR(bf_positions.back(), 15.0, 2.0);
}

// -----------------------------------------------------------------------
// Test 17: Backward scrub with B-frame video (1920p external)
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, BackwardScrubBframes1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  FfmpegBackend bf_backend;
  std::vector<double> bf_positions;
  bf_backend.setPositionCallback([&](double s) { bf_positions.push_back(s); });
  bf_backend.setFrameCallback([](const DecodedFrame&) {});
  bool bf_loaded = false;
  bf_backend.setFileLoadedCallback([&]() { bf_loaded = true; });
  bf_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(bf_backend.open(kTestVideo1920));
  auto deadline = Clock::now() + milliseconds(5000);
  while (!bf_loaded && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(bf_loaded);

  // Seek to 15.0s, wait
  bf_backend.seek(15.0);
  deadline = Clock::now() + milliseconds(5000);
  while (bf_positions.empty() && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  bf_positions.clear();

  // Aggressive backward scrub matching real GUI behavior:
  // ~35ms between seeks, ~0.2s steps (simulates moderate drag speed)
  for (double t = 14.5; t >= 5.0; t -= 0.2) {
    bf_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(35);
    while (Clock::now() < poll_end) {
      bf_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  // Settle
  deadline = Clock::now() + milliseconds(2000);
  size_t prev_count = bf_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    bf_backend.processEvents();
    if (bf_positions.size() > prev_count) {
      prev_count = bf_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(500) && !bf_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrubBframes1920: " << bf_positions.size() << " deliveries:";
  for (double p : bf_positions) {
    std::cout << " " << std::round(p * 100) / 100;
  }
  std::cout << std::endl;

  // Must deliver at least 3 frames during backward scrub
  ASSERT_GE(bf_positions.size(), 3u) << "B-frame backward scrub delivered too few frames";

  // No forward jumps > 0.2s — strict monotonicity for smooth visual
  for (size_t i = 1; i < bf_positions.size(); ++i) {
    double forward_jump = bf_positions[i] - bf_positions[i - 1];
    EXPECT_LT(forward_jump, 0.2) << "Forward jump in B-frame backward scrub at index " << i << ": "
                                 << bf_positions[i - 1] << " -> " << bf_positions[i];
  }

  EXPECT_NEAR(bf_positions.back(), 5.0, 2.0);
}

// -----------------------------------------------------------------------
// Test 18: Seek to end — last frame must be displayed
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, SeekToEnd) {
  ASSERT_TRUE(openAndWait());

  ASSERT_GT(last_duration, 0.0);

  // Seek to near the end
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

// -----------------------------------------------------------------------
// Test 19: Backward scrub dense delivery (4K — real worst case)
// Simulates actual GUI behavior: ~40ms between seeks, large range.
// 4K GOPs are ~72 frames. Decode takes ~80ms per GOP. Seeks arrive
// faster than decode completes → decoder must get minimum time.
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, BackwardScrubDenseDelivery4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found";
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo4k));
  auto deadline = Clock::now() + milliseconds(10000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Seek to 45.0s, wait
  hd_backend.seek(45.0);
  deadline = Clock::now() + milliseconds(10000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_FALSE(hd_positions.empty()) << "No frame at initial seek";
  hd_positions.clear();

  // Aggressive backward scrub: 44 → 43 → ... → 30 (15 seeks, 40ms apart)
  // Matches actual GUI slider drag rate from user testing
  for (double t = 44.0; t >= 30.0; t -= 1.0) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(40);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  // Wait for settle
  deadline = Clock::now() + milliseconds(5000);
  size_t prev_count = hd_positions.size();
  auto last_change = Clock::now();
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    if (hd_positions.size() > prev_count) {
      prev_count = hd_positions.size();
      last_change = Clock::now();
    }
    if (Clock::now() - last_change > milliseconds(500) && !hd_positions.empty()) {
      break;
    }
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrubDenseDelivery4K: " << hd_positions.size() << " deliveries:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 10) / 10;
  }
  std::cout << std::endl;

  // Must deliver at least 3 frames for minimally acceptable backward scrub
  ASSERT_GE(hd_positions.size(), 3u) << "4K backward scrub too sparse — user sees frozen display";

  // No forward jumps > 1.0s
  for (size_t i = 1; i < hd_positions.size(); ++i) {
    double forward_jump = hd_positions[i] - hd_positions[i - 1];
    EXPECT_LT(forward_jump, 1.0) << "Forward jump in 4K backward scrub at index " << i;
  }

  EXPECT_NEAR(hd_positions.back(), 30.0, 3.0);
}

// -----------------------------------------------------------------------
// Test 20: Backward scrub responsiveness — display must track slider
// The display position must stay within 2.0s of the seek target.
// Catches the "completions-only" regression where the display lags
// several seconds behind the slider during backward drag.
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, BackwardScrubResponsiveness1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  FfmpegBackend bf_backend;
  std::vector<std::pair<double, double>> seek_vs_display;  // {seek_target, displayed_position}
  double latest_display = -1;
  bf_backend.setPositionCallback([&](double s) { latest_display = s; });
  bf_backend.setFrameCallback([](const DecodedFrame&) {});
  bool bf_loaded = false;
  bf_backend.setFileLoadedCallback([&]() { bf_loaded = true; });
  bf_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(bf_backend.open(kTestVideo1920));
  auto deadline = Clock::now() + milliseconds(5000);
  while (!bf_loaded && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(bf_loaded);

  // Seek to 20.0s, wait for display to catch up.
  // Also wait for thumbnail cache to build (~1.3s for 30s 1920p video).
  bf_backend.seek(20.0);
  deadline = Clock::now() + milliseconds(5000);
  while (latest_display < 0 && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  // Give the cache thread time to finish building
  std::this_thread::sleep_for(milliseconds(2000));

  // Backward scrub: 19.5 → 19.0 → ... → 10.0 (20 seeks, 35ms apart)
  // Record {seek_target, displayed_position} after each seek
  for (double t = 19.5; t >= 10.0; t -= 0.5) {
    bf_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(35);
    while (Clock::now() < poll_end) {
      bf_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
    seek_vs_display.emplace_back(t, latest_display);
  }

  // Wait for settle
  deadline = Clock::now() + milliseconds(2000);
  while (Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  BackwardScrubResponsiveness1920:" << std::endl;
  double max_lag = 0;
  for (const auto& [target, display] : seek_vs_display) {
    double lag = display - target;  // positive = display is ahead of target
    if (lag > max_lag) {
      max_lag = lag;
    }
    std::cout << "    target=" << std::round(target * 10) / 10 << " display=" << std::round(display * 100) / 100
              << " lag=" << std::round(lag * 100) / 100 << std::endl;
  }

  // The display must not lag more than 2.0s behind the slider target.
  // "Lag" here means display shows a position AHEAD of where the slider
  // is (because the slider moved backward but display hasn't caught up).
  EXPECT_LT(max_lag, 2.0) << "Backward scrub display lags too far behind slider (max lag " << max_lag << "s)";
}

// -----------------------------------------------------------------------
// Test 21: Forward scrub settle — position must not jump backward on stop
// After forward scrub, when seeks stop, the position must not rewind.
// Catches the bug where releasing the slider causes a backward jump
// followed by forward replay.
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrubSettle) {
  ASSERT_TRUE(openAndWait());

  // Forward scrub: 0.5 → 1.0 → ... → 4.0 (8 seeks, 35ms apart)
  backend.seek(0.5);
  pollUntilFrame(3000);
  positions.clear();

  for (double t = 1.0; t <= 4.0; t += 0.5) {
    backend.seek(t);
    pollFor(35);
  }

  // Record the last position during scrub
  ASSERT_FALSE(positions.empty());
  double last_scrub_pos = positions.back();

  // Now STOP — no more seeks. Wait and collect positions for 1 second.
  positions.clear();
  pollFor(1000);

  std::cout << "  ForwardScrubSettle: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  // No position should jump backward from last_scrub_pos by more than 0.5s
  for (size_t i = 0; i < positions.size(); ++i) {
    double backward_jump = last_scrub_pos - positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Position jumped backward after forward scrub settle at index " << i << ": "
                                  << last_scrub_pos << " -> " << positions[i];
  }
}

// -----------------------------------------------------------------------
// Test 22: Forward scrub settle with B-frames (1920p) — must not rewind
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrubSettle1920) {
  if (!std::filesystem::exists(kTestVideo1920)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }

  FfmpegBackend bf_backend;
  std::vector<double> bf_positions;
  bf_backend.setPositionCallback([&](double s) { bf_positions.push_back(s); });
  bf_backend.setFrameCallback([](const DecodedFrame&) {});
  bool bf_loaded = false;
  bf_backend.setFileLoadedCallback([&]() { bf_loaded = true; });
  bf_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(bf_backend.open(kTestVideo1920));
  auto deadline = Clock::now() + milliseconds(5000);
  while (!bf_loaded && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(bf_loaded);

  // Wait for thumbnail cache
  std::this_thread::sleep_for(milliseconds(2000));

  // Forward scrub: 1.0 → 1.5 → ... → 4.0 (7 seeks, 35ms apart)
  bf_backend.seek(0.5);
  deadline = Clock::now() + milliseconds(3000);
  while (bf_positions.empty() && Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  bf_positions.clear();

  for (double t = 1.0; t <= 4.0; t += 0.5) {
    bf_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(35);
    while (Clock::now() < poll_end) {
      bf_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  ASSERT_FALSE(bf_positions.empty());
  double last_scrub_pos = bf_positions.back();

  // Stop seeking — wait 1 second, collect settle positions
  bf_positions.clear();
  deadline = Clock::now() + milliseconds(1000);
  while (Clock::now() < deadline) {
    bf_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  ForwardScrubSettle1920: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : bf_positions) {
    std::cout << " " << p;
  }
  std::cout << std::endl;

  // No position should jump backward from last_scrub_pos by more than 0.5s
  for (size_t i = 0; i < bf_positions.size(); ++i) {
    double backward_jump = last_scrub_pos - bf_positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Rewind after forward scrub at index " << i << ": was at " << last_scrub_pos
                                  << ", jumped to " << bf_positions[i];
  }
}

// -----------------------------------------------------------------------
// Test 23: Forward scrub settle with 4K — must not rewind
// -----------------------------------------------------------------------

TEST_F(FfmpegBackendTest, ForwardScrubSettle4K) {
  if (!std::filesystem::exists(kTestVideo4k)) {
    GTEST_SKIP() << "video_4k.mp4 not found";
  }

  FfmpegBackend hd_backend;
  std::vector<double> hd_positions;
  hd_backend.setPositionCallback([&](double s) { hd_positions.push_back(s); });
  hd_backend.setFrameCallback([](const DecodedFrame&) {});
  bool hd_loaded = false;
  hd_backend.setFileLoadedCallback([&]() { hd_loaded = true; });
  hd_backend.setDurationCallback([](double) {});

  ASSERT_TRUE(hd_backend.open(kTestVideo4k));
  auto deadline = Clock::now() + milliseconds(10000);
  while (!hd_loaded && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  ASSERT_TRUE(hd_loaded);

  // Wait for some cache frames
  std::this_thread::sleep_for(milliseconds(3000));

  // Forward scrub from 0.5 to 4.0 (matching user's pattern)
  hd_backend.seek(0.5);
  deadline = Clock::now() + milliseconds(5000);
  while (hd_positions.empty() && Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }
  hd_positions.clear();

  // Slow forward scrub: 0.5 → 1.0 → ... → 4.0 (every 100ms like the user)
  for (double t = 0.5; t <= 4.0; t += 0.07) {
    hd_backend.seek(t);
    auto poll_end = Clock::now() + milliseconds(40);
    while (Clock::now() < poll_end) {
      hd_backend.processEvents();
      std::this_thread::sleep_for(milliseconds(5));
    }
  }

  ASSERT_FALSE(hd_positions.empty());
  double last_scrub_pos = hd_positions.back();

  // Stop — collect settle positions for 2 seconds
  hd_positions.clear();
  deadline = Clock::now() + milliseconds(2000);
  while (Clock::now() < deadline) {
    hd_backend.processEvents();
    std::this_thread::sleep_for(milliseconds(5));
  }

  std::cout << "  ForwardScrubSettle4K: last_scrub=" << last_scrub_pos << " settle:";
  for (double p : hd_positions) {
    std::cout << " " << std::round(p * 1000) / 1000;
  }
  std::cout << std::endl;

  for (size_t i = 0; i < hd_positions.size(); ++i) {
    double backward_jump = last_scrub_pos - hd_positions[i];
    EXPECT_LT(backward_jump, 0.5) << "Rewind after 4K forward scrub at index " << i << ": was at " << last_scrub_pos
                                  << ", jumped to " << hd_positions[i];
  }
}

}  // namespace
}  // namespace PJ
