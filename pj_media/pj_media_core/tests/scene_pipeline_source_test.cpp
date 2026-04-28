#include "pj_media_core/scene_pipeline_source.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/scene_decoder.h"
#include "pj_media_core/scene_frame.h"

namespace PJ {
namespace {

// Mock decoder: interprets the first 8 bytes as a uint64 timestamp; the
// remainder as a uint32_t count of empty PointsAnnotation entries.
class MockDecoder final : public ISceneDecoder {
 public:
  std::atomic<int> calls{0};

  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    calls.fetch_add(1, std::memory_order_relaxed);
    if (size < 12) {
      return unexpected(std::string("mock decoder: too small"));
    }
    int64_t ts = 0;
    std::memcpy(&ts, data, 8);
    uint32_t n = 0;
    std::memcpy(&n, data + 8, 4);

    SceneFrame sf;
    sf.timestamp = ts;
    ImageAnnotation ia;
    ia.timestamp = ts;
    ia.points.resize(n);
    sf.annotations.push_back(std::move(ia));
    return sf;
  }
};

std::vector<uint8_t> makeMockBytes(int64_t ts, uint32_t n) {
  std::vector<uint8_t> v(12, 0);
  std::memcpy(v.data(), &ts, 8);
  std::memcpy(v.data() + 8, &n, 4);
  return v;
}

ObjectTopicId registerTopic(ObjectStore& store, const std::string& name) {
  auto id = store.registerTopic({.dataset_id = 1, .topic_name = name, .metadata_json = "{}"});
  return id.has_value() ? *id : ObjectTopicId{};
}

TEST(ScenePipelineSourceTest, EmptyStoreReturnsNullopt) {
  ObjectStore store;
  auto topic = registerTopic(store, "annot");
  ScenePipelineSource source(&store, topic, std::make_unique<MockDecoder>());

  source.setTimestamp(100);
  EXPECT_FALSE(source.takeFrame().has_value());
}

TEST(ScenePipelineSourceTest, FetchAndDecode) {
  ObjectStore store;
  auto topic = registerTopic(store, "annot");
  store.pushOwned(topic, 1000, makeMockBytes(1000, 3));

  auto decoder = std::make_unique<MockDecoder>();
  auto* decoder_ptr = decoder.get();
  ScenePipelineSource source(&store, topic, std::move(decoder));

  source.setTimestamp(1000);
  EXPECT_EQ(decoder_ptr->calls.load(), 1);

  auto frame = source.takeFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_FALSE(frame->base.has_value());
  ASSERT_EQ(frame->overlays.size(), 1u);
  EXPECT_EQ(frame->overlays[0].timestamp, 1000);
  ASSERT_EQ(frame->overlays[0].annotations.size(), 1u);
  EXPECT_EQ(frame->overlays[0].annotations[0].points.size(), 3u);
}

TEST(ScenePipelineSourceTest, RepeatedSameTimestampSkipsDecode) {
  ObjectStore store;
  auto topic = registerTopic(store, "annot");
  store.pushOwned(topic, 1000, makeMockBytes(1000, 1));

  auto decoder = std::make_unique<MockDecoder>();
  auto* decoder_ptr = decoder.get();
  ScenePipelineSource source(&store, topic, std::move(decoder));

  source.setTimestamp(1000);
  source.setTimestamp(1000);
  source.setTimestamp(1000);
  EXPECT_EQ(decoder_ptr->calls.load(), 1);  // dedup'd
}

TEST(ScenePipelineSourceTest, TakeFrameConsumesPending) {
  ObjectStore store;
  auto topic = registerTopic(store, "annot");
  store.pushOwned(topic, 1000, makeMockBytes(1000, 2));

  ScenePipelineSource source(&store, topic, std::make_unique<MockDecoder>());
  source.setTimestamp(1000);

  EXPECT_TRUE(source.takeFrame().has_value());
  EXPECT_FALSE(source.takeFrame().has_value());  // already consumed
}

TEST(ScenePipelineSourceTest, LatestAtSemantics) {
  ObjectStore store;
  auto topic = registerTopic(store, "annot");
  store.pushOwned(topic, 1000, makeMockBytes(1000, 1));
  store.pushOwned(topic, 2000, makeMockBytes(2000, 2));
  store.pushOwned(topic, 3000, makeMockBytes(3000, 3));

  ScenePipelineSource source(&store, topic, std::make_unique<MockDecoder>());

  // ts=2500 -> latestAt should return entry at 2000
  source.setTimestamp(2500);
  auto frame = source.takeFrame();
  ASSERT_TRUE(frame.has_value());
  ASSERT_EQ(frame->overlays.size(), 1u);
  EXPECT_EQ(frame->overlays[0].timestamp, 2000);
  EXPECT_EQ(frame->overlays[0].annotations[0].points.size(), 2u);
}

TEST(ScenePipelineSourceTest, ConcurrentPushAndQuery) {
  // Mirrors the pj_datastore concurrency tests: indexer thread pushes lazy
  // entries while a reader thread queries via setTimestamp + takeFrame.
  ObjectStore store;
  auto topic = registerTopic(store, "annot");

  constexpr int kPushCount = 200;
  std::atomic<size_t> indexed{0};
  std::thread indexer([&]() {
    for (int i = 0; i < kPushCount; ++i) {
      auto bytes = makeMockBytes(static_cast<int64_t>(i) * 100, 1);
      auto status = store.pushLazy(topic, static_cast<Timestamp>(i) * 100,
                                   [bytes]() -> std::vector<uint8_t> { return bytes; });
      if (status.has_value()) {
        indexed.fetch_add(1, std::memory_order_release);
      }
    }
  });

  ScenePipelineSource source(&store, topic, std::make_unique<MockDecoder>());
  std::atomic<int> resolved{0};
  std::thread reader([&]() {
    for (int i = 0; i < kPushCount * 2; ++i) {
      size_t n = indexed.load(std::memory_order_acquire);
      if (n == 0) {
        std::this_thread::yield();
        continue;
      }
      source.setTimestamp(static_cast<Timestamp>(i % static_cast<int>(n)) * 100);
      if (source.takeFrame().has_value()) {
        resolved.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  indexer.join();
  reader.join();
  EXPECT_GT(resolved.load(), 0);
}

}  // namespace
}  // namespace PJ
