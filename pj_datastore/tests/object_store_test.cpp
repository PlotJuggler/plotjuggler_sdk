// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/object_store.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace PJ {
namespace {

ObjectTopicId registerTestTopic(ObjectStore& store, const std::string& name = "test/topic") {
  auto id_or = store.registerTopic({.dataset_id = 1, .topic_name = name, .metadata_json = "{}"});
  EXPECT_TRUE(id_or.has_value());
  return *id_or;
}

std::vector<uint8_t> makePayload(size_t size, uint8_t fill = 0xAB) {
  return std::vector<uint8_t>(size, fill);
}

// =========================================================================
// Registration
// =========================================================================

TEST(ObjectStoreTest, RegisterAndDescriptor) {
  ObjectStore store;
  auto id = registerTestTopic(store, "cam/image");
  auto desc = store.descriptor(id);
  EXPECT_EQ(desc.topic_name, "cam/image");
  EXPECT_EQ(desc.dataset_id, 1u);
}

TEST(ObjectStoreTest, DuplicateRegistrationFails) {
  ObjectStore store;
  registerTestTopic(store, "cam/image");
  auto dup = store.registerTopic({.dataset_id = 1, .topic_name = "cam/image", .metadata_json = "{}"});
  EXPECT_FALSE(dup.has_value());
}

TEST(ObjectStoreTest, SameNameDifferentDatasetOk) {
  ObjectStore store;
  auto id1 = registerTestTopic(store, "cam/image");
  auto id2_or = store.registerTopic({.dataset_id = 2, .topic_name = "cam/image", .metadata_json = "{}"});
  ASSERT_TRUE(id2_or.has_value());
  EXPECT_NE(id1.id, id2_or->id);
}

TEST(ObjectStoreTest, FindTopicReturnsRegisteredId) {
  ObjectStore store;
  auto id = registerTestTopic(store, "cam/image");
  auto found = store.findTopic(1, "cam/image");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->id, id.id);
}

TEST(ObjectStoreTest, FindTopicMissingReturnsNullopt) {
  ObjectStore store;
  registerTestTopic(store, "cam/image");
  EXPECT_FALSE(store.findTopic(1, "other/topic").has_value());
  EXPECT_FALSE(store.findTopic(99, "cam/image").has_value());
}

TEST(ObjectStoreTest, ListTopics) {
  ObjectStore store;
  auto id1 = registerTestTopic(store, "topic_a");
  auto id2 = registerTestTopic(store, "topic_b");
  auto all = store.listTopics();
  EXPECT_EQ(all.size(), 2u);
}

TEST(ObjectStoreTest, ListTopicsByDataset) {
  ObjectStore store;
  store.registerTopic({.dataset_id = 1, .topic_name = "a", .metadata_json = "{}"});
  store.registerTopic({.dataset_id = 2, .topic_name = "b", .metadata_json = "{}"});
  store.registerTopic({.dataset_id = 1, .topic_name = "c", .metadata_json = "{}"});
  auto ds1 = store.listTopics(1);
  auto ds2 = store.listTopics(2);
  EXPECT_EQ(ds1.size(), 2u);
  EXPECT_EQ(ds2.size(), 1u);
}

// =========================================================================
// Push + basic queries
// =========================================================================

TEST(ObjectStoreTest, PushOwnedAndEntryCount) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  constexpr size_t kCount = 100;
  for (size_t i = 0; i < kCount; ++i) {
    auto ts = static_cast<Timestamp>(i) * 1000;
    auto result = store.pushOwned(id, ts, makePayload(64));
    ASSERT_TRUE(result.has_value()) << result.error();
  }
  EXPECT_EQ(store.entryCount(id), kCount);
}

TEST(ObjectStoreTest, TimeRange) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(8));
  store.pushOwned(id, 500, makePayload(8));
  store.pushOwned(id, 900, makePayload(8));
  auto [t_min, t_max] = store.timeRange(id);
  EXPECT_EQ(t_min, 100);
  EXPECT_EQ(t_max, 900);
}

