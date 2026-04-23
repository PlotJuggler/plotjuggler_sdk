#include "pj_media_core/streaming_video_decoder.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "test_mp4_demux.h"

namespace PJ {
namespace {

using test::AnnexBPacket;

const std::string kTestVideo = "pj_media/testdata/test_480p.mp4";

ObjectTopicId pushPackets(ObjectStore& store, const std::vector<AnnexBPacket>& packets, size_t count) {
  auto topic_result = store.registerTopic({0, "video/test", R"({"media_class":"video","encoding":"h264"})"});
  EXPECT_TRUE(topic_result.has_value());
  auto topic = topic_result.value();

  for (size_t i = 0; i < count && i < packets.size(); ++i) {
    auto result = store.pushOwned(topic, packets[i].timestamp, packets[i].data);
    EXPECT_TRUE(result.has_value()) << "push failed at i=" << i;
  }
  return topic;
}

class StreamingVideoDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kTestVideo)) {
      GTEST_SKIP() << "test_480p.mp4 not found";
    }
    all_packets_ = test::extractAnnexBPackets(kTestVideo);
    ASSERT_GT(all_packets_.size(), 30u) << "need at least 30 packets";

    // Count keyframes for reference
    for (const auto& pkt : all_packets_) {
      if (pkt.keyframe) {
        ++expected_keyframe_count_;
      }
    }
    ASSERT_GT(expected_keyframe_count_, 0);
  }

  std::vector<AnnexBPacket> all_packets_;
  int expected_keyframe_count_ = 0;
};

TEST_F(StreamingVideoDecoderTest, BasicDecode) {
  ObjectStore store;
  // Push enough packets for at least one GOP
  size_t first_gop_end = 0;
  int kf_count = 0;
  for (size_t i = 0; i < all_packets_.size(); ++i) {
    if (all_packets_[i].keyframe) {
      ++kf_count;
      if (kf_count > 1) {
        first_gop_end = i;
        break;
      }
    }
  }
  if (first_gop_end == 0) {
    first_gop_end = all_packets_.size();
  }

  auto topic = pushPackets(store, all_packets_, first_gop_end);

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Decode the last frame in the GOP
  Timestamp last_ts = all_packets_[first_gop_end - 1].timestamp;
  auto result = decoder.decodeAt(last_ts);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_FALSE(result->isNull());
  EXPECT_EQ(result->width, 640);
  EXPECT_EQ(result->height, 480);
  EXPECT_EQ(result->format, PixelFormat::kYUV420P);
}

TEST_F(StreamingVideoDecoderTest, SequentialLive) {
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/live", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  int decoded_count = 0;
  size_t limit = std::min(all_packets_.size(), size_t{60});

  for (size_t i = 0; i < limit; ++i) {
    auto push_result = store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);
    ASSERT_TRUE(push_result.has_value());

    auto range = store.timeRange(topic);
    auto result = decoder.decodeAt(range.second);
    if (result.has_value() && !result->isNull()) {
      ++decoded_count;
    }
  }

  // First frame(s) before keyframe may fail
  EXPECT_GT(decoded_count, 0) << "should decode at least some frames";
}

TEST_F(StreamingVideoDecoderTest, JoinMidStream) {
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/join", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Push only P-frames (skip initial keyframe)
  size_t first_p = 0;
  for (size_t i = 0; i < all_packets_.size(); ++i) {
    if (!all_packets_[i].keyframe) {
      first_p = i;
      break;
    }
  }

  // Push a few P-frames
  for (size_t i = first_p; i < first_p + 5 && i < all_packets_.size(); ++i) {
    store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);
  }

  EXPECT_FALSE(decoder.isInitialized());
  auto range = store.timeRange(topic);
  auto result = decoder.decodeAt(range.second);
  EXPECT_FALSE(result.has_value()) << "should fail without keyframe";

  // Now push a keyframe (find the next one after our P-frames)
  for (size_t i = first_p + 5; i < all_packets_.size(); ++i) {
    store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);
    if (all_packets_[i].keyframe) {
      // Push a few more after the keyframe
      for (size_t j = i + 1; j < i + 5 && j < all_packets_.size(); ++j) {
        store.pushOwned(topic, all_packets_[j].timestamp, all_packets_[j].data);
      }
      break;
    }
  }

  range = store.timeRange(topic);
  result = decoder.decodeAt(range.second);
  EXPECT_TRUE(result.has_value()) << "should decode after keyframe arrives";
  EXPECT_TRUE(decoder.isInitialized());
}

