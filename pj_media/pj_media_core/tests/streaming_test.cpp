#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "pj_datastore/object_store.hpp"

namespace PJ {
namespace {

std::vector<uint8_t> makeFakeJpeg(size_t size, uint8_t fill) {
  return std::vector<uint8_t>(size, fill);
}

TEST(StreamingTest, RetentionWindowDuringPush) {
  ObjectStore store;
  auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = "cam/stream", .metadata_json = "{}"});
  ASSERT_TRUE(id_or.has_value());
  auto topic = *id_or;

  // 2-second retention window
  store.setRetentionBudget(topic, {.time_window_ns = 2'000'000'000, .max_memory_bytes = 0});

  // Push 300 frames at 30 Hz (10 seconds of data)
  constexpr int kFrameCount = 300;
  constexpr int64_t kFrameIntervalNs = 33'333'333;  // ~30 Hz

  for (int i = 0; i < kFrameCount; ++i) {
    auto ts = static_cast<Timestamp>(i) * kFrameIntervalNs;
    auto result = store.pushOwned(topic, ts, makeFakeJpeg(1024, static_cast<uint8_t>(i % 256)));
    ASSERT_TRUE(result.has_value()) << result.error();
  }

  // After 10s of data with 2s window, only the last ~60 frames should remain
  auto [t_min, t_max] = store.timeRange(topic);
  auto window = t_max - t_min;
  EXPECT_LE(window, 2'100'000'000) << "time window should be ~2s, got " << window / 1'000'000 << " ms";
  EXPECT_GE(store.entryCount(topic), 50u);
  EXPECT_LE(store.entryCount(topic), 70u);
}

TEST(StreamingTest, MemoryCapDuringPush) {
  ObjectStore store;
  auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = "cam/mem", .metadata_json = "{}"});
  ASSERT_TRUE(id_or.has_value());
  auto topic = *id_or;

  // 50 KB memory cap
  store.setRetentionBudget(topic, {.time_window_ns = 0, .max_memory_bytes = 50'000});

  for (int i = 0; i < 200; ++i) {
    store.pushOwned(topic, static_cast<Timestamp>(i) * 1'000'000, makeFakeJpeg(1024, 0));
  }

  EXPECT_LE(store.memoryUsage(topic), 50'000u);
  EXPECT_GT(store.entryCount(topic), 0u);
}

TEST(StreamingTest, PauseFreezesScrubResume) {
  ObjectStore store;
  auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = "cam/pause", .metadata_json = "{}"});
  ASSERT_TRUE(id_or.has_value());
  auto topic = *id_or;

  store.setRetentionBudget(topic, {.time_window_ns = 1'000'000'000, .max_memory_bytes = 0});

  // Push 3 seconds of data at 30 Hz
  constexpr int64_t kInterval = 33'333'333;
  for (int i = 0; i < 90; ++i) {
    store.pushOwned(topic, static_cast<Timestamp>(i) * kInterval, makeFakeJpeg(512, static_cast<uint8_t>(i)));
  }

  // "Pause" = stop pushing. Buffer should be frozen.
  auto count_at_pause = store.entryCount(topic);
  auto [t_min_pause, t_max_pause] = store.timeRange(topic);

  // Scrub: all entries in the retained window should be accessible
  for (size_t i = 0; i < count_at_pause; ++i) {
    auto entry = store.at(topic, i);
    ASSERT_TRUE(entry.has_value()) << "entry " << i << " inaccessible during scrub";
    EXPECT_GT(entry->data->size(), 0u);
  }

  // latestAt at midpoint should work
  auto mid = t_min_pause + (t_max_pause - t_min_pause) / 2;
  auto mid_entry = store.latestAt(topic, mid);
  ASSERT_TRUE(mid_entry.has_value());

  // "Resume" = push more data. Old entries should be evicted.
  for (int i = 90; i < 150; ++i) {
    store.pushOwned(topic, static_cast<Timestamp>(i) * kInterval, makeFakeJpeg(512, static_cast<uint8_t>(i)));
  }

  auto [t_min_resume, t_max_resume] = store.timeRange(topic);
  EXPECT_GT(t_min_resume, t_min_pause) << "old entries should be evicted after resume";
  EXPECT_GT(t_max_resume, t_max_pause);
}

TEST(StreamingTest, ConcurrentPushAndRead) {
  ObjectStore store;
  auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = "cam/concurrent", .metadata_json = "{}"});
  ASSERT_TRUE(id_or.has_value());
  auto topic = *id_or;

  store.setRetentionBudget(topic, {.time_window_ns = 500'000'000, .max_memory_bytes = 0});

  std::atomic<bool> stop{false};
  std::atomic<int> read_count{0};

  // Writer: push at ~100 Hz
  std::thread writer([&]() {
    for (int i = 0; i < 500 && !stop.load(); ++i) {
      store.pushOwned(topic, static_cast<Timestamp>(i) * 10'000'000, makeFakeJpeg(256, static_cast<uint8_t>(i)));
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    stop.store(true);
  });

  // Reader: poll latestAt continuously
  std::thread reader([&]() {
    while (!stop.load()) {
      auto [t_min, t_max] = store.timeRange(topic);
      if (t_max > 0) {
        auto entry = store.latestAt(topic, t_max);
        if (entry.has_value() && !entry->data->empty()) {
          read_count.fetch_add(1);
        }
      }
    }
  });

  writer.join();
  reader.join();

  EXPECT_GT(read_count.load(), 0) << "reader should have read at least one entry";
  EXPECT_LE(store.memoryUsage(topic), 256u * 60) << "retention should limit memory";
}

}  // namespace
}  // namespace PJ