TEST(ObjectStoreTest, EmptyTopicQueries) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  EXPECT_EQ(store.entryCount(id), 0u);
  auto [t_min, t_max] = store.timeRange(id);
  EXPECT_EQ(t_min, 0);
  EXPECT_EQ(t_max, 0);
  EXPECT_FALSE(store.latestAt(id, 1000).has_value());
  EXPECT_FALSE(store.at(id, 0).has_value());
  EXPECT_FALSE(store.indexAt(id, 1000).has_value());
}

TEST(ObjectStoreTest, UnknownTopicQueries) {
  ObjectStore store;
  ObjectTopicId bogus{999};
  EXPECT_EQ(store.entryCount(bogus), 0u);
  EXPECT_FALSE(store.latestAt(bogus, 0).has_value());
  EXPECT_FALSE(store.at(bogus, 0).has_value());
  auto result = store.pushOwned(bogus, 0, makePayload(8));
  EXPECT_FALSE(result.has_value());
}

// =========================================================================
// latestAt semantics
// =========================================================================

TEST(ObjectStoreTest, LatestAtExact) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4, 0x01));
  store.pushOwned(id, 200, makePayload(4, 0x02));
  store.pushOwned(id, 300, makePayload(4, 0x03));

  auto r = store.latestAt(id, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->timestamp, 200);
  EXPECT_EQ(r->payload.bytes[0], 0x02);
}

TEST(ObjectStoreTest, LatestAtBetween) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4, 0x01));
  store.pushOwned(id, 300, makePayload(4, 0x03));

  auto r = store.latestAt(id, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->timestamp, 100);
}

TEST(ObjectStoreTest, LatestAtBeforeFirst) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  EXPECT_FALSE(store.latestAt(id, 50).has_value());
}

TEST(ObjectStoreTest, LatestAtAfterLast) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4, 0x01));
  store.pushOwned(id, 200, makePayload(4, 0x02));

  auto r = store.latestAt(id, 999);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->timestamp, 200);
}

// =========================================================================
// at(index)
// =========================================================================

TEST(ObjectStoreTest, AtValidIndex) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4, 0x01));
  store.pushOwned(id, 200, makePayload(4, 0x02));

  auto r0 = store.at(id, 0);
  ASSERT_TRUE(r0.has_value());
  EXPECT_EQ(r0->timestamp, 100);

  auto r1 = store.at(id, 1);
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->timestamp, 200);
}

TEST(ObjectStoreTest, AtOutOfRange) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  EXPECT_FALSE(store.at(id, 1).has_value());
  EXPECT_FALSE(store.at(id, 999).has_value());
}

// =========================================================================
// indexAt
// =========================================================================

TEST(ObjectStoreTest, IndexAtExact) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  store.pushOwned(id, 200, makePayload(4));
  store.pushOwned(id, 300, makePayload(4));

  auto idx = store.indexAt(id, 200);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1u);
}

TEST(ObjectStoreTest, IndexAtBetween) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  store.pushOwned(id, 300, makePayload(4));

  auto idx = store.indexAt(id, 250);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 0u);
}

TEST(ObjectStoreTest, IndexAtBeforeFirst) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  EXPECT_FALSE(store.indexAt(id, 50).has_value());
}

// =========================================================================
// EntryTimestampsView
// =========================================================================

TEST(ObjectStoreTest, EntryTimestampsView) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  store.pushOwned(id, 200, makePayload(4));
  store.pushOwned(id, 300, makePayload(4));

  auto view = store.entryTimestamps(id);
  EXPECT_EQ(view.size(), 3u);
  EXPECT_EQ(view[0], 100);
  EXPECT_EQ(view[1], 200);
  EXPECT_EQ(view[2], 300);

  size_t count = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    ++count;
  }
  EXPECT_EQ(count, 3u);
}

TEST(ObjectStoreTest, EntryTimestampsViewEmpty) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  auto view = store.entryTimestamps(id);
  EXPECT_TRUE(view.empty());
  EXPECT_EQ(view.size(), 0u);
}

// =========================================================================
// pushLazy
// =========================================================================