TEST_F(StreamingVideoDecoderTest, ScrubToMiddle) {
  ObjectStore store;
  auto topic = pushPackets(store, all_packets_, all_packets_.size());

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Find a frame in the middle of the first GOP (not a keyframe)
  size_t mid = 0;
  for (size_t i = 1; i < all_packets_.size(); ++i) {
    if (!all_packets_[i].keyframe) {
      mid = i;
      if (i > 10) {
        break;  // Get a frame well into the GOP
      }
    }
  }

  Timestamp mid_ts = all_packets_[mid].timestamp;
  auto result = decoder.decodeAt(mid_ts);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_FALSE(result->isNull());
  EXPECT_EQ(result->width, 640);
}

TEST_F(StreamingVideoDecoderTest, ScrubBackward) {
  ObjectStore store;
  auto topic = pushPackets(store, all_packets_, all_packets_.size());

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Decode a later frame
  size_t later = std::min(size_t{50}, all_packets_.size() - 1);
  auto result1 = decoder.decodeAt(all_packets_[later].timestamp);
  ASSERT_TRUE(result1.has_value()) << result1.error();

  // Now scrub backward to an earlier frame
  size_t earlier = 5;
  auto result2 = decoder.decodeAt(all_packets_[earlier].timestamp);
  ASSERT_TRUE(result2.has_value()) << result2.error();
  EXPECT_FALSE(result2->isNull());
}

TEST_F(StreamingVideoDecoderTest, ScrubForwardWithinGop) {
  ObjectStore store;
  auto topic = pushPackets(store, all_packets_, all_packets_.size());

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Decode frame 5 (within first GOP)
  auto result1 = decoder.decodeAt(all_packets_[5].timestamp);
  ASSERT_TRUE(result1.has_value()) << result1.error();

  // Decode frame 8 (still within same GOP — should not flush)
  auto result2 = decoder.decodeAt(all_packets_[8].timestamp);
  ASSERT_TRUE(result2.has_value()) << result2.error();
  EXPECT_FALSE(result2->isNull());
}

TEST_F(StreamingVideoDecoderTest, RetentionEviction) {
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/retention", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  // Set a tight retention window
  store.setRetentionBudget(topic, {500'000'000, 0});  // 500ms

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Push all packets — retention will evict old ones
  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
  }

  // The earliest timestamps should have been evicted
  auto range = store.timeRange(topic);
  Timestamp first_pkt_ts = all_packets_.front().timestamp;

  if (range.first > first_pkt_ts) {
    // Early frames were evicted — trying to decode them should fail
    // or at least the keyframe index should not contain stale entries
    auto result = decoder.decodeAt(first_pkt_ts);
    EXPECT_FALSE(result.has_value()) << "evicted timestamp should be undecodable";
  }

  // But latest frames should still decode
  auto result = decoder.decodeAt(range.second);
  EXPECT_TRUE(result.has_value()) << result.error();
}

TEST_F(StreamingVideoDecoderTest, KeyframeCount) {
  ObjectStore store;
  auto topic = pushPackets(store, all_packets_, all_packets_.size());

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Trigger a decode to force keyframe index scan
  auto range = store.timeRange(topic);
  decoder.decodeAt(range.second);

  // Verify by decoding at each keyframe timestamp
  int successful_kf_decodes = 0;
  for (const auto& pkt : all_packets_) {
    if (pkt.keyframe) {
      auto result = decoder.decodeAt(pkt.timestamp);
      if (result.has_value() && !result->isNull()) {
        ++successful_kf_decodes;
      }
    }
  }
  // The last keyframe may not produce a frame (no subsequent packets
  // to fill the decoder's reorder buffer), so allow one miss.
  EXPECT_GE(successful_kf_decodes, expected_keyframe_count_ - 1);
}

