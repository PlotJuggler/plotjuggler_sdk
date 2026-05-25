// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/object_store.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
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
  EXPECT_EQ((*r->data)[0], 0x02);
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
  store.pushLazy(id, 100, [&call_count]() -> std::vector<uint8_t> {
    ++call_count;
    return {0xDE, 0xAD};
  });

  EXPECT_EQ(call_count, 0);
  auto r = store.latestAt(id, 100);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(r->data->size(), 2u);
  EXPECT_EQ((*r->data)[0], 0xDE);
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
  EXPECT_EQ((*handle->data)[0], 0xAA);

  store.evictBefore(id, 150);
  EXPECT_EQ(store.entryCount(id), 1u);

  EXPECT_EQ(handle->data->size(), 4u);
  EXPECT_EQ((*handle->data)[0], 0xAA);
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
    store.pushLazy(id, static_cast<Timestamp>(i) * 100, []() { return makePayload(1000); });
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

}  // namespace
}  // namespace PJ