TEST(ObjectStoreTest, PushLazyResolves) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  int call_count = 0;
  store.pushLazy(id, 100, [&call_count]() -> sdk::PayloadView {
    ++call_count;
    return sdk::makePayloadView({0xDE, 0xAD});
  });

  EXPECT_EQ(call_count, 0);
  auto r = store.latestAt(id, 100);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(r->payload.bytes.size(), 2u);
  EXPECT_EQ(r->payload.bytes[0], 0xDE);
}

// Regression: anchor type-erasure must survive resolveEntry. The anchor here
// is a shared_ptr<TestBuffer> (not vector); a prior static_pointer_cast to
// vector would UB.
TEST(ObjectStoreTest, PushLazyPreservesAnchorType) {
  struct TestBuffer {
    std::array<uint8_t, 4> bytes{0x11, 0x22, 0x33, 0x44};
  };
  ObjectStore store;
  auto id = registerTestTopic(store);

  auto buffer = std::make_shared<TestBuffer>();
  std::weak_ptr<TestBuffer> weak_buffer = buffer;

  store.pushLazy(id, 100, [buffer]() -> sdk::PayloadView {
    return sdk::PayloadView{
        Span<const uint8_t>{buffer->bytes.data(), buffer->bytes.size()},
        sdk::BufferAnchor{buffer},
    };
  });

  auto r = store.latestAt(id, 100);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->payload.bytes.size(), 4u);
  EXPECT_EQ(r->payload.bytes[0], 0x11);
  EXPECT_EQ(r->payload.bytes[3], 0x44);
  EXPECT_FALSE(weak_buffer.expired());  // anchor still holds the buffer alive
}

// Regression: the producer's Span is a sub-range of the anchor's storage.
// resolveEntry must propagate it verbatim — not the anchor's full extent.
TEST(ObjectStoreTest, PushLazyHonorsSpanSubview) {
  ObjectStore store;
  auto id = registerTestTopic(store);

  auto chunk = std::make_shared<std::vector<uint8_t>>(100);
  for (size_t i = 0; i < chunk->size(); ++i) {
    (*chunk)[i] = static_cast<uint8_t>(i);
  }

  store.pushLazy(id, 100, [chunk]() -> sdk::PayloadView {
    return sdk::PayloadView{
        Span<const uint8_t>{chunk->data() + 20, 10},  // bytes [20, 30)
        sdk::BufferAnchor{chunk},
    };
  });

  auto r = store.latestAt(id, 100);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->payload.bytes.size(), 10u);
  EXPECT_EQ(r->payload.bytes.data(), chunk->data() + 20);
  EXPECT_EQ(r->payload.bytes[0], 20);
  EXPECT_EQ(r->payload.bytes[9], 29);
}

// =========================================================================
// Timestamp monotonicity
// =========================================================================

TEST(ObjectStoreTest, OutOfOrderPushFails) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 200, makePayload(4));
  auto result = store.pushOwned(id, 100, makePayload(4));
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(store.entryCount(id), 1u);
}

TEST(ObjectStoreTest, EqualTimestampAllowed) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4));
  auto result = store.pushOwned(id, 100, makePayload(4));
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(store.entryCount(id), 2u);
}

// =========================================================================
// Owning handle survives eviction
// =========================================================================

TEST(ObjectStoreTest, HandleSurvivesEviction) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.pushOwned(id, 100, makePayload(4, 0xAA));
  store.pushOwned(id, 200, makePayload(4, 0xBB));

  auto handle = store.latestAt(id, 100);
  ASSERT_TRUE(handle.has_value());
  EXPECT_EQ(handle->payload.bytes[0], 0xAA);

  store.evictBefore(id, 150);
  EXPECT_EQ(store.entryCount(id), 1u);

  EXPECT_EQ(handle->payload.bytes.size(), 4u);
  EXPECT_EQ(handle->payload.bytes[0], 0xAA);
}

// =========================================================================
// evictBefore
// =========================================================================

TEST(ObjectStoreTest, EvictBefore) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  for (int i = 0; i < 10; ++i) {
    store.pushOwned(id, static_cast<Timestamp>(i) * 100, makePayload(8));
  }
  EXPECT_EQ(store.entryCount(id), 10u);

  store.evictBefore(id, 500);
  EXPECT_EQ(store.entryCount(id), 5u);
  auto [t_min, t_max] = store.timeRange(id);
  EXPECT_EQ(t_min, 500);
  EXPECT_EQ(t_max, 900);
}