TEST_F(StreamingVideoDecoderTest, LiveStreamWithRetention) {
  // Simulates the demo: push frames one-by-one with retention,
  // decode the latest on each tick. The decoder must continue
  // working after the initial keyframe is evicted.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/live_retention", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  // Retention window smaller than the full video — forces eviction
  // Use 2 seconds (60 frames at 30fps)
  store.setRetentionBudget(topic, {2'000'000'000, 0});

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  int decoded_count = 0;
  int failed_after_eviction = 0;
  bool eviction_started = false;

  for (size_t i = 0; i < all_packets_.size(); ++i) {
    auto push_result = store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);
    ASSERT_TRUE(push_result.has_value()) << "push failed at i=" << i;

    auto range = store.timeRange(topic);
    if (range.first > all_packets_.front().timestamp) {
      eviction_started = true;
    }

    auto result = decoder.decodeAt(range.second);
    if (result.has_value() && !result->isNull()) {
      ++decoded_count;
    } else if (eviction_started) {
      ++failed_after_eviction;
    }
  }

  EXPECT_TRUE(eviction_started) << "retention should have evicted some entries";
  // The key assertion: decoding must continue working after eviction.
  // Without the fix, failed_after_eviction would be high (all frames after ~150).
  EXPECT_LE(failed_after_eviction, 5) << "should not fail after keyframe eviction in live mode";
  EXPECT_GT(decoded_count, static_cast<int>(all_packets_.size()) / 2)
      << "should decode most frames even with retention";
}

TEST_F(StreamingVideoDecoderTest, LiveDecodeIsRealTime) {
  // Verify that decoding one frame in live mode (forward, sequential)
  // takes less than one frame interval. At 30fps, frame interval = 33ms.
  // A single decodeAt in forward mode should decode just 1 frame, not
  // seek back to a keyframe.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/realtime", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Push all packets first (like a pre-loaded buffer)
  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
  }

  // Warm up: decode the first few frames (VAAPI pipeline needs a few
  // packets before producing output — not a B-frame issue, just HW latency)
  for (size_t i = 0; i < 10 && i < all_packets_.size(); ++i) {
    decoder.decodeAt(all_packets_[i].timestamp);
  }

  // Measure: decode 20 sequential frames and check each takes < 33ms
  using Clock = std::chrono::steady_clock;
  int slow_frames = 0;
  constexpr size_t kTestFrames = 20;
  constexpr auto kMaxFrameTime = std::chrono::milliseconds(33);

  for (size_t i = 10; i < 10 + kTestFrames && i < all_packets_.size(); ++i) {
    auto start = Clock::now();
    auto result = decoder.decodeAt(all_packets_[i].timestamp);
    auto elapsed = Clock::now() - start;

    if (!result.has_value() || result->isNull()) {
      continue;  // EAGAIN from decoder pipeline — normal for first few
    }
    if (elapsed > kMaxFrameTime) {
      ++slow_frames;
    }
  }

  // Allow at most 2 slow frames (VAAPI init jitter, first few frames)
  EXPECT_LE(slow_frames, 2) << "too many frames exceeded 33ms — "
                            << "forward decode may be seeking unnecessarily";
}

