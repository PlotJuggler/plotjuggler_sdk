#include "pj_media_core/frame_slot.h"

#include <gtest/gtest.h>

#include <thread>

namespace PJ {
namespace {

CompositeFrame makeFrame(int w, int h, uint8_t fill = 0) {
  CompositeFrame f;
  f.width = w;
  f.height = h;
  f.channels = 3;
  f.pixels.assign(static_cast<size_t>(w * h * 3), fill);
  return f;
}

TEST(FrameSlotTest, TakeWithoutStoreReturnsNullopt) {
  FrameSlot slot;
  EXPECT_FALSE(slot.take().has_value());
}

TEST(FrameSlotTest, StoreAndTake) {
  FrameSlot slot;
  slot.store(1000, makeFrame(320, 240, 0xAA));
  auto r = slot.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->timestamp_ns, 1000);
  EXPECT_EQ(r->frame.width, 320);
  EXPECT_EQ(r->frame.height, 240);
  EXPECT_EQ(r->frame.pixels[0], 0xAA);
}

TEST(FrameSlotTest, TakeConsumesSoSecondTakeIsEmpty) {
  FrameSlot slot;
  slot.store(1000, makeFrame(10, 10));
  ASSERT_TRUE(slot.take().has_value());
  EXPECT_FALSE(slot.take().has_value());
}

TEST(FrameSlotTest, LatestWins) {
  FrameSlot slot;
  slot.store(100, makeFrame(10, 10, 0x01));
  slot.store(200, makeFrame(10, 10, 0x02));
  slot.store(300, makeFrame(10, 10, 0x03));
  auto r = slot.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->timestamp_ns, 300);
  EXPECT_EQ(r->frame.pixels[0], 0x03);
}

TEST(FrameSlotTest, ConcurrentStoreAndTake) {
  FrameSlot slot;
  constexpr int kCount = 10000;

  std::thread writer([&]() {
    for (int i = 0; i < kCount; ++i) {
      slot.store(static_cast<int64_t>(i), makeFrame(4, 4));
    }
  });

  int received = 0;
  std::thread reader([&]() {
    for (int i = 0; i < kCount * 2; ++i) {
      if (slot.take().has_value()) {
        ++received;
      }
    }
  });

  writer.join();
  reader.join();
  EXPECT_GT(received, 0);
}

}  // namespace
}  // namespace PJ