// =========================================================================
// removeTopic / clear
// =========================================================================

TEST(ObjectStoreTest, RemoveTopic) {
  ObjectStore store;
  auto id = registerTestTopic(store, "to_remove");
  store.pushOwned(id, 100, makePayload(4));
  store.removeTopic(id);
  EXPECT_EQ(store.entryCount(id), 0u);
  EXPECT_TRUE(store.listTopics().empty());
}

TEST(ObjectStoreTest, Clear) {
  ObjectStore store;
  registerTestTopic(store, "a");
  registerTestTopic(store, "b");
  EXPECT_EQ(store.listTopics().size(), 2u);
  store.clear();
  EXPECT_TRUE(store.listTopics().empty());
}

// =========================================================================
// Retention budget
// =========================================================================

TEST(ObjectStoreTest, TimeWindowRetention) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.setRetentionBudget(id, {.time_window_ns = 2000, .max_memory_bytes = 0});

  for (int i = 0; i < 100; ++i) {
    store.pushOwned(id, static_cast<Timestamp>(i) * 100, makePayload(8));
  }

  auto [t_min, t_max] = store.timeRange(id);
  EXPECT_GE(t_min, t_max - 2000);
}

TEST(ObjectStoreTest, MemoryRetention) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  store.setRetentionBudget(id, {.time_window_ns = 0, .max_memory_bytes = 500});

  for (int i = 0; i < 100; ++i) {
    store.pushOwned(id, static_cast<Timestamp>(i) * 100, makePayload(100));
  }

  EXPECT_LE(store.memoryUsage(id), 500u);
}

TEST(ObjectStoreTest, DefaultBudgetNoEviction) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  for (int i = 0; i < 100; ++i) {
    store.pushOwned(id, static_cast<Timestamp>(i) * 100, makePayload(100));
  }
  EXPECT_EQ(store.entryCount(id), 100u);
}

TEST(ObjectStoreTest, LazyEntriesZeroMemory) {
  ObjectStore store;
  auto id = registerTestTopic(store);
  for (int i = 0; i < 10; ++i) {
    store.pushLazy(id, static_cast<Timestamp>(i) * 100, []() -> sdk::PayloadView {
      return sdk::makePayloadView(makePayload(1000));
    });
  }
  EXPECT_EQ(store.memoryUsage(id), 0u);
}

// =========================================================================
// Concurrency (basic smoke test — M2 will add thorough tests)
// =========================================================================

TEST(ObjectStoreTest, ConcurrentReadWriteSmoke) {
  ObjectStore store;
  auto id = registerTestTopic(store);

  constexpr int kPushCount = 1000;
  std::thread writer([&]() {
    for (int i = 0; i < kPushCount; ++i) {
      store.pushOwned(id, static_cast<Timestamp>(i) * 100, makePayload(16));
    }
  });

  std::thread reader([&]() {
    for (int i = 0; i < kPushCount; ++i) {
      store.latestAt(id, static_cast<Timestamp>(i) * 100);
      store.entryCount(id);
    }
  });

  writer.join();
  reader.join();
  EXPECT_EQ(store.entryCount(id), static_cast<size_t>(kPushCount));
}

// =========================================================================
// Cross-store flush (flushTo)
// =========================================================================

namespace {

ObjectTopicId registerSameDescriptor(
    ObjectStore& store, DatasetId dataset_id = 1, const std::string& name = "test/topic") {
  auto id_or = store.registerTopic({.dataset_id = dataset_id, .topic_name = name, .metadata_json = "{}"});
  EXPECT_TRUE(id_or.has_value());
  return *id_or;
}

}  // namespace