TEST_F(StreamingVideoDecoderTest, LiveDecodeWithRetentionIsRealTime) {
  // Same as above but with retention eviction active, simulating
  // the actual streaming use case. Each call should decode exactly
  // one new frame, not re-decode from a keyframe.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/rt_retention", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();
  store.setRetentionBudget(topic, {2'000'000'000, 0});  // 2s

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  using Clock = std::chrono::steady_clock;
  int slow_frames = 0;
  constexpr auto kMaxFrameTime = std::chrono::milliseconds(33);

  for (size_t i = 0; i < all_packets_.size(); ++i) {
    store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);

    auto range = store.timeRange(topic);
    auto start = Clock::now();
    auto result = decoder.decodeAt(range.second);
    auto elapsed = Clock::now() - start;

    if (result.has_value() && !result->isNull()) {
      // Only count after the decoder is warmed up (past first keyframe)
      if (i > 5 && elapsed > kMaxFrameTime) {
        ++slow_frames;
      }
    }
  }

  // In live streaming, nearly all frames should be fast (single decode)
  EXPECT_LE(slow_frames, 5) << "too many slow frames during live streaming with retention";
}

TEST_F(StreamingVideoDecoderTest, ScrubAfterLiveStream) {
  // Simulates: user watches live stream, then pauses and scrubs
  // the retained buffer. Per REQUIREMENTS.md §4.3, live and scrub
  // are mutually exclusive — push stops during scrub.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/scrub_after", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();
  store.setRetentionBudget(topic, {3'000'000'000, 0});  // 3s

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Phase 1: live streaming — push all frames
  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
    auto range = store.timeRange(topic);
    decoder.decodeAt(range.second);
  }

  // Phase 2: pause — no more pushes, buffer is frozen
  size_t count = store.entryCount(topic);
  ASSERT_GT(count, 10u) << "need entries in buffer to scrub";

  // Scrub forward through entire retained buffer
  int forward_decoded = 0;
  for (size_t i = 0; i < count; ++i) {
    auto entry = store.at(topic, i);
    ASSERT_TRUE(entry.has_value());
    auto result = decoder.decodeAt(entry->timestamp);
    if (result.has_value() && !result->isNull()) {
      ++forward_decoded;
    }
  }
  EXPECT_GT(forward_decoded, static_cast<int>(count) / 2) << "should decode most frames during forward scrub";

  // Scrub backward through entire retained buffer
  int backward_decoded = 0;
  for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
    auto entry = store.at(topic, static_cast<size_t>(i));
    ASSERT_TRUE(entry.has_value());
    auto result = decoder.decodeAt(entry->timestamp);
    if (result.has_value() && !result->isNull()) {
      ++backward_decoded;
    }
  }
  EXPECT_GT(backward_decoded, static_cast<int>(count) / 2) << "should decode most frames during backward scrub";

  // Scrub to specific position (middle of buffer)
  auto mid_entry = store.at(topic, count / 2);
  ASSERT_TRUE(mid_entry.has_value());
  auto mid_result = decoder.decodeAt(mid_entry->timestamp);
  ASSERT_TRUE(mid_result.has_value()) << mid_result.error();
  EXPECT_FALSE(mid_result->isNull());
  EXPECT_EQ(mid_result->width, 640);
  EXPECT_EQ(mid_result->height, 480);
}

TEST_F(StreamingVideoDecoderTest, ScrubAfterLiveStreamToFirstEntry) {
  // Edge case: scrub to the very first entry in the retained buffer.
  // This requires the keyframe at or before that entry to still exist.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/scrub_first", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();
  store.setRetentionBudget(topic, {3'000'000'000, 0});

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Live phase
  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
    auto range = store.timeRange(topic);
    decoder.decodeAt(range.second);
  }

  // Scrub to first retained entry
  auto first_entry = store.at(topic, 0);
  ASSERT_TRUE(first_entry.has_value());

  auto result = decoder.decodeAt(first_entry->timestamp);
  // This may fail if the keyframe for this entry was evicted — that's OK
  // (it's a known limitation). But it must not crash.
  if (result.has_value()) {
    EXPECT_FALSE(result->isNull());
  }
}

TEST_F(StreamingVideoDecoderTest, ScrubAfterLiveStreamToLastEntry) {
  // Edge case: after pause, scrub to the very last entry.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/scrub_last", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();
  store.setRetentionBudget(topic, {3'000'000'000, 0});

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
    auto range = store.timeRange(topic);
    decoder.decodeAt(range.second);
  }

  // Scrub to last entry — should work since we just decoded it live
  auto range = store.timeRange(topic);
  auto result = decoder.decodeAt(range.second);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_FALSE(result->isNull());
  EXPECT_EQ(result->width, 640);
}

TEST_F(StreamingVideoDecoderTest, DecodeAtSameTimestampTwice) {
  // When display polls faster than push rate (60Hz display vs 30Hz push),
  // decodeAt is called with the same timestamp twice. This must not
  // trigger a full seek — it should return quickly.
  ObjectStore store;
  auto topic = pushPackets(store, all_packets_, all_packets_.size());

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Decode frame 10
  auto result1 = decoder.decodeAt(all_packets_[10].timestamp);
  ASSERT_TRUE(result1.has_value()) << result1.error();

  // Decode same frame again — must succeed and be fast
  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto result2 = decoder.decodeAt(all_packets_[10].timestamp);
  auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

  ASSERT_TRUE(result2.has_value()) << result2.error();
  EXPECT_FALSE(result2->isNull());
  // Same-frame decode should be nearly instant (< 5ms), not a full seek
  EXPECT_LT(elapsed, 10.0) << "same-timestamp decode took " << elapsed << "ms — likely re-seeking";
}

TEST_F(StreamingVideoDecoderTest, ScrubAfterSteadyStateStreaming) {
  // Codex P1: after the retention window fills, entryCount() stays constant.
  // updateKeyframeIndex() must still discover new keyframes, or scrub
  // into the retained buffer fails with "no keyframe yet".
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/steady_state", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  // Small retention: 1 second (~30 frames at 30fps)
  store.setRetentionBudget(topic, {1'000'000'000, 0});

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  // Phase 1: live stream all packets (retention will reach steady state)
  for (const auto& pkt : all_packets_) {
    store.pushOwned(topic, pkt.timestamp, pkt.data);
    auto range = store.timeRange(topic);
    decoder.decodeAt(range.second);
  }

  // Phase 2: pause and scrub. The retained buffer should contain ~30 frames.
  // Scrubbing to any entry must work — keyframe index must be up to date.
  size_t count = store.entryCount(topic);
  ASSERT_GT(count, 5u);

  // Scrub to middle of retained buffer
  auto mid_entry = store.at(topic, count / 2);
  ASSERT_TRUE(mid_entry.has_value());
  auto result = decoder.decodeAt(mid_entry->timestamp);
  EXPECT_TRUE(result.has_value()) << "scrub after steady-state should work: " << result.error();

  // Scrub to first entry
  auto first_entry = store.at(topic, 0);
  ASSERT_TRUE(first_entry.has_value());
  auto first_result = decoder.decodeAt(first_entry->timestamp);
  // May fail if keyframe was evicted, but must not fail with "no keyframe yet"
  if (!first_result.has_value()) {
    EXPECT_NE(first_result.error(), "no keyframe yet")
        << "keyframe index was not updated during steady-state streaming";
  }
}

TEST_F(StreamingVideoDecoderTest, BenchmarkLiveWith60HzDisplay) {
  // Simulates real demo: push at 30Hz, display polls at 60Hz.
  // Every other display call hits the same timestamp (no new frame).
  // This must NOT cause a re-seek — total time should be close to
  // the push-only benchmark.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/bench60", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  using Clock = std::chrono::steady_clock;
  int slow_calls = 0;
  int total_calls = 0;
  constexpr auto kMaxCallTime = std::chrono::milliseconds(33);
  size_t pkt_idx = 0;
  bool push_tick = true;  // Alternates: push+decode, then decode-only

  while (pkt_idx < all_packets_.size()) {
    if (push_tick && pkt_idx < all_packets_.size()) {
      store.pushOwned(topic, all_packets_[pkt_idx].timestamp, all_packets_[pkt_idx].data);
      ++pkt_idx;
    }
    push_tick = !push_tick;

    auto range = store.timeRange(topic);
    auto start = Clock::now();
    decoder.decodeAt(range.second);
    auto elapsed = Clock::now() - start;

    ++total_calls;
    if (elapsed > kMaxCallTime) {
      ++slow_calls;
    }
  }

  fprintf(stderr, "[Benchmark 60Hz] %d calls, %d slow (>33ms)\n", total_calls, slow_calls);
  // Very few should be slow — same-timestamp calls should be fast
  EXPECT_LE(slow_calls, 3) << "too many slow calls — same-timestamp may be re-seeking";
}

TEST_F(StreamingVideoDecoderTest, BenchmarkLiveDecodePerFrame) {
  // Benchmark: push frames one-by-one (simulating live), decode each.
  // Measures per-frame decode time to identify real-time bottlenecks.
  // At 30fps, each frame must take < 33ms for real-time playback.
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/bench", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  using Clock = std::chrono::steady_clock;
  std::vector<double> frame_times_ms;
  frame_times_ms.reserve(all_packets_.size());

  for (size_t i = 0; i < all_packets_.size(); ++i) {
    store.pushOwned(topic, all_packets_[i].timestamp, all_packets_[i].data);

    auto range = store.timeRange(topic);
    auto start = Clock::now();
    auto result = decoder.decodeAt(range.second);
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    if (result.has_value() && !result->isNull()) {
      frame_times_ms.push_back(elapsed);
    }
  }

  ASSERT_GT(frame_times_ms.size(), 10u);

  // Compute statistics
  std::sort(frame_times_ms.begin(), frame_times_ms.end());
  double min_ms = frame_times_ms.front();
  double max_ms = frame_times_ms.back();
  double sum = 0;
  for (double t : frame_times_ms) {
    sum += t;
  }
  double avg_ms = sum / static_cast<double>(frame_times_ms.size());
  double p95_ms = frame_times_ms[frame_times_ms.size() * 95 / 100];
  double p99_ms = frame_times_ms[frame_times_ms.size() * 99 / 100];

  int over_budget = 0;
  for (double t : frame_times_ms) {
    if (t > 33.0) {
      ++over_budget;
    }
  }

  fprintf(
      stderr, "[Benchmark] %zu frames: min=%.1fms avg=%.1fms p95=%.1fms p99=%.1fms max=%.1fms | %d over 33ms budget\n",
      frame_times_ms.size(), min_ms, avg_ms, p95_ms, p99_ms, max_ms, over_budget);

  // At most 5% of frames should exceed the 33ms budget (480p should be fast)
  EXPECT_LT(over_budget, static_cast<int>(frame_times_ms.size()) / 20)
      << "too many frames exceed 33ms real-time budget";
}

// ---------------------------------------------------------------------------
// B-frame tests: push using DTS (decode order), verify decode works
// ---------------------------------------------------------------------------

const std::string kBframeVideo = "pj_media/testdata/test_1080p_bframes.mp4";

TEST(StreamingVideoDecoderBframeTest, PushWithDtsAndDecode) {
  if (!std::filesystem::exists(kBframeVideo)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }
  auto packets = test::extractAnnexBPackets(kBframeVideo);
  ASSERT_GT(packets.size(), 10u);

  // Verify this video actually has non-monotonic PTS (B-frames)
  bool has_non_monotonic_pts = false;
  for (size_t i = 1; i < packets.size(); ++i) {
    if (packets[i].timestamp < packets[i - 1].timestamp) {
      has_non_monotonic_pts = true;
      break;
    }
  }
  ASSERT_TRUE(has_non_monotonic_pts) << "test video should have B-frames with non-monotonic PTS";

  // Verify DTS IS monotonic
  for (size_t i = 1; i < packets.size(); ++i) {
    ASSERT_GE(packets[i].dts, packets[i - 1].dts) << "DTS should be monotonic at i=" << i;
  }

  // Push using DTS as ObjectStore timestamp
  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/bframes", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  for (const auto& pkt : packets) {
    auto result = store.pushOwned(topic, pkt.dts, pkt.data);
    ASSERT_TRUE(result.has_value()) << "push failed at dts=" << pkt.dts << ": " << result.error();
  }

  EXPECT_EQ(store.entryCount(topic), packets.size());

  // Decode via StreamingVideoDecoder
  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  auto range = store.timeRange(topic);
  auto result = decoder.decodeAt(range.second);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_FALSE(result->isNull());
  EXPECT_EQ(result->width, 1920);
  EXPECT_EQ(result->height, 1080);
}

TEST(StreamingVideoDecoderBframeTest, LiveStreamWithDts) {
  if (!std::filesystem::exists(kBframeVideo)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }
  auto packets = test::extractAnnexBPackets(kBframeVideo);
  ASSERT_GT(packets.size(), 30u);

  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/bframes_live", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  int decoded_count = 0;
  for (const auto& pkt : packets) {
    store.pushOwned(topic, pkt.dts, pkt.data);
    auto range = store.timeRange(topic);
    auto result = decoder.decodeAt(range.second);
    if (result.has_value() && !result->isNull()) {
      ++decoded_count;
    }
  }

  EXPECT_GT(decoded_count, static_cast<int>(packets.size()) / 2) << "should decode most B-frame packets";
}

TEST(StreamingVideoDecoderBframeTest, BenchmarkLiveDecodeBframes) {
  if (!std::filesystem::exists(kBframeVideo)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }
  auto packets = test::extractAnnexBPackets(kBframeVideo);
  ASSERT_GT(packets.size(), 30u);

  ObjectStore store;
  auto topic_result = store.registerTopic({0, "video/bframes_bench", R"({"media_class":"video","encoding":"h264"})"});
  auto topic = topic_result.value();

  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  using Clock = std::chrono::steady_clock;
  std::vector<double> frame_times_ms;
  frame_times_ms.reserve(packets.size());

  for (const auto& pkt : packets) {
    store.pushOwned(topic, pkt.dts, pkt.data);

    auto range = store.timeRange(topic);
    auto start = Clock::now();
    auto result = decoder.decodeAt(range.second);
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    if (result.has_value() && !result->isNull()) {
      frame_times_ms.push_back(elapsed);
    }
  }

  ASSERT_GT(frame_times_ms.size(), 10u);

  std::sort(frame_times_ms.begin(), frame_times_ms.end());
  double sum = 0;
  for (double t : frame_times_ms) {
    sum += t;
  }
  double avg_ms = sum / static_cast<double>(frame_times_ms.size());
  double p95_ms = frame_times_ms[frame_times_ms.size() * 95 / 100];
  double p99_ms = frame_times_ms[frame_times_ms.size() * 99 / 100];
  double max_ms = frame_times_ms.back();

  int over_budget = 0;
  for (double t : frame_times_ms) {
    if (t > 33.0) {
      ++over_budget;
    }
  }

  fprintf(
      stderr, "[Benchmark B-frames 1080p] %zu frames: avg=%.1fms p95=%.1fms p99=%.1fms max=%.1fms | %d over 33ms\n",
      frame_times_ms.size(), avg_ms, p95_ms, p99_ms, max_ms, over_budget);

  EXPECT_LT(over_budget, static_cast<int>(frame_times_ms.size()) / 10)
      << "too many 1080p B-frame frames exceed 33ms budget";
}

TEST(StreamingVideoDecoderBframeTest, SimulateDemoDualTimer) {
  // Exactly simulates the demo: push at 30Hz, display polls at 60Hz.
  // Measures pushes until first displayed frame.
  if (!std::filesystem::exists(kBframeVideo)) {
    GTEST_SKIP() << "test_1080p_bframes.mp4 not found";
  }
  auto packets = test::extractAnnexBPackets(kBframeVideo);
  ASSERT_GT(packets.size(), 100u);

  // Check DTS monotonicity and print first few DTS/PTS values
  fprintf(stderr, "[DemoDualTimer] First 10 packets DTS/PTS:\n");
  for (size_t i = 0; i < 10 && i < packets.size(); ++i) {
    fprintf(stderr, "  [%zu] dts=%ld pts=%ld key=%d\n", i, packets[i].dts, packets[i].timestamp, packets[i].keyframe);
  }

  ObjectStore store;
  auto topic = *store.registerTopic({0, "video/demo_sim", R"({"media_class":"video","encoding":"h264"})"});
  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  size_t push_idx = 0;
  int push_count = 0;
  int display_count = 0;
  bool first_frame_found = false;

  // Simulate interleaved timers: push every 33ms, display every 16ms
  // In real time: push at ticks 0,33,66,99,... display at 0,16,32,48,64,...
  // Simulate 200 ticks at 1ms resolution
  for (int tick_ms = 0; tick_ms < 6000 && !first_frame_found; ++tick_ms) {
    // Push tick: every 33ms
    if (tick_ms % 33 == 0 && push_idx < packets.size()) {
      store.pushOwned(topic, packets[push_idx].dts, packets[push_idx].data);
      ++push_idx;
      ++push_count;
    }

    // Display tick: every 16ms
    if (tick_ms % 16 == 0) {
      auto count = store.entryCount(topic);
      if (count == 0) {
        continue;
      }
      auto range = store.timeRange(topic);
      auto result = decoder.decodeAt(range.second);
      ++display_count;

      if (result.has_value() && !result->isNull()) {
        fprintf(
            stderr, "[DemoDualTimer] First frame at tick=%dms, push_count=%d, display_count=%d\n", tick_ms, push_count,
            display_count);
        first_frame_found = true;
      }
    }
  }

  ASSERT_TRUE(first_frame_found) << "no frame after 6 seconds of simulation";
  // With B-frame reorder depth ~30, expect first frame at ~30-40 pushes (~1s)
  EXPECT_LT(push_count, 100) << "first frame took too many pushes — possible O(n^2) regression";
}

TEST(StreamingVideoDecoderBframeTest, TimeToFirstFrameUserVideo) {
  // Test with the user's actual video file if available
  const std::string user_video = "/home/davide/ws_plotjuggler/video_1920.mp4";
  if (!std::filesystem::exists(user_video)) {
    GTEST_SKIP() << "video_1920.mp4 not found";
  }
  auto packets = test::extractAnnexBPackets(user_video);
  ASSERT_GT(packets.size(), 10u);

  fprintf(stderr, "[video_1920] %zu packets\n", packets.size());
  fprintf(stderr, "[video_1920] First 5 DTS/PTS:\n");
  for (size_t i = 0; i < 5 && i < packets.size(); ++i) {
    fprintf(
        stderr, "  [%zu] dts=%ld pts=%ld key=%d size=%zu\n", i, packets[i].dts, packets[i].timestamp,
        packets[i].keyframe, packets[i].data.size());
  }

  // Check DTS monotonicity
  for (size_t i = 1; i < packets.size(); ++i) {
    if (packets[i].dts < packets[i - 1].dts) {
      fprintf(stderr, "[video_1920] NON-MONOTONIC DTS at i=%zu: %ld < %ld\n", i, packets[i].dts, packets[i - 1].dts);
      break;
    }
  }

  ObjectStore store;
  auto topic = *store.registerTopic({0, "video/user", R"({"media_class":"video","encoding":"h264"})"});
  StreamingVideoDecoder decoder;
  decoder.attach(&store, topic);

  int push_count = 0;
  for (const auto& pkt : packets) {
    auto push_result = store.pushOwned(topic, pkt.dts, pkt.data);
    if (!push_result.has_value()) {
      fprintf(
          stderr, "[video_1920] Push FAILED at %d: %s (dts=%ld)\n", push_count, push_result.error().c_str(), pkt.dts);
      break;
    }
    ++push_count;

    auto range = store.timeRange(topic);
    auto result = decoder.decodeAt(range.second);
    if (result.has_value() && !result->isNull()) {
      fprintf(stderr, "[video_1920] First frame after %d pushes, %dx%d\n", push_count, result->width, result->height);
      EXPECT_LT(push_count, 100) << "too many pushes for first frame";
      return;
    }
  }
  FAIL() << "no frame after " << push_count << " pushes";
}

TEST(StreamingVideoDecoderBframeTest, TimeToFirstFrame) {
  // Measure how long it takes to get the first decoded frame.
  // With B-frames, FFmpeg's reorder buffer delays output.
  using Clock = std::chrono::steady_clock;

  auto measure_first_frame = [](const std::string& path, const char* label) {
    if (!std::filesystem::exists(path)) {
      fprintf(stderr, "[%s] SKIPPED — file not found\n", label);
      return;
    }
    auto packets = test::extractAnnexBPackets(path);

    ObjectStore store;
    auto topic = *store.registerTopic({0, "video/ttff", R"({"media_class":"video","encoding":"h264"})"});
    StreamingVideoDecoder decoder;
    decoder.attach(&store, topic);

    auto wall_start = Clock::now();
    int frames_pushed = 0;
    for (const auto& pkt : packets) {
      store.pushOwned(topic, pkt.dts, pkt.data);
      ++frames_pushed;

      auto range = store.timeRange(topic);
      auto result = decoder.decodeAt(range.second);
      if (result.has_value() && !result->isNull()) {
        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - wall_start).count();
        fprintf(stderr, "[%s] First frame after %d pushes, %.1fms\n", label, frames_pushed, elapsed);
        return;
      }
    }
    fprintf(stderr, "[%s] NO FRAME after %d pushes!\n", label, frames_pushed);
  };

  measure_first_frame("pj_media/testdata/test_480p.mp4", "480p I+P");
  measure_first_frame("pj_media/testdata/test_1080p.mp4", "1080p I+P");
  measure_first_frame("pj_media/testdata/test_1080p_bframes.mp4", "1080p B-frames");
}

}  // namespace
}  // namespace PJ