TEST(ObjectStoreFlushTest, BasicMoveOwnedEntries) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  src.pushOwned(src_id, 100, makePayload(8, 0xAA));
  src.pushOwned(src_id, 200, makePayload(8, 0xBB));
  src.pushOwned(src_id, 300, makePayload(8, 0xCC));

  ASSERT_EQ(src.entryCount(src_id), 3u);
  ASSERT_EQ(dst.entryCount(dst_id), 0u);

  auto result = src.flushTo(dst);
  ASSERT_TRUE(result.has_value()) << result.error();

  EXPECT_EQ(src.entryCount(src_id), 0u);
  EXPECT_EQ(dst.entryCount(dst_id), 3u);

  auto e0 = dst.at(dst_id, 0);
  ASSERT_TRUE(e0.has_value());
  EXPECT_EQ(e0->timestamp, 100);
  EXPECT_EQ(e0->payload.bytes.size(), 8u);
  EXPECT_EQ(e0->payload.bytes[0], 0xAA);

  auto e2 = dst.at(dst_id, 2);
  ASSERT_TRUE(e2.has_value());
  EXPECT_EQ(e2->timestamp, 300);
  EXPECT_EQ(e2->payload.bytes[0], 0xCC);
}

TEST(ObjectStoreFlushTest, PreservesTopicRegistrationOnSrc) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  registerSameDescriptor(dst);

  src.pushOwned(src_id, 100, makePayload(4));

  ASSERT_TRUE(src.flushTo(dst).has_value());

  // src is empty but the topic is still registered — the same id can accept new pushes.
  EXPECT_EQ(src.entryCount(src_id), 0u);
  auto post_push = src.pushOwned(src_id, 400, makePayload(4));
  EXPECT_TRUE(post_push.has_value());
  EXPECT_EQ(src.entryCount(src_id), 1u);
}

TEST(ObjectStoreFlushTest, AppendsToExistingDstEntries) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  dst.pushOwned(dst_id, 50, makePayload(4, 0x11));
  dst.pushOwned(dst_id, 100, makePayload(4, 0x22));
  src.pushOwned(src_id, 200, makePayload(4, 0x33));
  src.pushOwned(src_id, 300, makePayload(4, 0x44));

  ASSERT_TRUE(src.flushTo(dst).has_value());

  EXPECT_EQ(dst.entryCount(dst_id), 4u);
  EXPECT_EQ(dst.at(dst_id, 0)->timestamp, 50);
  EXPECT_EQ(dst.at(dst_id, 1)->timestamp, 100);
  EXPECT_EQ(dst.at(dst_id, 2)->timestamp, 200);
  EXPECT_EQ(dst.at(dst_id, 3)->timestamp, 300);
}

TEST(ObjectStoreFlushTest, RejectsMonotonicityViolation) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  dst.pushOwned(dst_id, 200, makePayload(4));
  src.pushOwned(src_id, 100, makePayload(4));  // earlier than dst's last

  auto result = src.flushTo(dst);
  EXPECT_FALSE(result.has_value());

  // Neither store is mutated on failure.
  EXPECT_EQ(src.entryCount(src_id), 1u);
  EXPECT_EQ(dst.entryCount(dst_id), 1u);
}

TEST(ObjectStoreFlushTest, RejectsUnknownTopicInDst) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src, 1, "missing/topic");
  // dst has a different topic.
  registerSameDescriptor(dst, 1, "other/topic");

  src.pushOwned(src_id, 100, makePayload(4));

  auto result = src.flushTo(dst);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(src.entryCount(src_id), 1u);
}

TEST(ObjectStoreFlushTest, EmptySourceSeriesSkipped) {
  ObjectStore src, dst;
  auto src_id_with_data = registerSameDescriptor(src, 1, "with/data");
  registerSameDescriptor(src, 1, "empty/series");  // registered but no pushes

  auto dst_id_with_data = registerSameDescriptor(dst, 1, "with/data");
  // dst does NOT register "empty/series" — flush should still succeed since src
  // has no entries for it.

  src.pushOwned(src_id_with_data, 100, makePayload(4));

  auto result = src.flushTo(dst);
  EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error());
  EXPECT_EQ(dst.entryCount(dst_id_with_data), 1u);
}

TEST(ObjectStoreFlushTest, RejectsSameStore) {
  ObjectStore store;
  auto id = registerSameDescriptor(store);
  store.pushOwned(id, 100, makePayload(4));

  auto result = store.flushTo(store);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(store.entryCount(id), 1u);
}

TEST(ObjectStoreFlushTest, MultipleTopicsFlushed) {
  ObjectStore src, dst;
  auto src_a = registerSameDescriptor(src, 1, "topic/a");
  auto src_b = registerSameDescriptor(src, 1, "topic/b");
  auto dst_a = registerSameDescriptor(dst, 1, "topic/a");
  auto dst_b = registerSameDescriptor(dst, 1, "topic/b");

  src.pushOwned(src_a, 100, makePayload(4, 0xAA));
  src.pushOwned(src_b, 100, makePayload(4, 0xBB));
  src.pushOwned(src_a, 200, makePayload(4, 0xCC));
  src.pushOwned(src_b, 200, makePayload(4, 0xDD));

  ASSERT_TRUE(src.flushTo(dst).has_value());

  EXPECT_EQ(dst.entryCount(dst_a), 2u);
  EXPECT_EQ(dst.entryCount(dst_b), 2u);
  EXPECT_EQ(dst.at(dst_a, 0)->payload.bytes[0], 0xAA);
  EXPECT_EQ(dst.at(dst_b, 1)->payload.bytes[0], 0xDD);
}

TEST(ObjectStoreFlushTest, LazyEntriesPreserveSemantics) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  int src_invocations = 0;
  src.pushLazy(src_id, 100, [&src_invocations]() -> sdk::PayloadView {
    ++src_invocations;
    return sdk::makePayloadView({0xDE, 0xAD, 0xBE, 0xEF});
  });

  // Flush itself must NOT invoke the closure — it just moves the std::function.
  EXPECT_EQ(src_invocations, 0);
  ASSERT_TRUE(src.flushTo(dst).has_value());
  EXPECT_EQ(src_invocations, 0);

  EXPECT_EQ(src.entryCount(src_id), 0u);
  EXPECT_EQ(dst.entryCount(dst_id), 1u);

  // Reading from dst invokes the closure once, exactly like in src.
  auto resolved = dst.latestAt(dst_id, 100);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(src_invocations, 1);
  ASSERT_EQ(resolved->payload.bytes.size(), 4u);
  EXPECT_EQ(resolved->payload.bytes[0], 0xDE);
}

TEST(ObjectStoreFlushTest, RetentionBudgetAppliedAfterFlush) {
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  // Time-window retention of 100 ns on dst: anything older than newest - 100 evicts.
  dst.setRetentionBudget(dst_id, RetentionBudget{.time_window_ns = 100, .max_memory_bytes = 0});

  src.pushOwned(src_id, 100, makePayload(4));
  src.pushOwned(src_id, 150, makePayload(4));
  src.pushOwned(src_id, 250, makePayload(4));  // newest after flush; evict anything < 150.

  ASSERT_TRUE(src.flushTo(dst).has_value());

  // After flush: newest is 250, threshold = 150, so the entry at 100 evicts.
  EXPECT_EQ(dst.entryCount(dst_id), 2u);
  EXPECT_EQ(dst.at(dst_id, 0)->timestamp, 150);
  EXPECT_EQ(dst.at(dst_id, 1)->timestamp, 250);
}

TEST(ObjectStoreFlushTest, ZeroCopyOwnershipChainSurvives) {
  // A shared_ptr handed in via pushOwned is shared between the entry's payload
  // and any consumer of `at()`. After flushTo, the same shared_ptr instance
  // must be the one observed in the destination — no copy, just refcount
  // transfer through the move of the underlying ObjectEntry.
  ObjectStore src, dst;
  auto src_id = registerSameDescriptor(src);
  auto dst_id = registerSameDescriptor(dst);

  src.pushOwned(src_id, 100, makePayload(16, 0x77));
  auto pre_handle = src.at(src_id, 0);
  ASSERT_TRUE(pre_handle.has_value());
  const auto* pre_ptr = pre_handle->payload.anchor.get();

  ASSERT_TRUE(src.flushTo(dst).has_value());

  auto post_handle = dst.at(dst_id, 0);
  ASSERT_TRUE(post_handle.has_value());
  EXPECT_EQ(post_handle->payload.anchor.get(), pre_ptr) << "shared_ptr identity must survive the flush";
}

}  // namespace
}  // namespace PJ
