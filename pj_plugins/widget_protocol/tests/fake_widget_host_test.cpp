// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <pj_plugins/sdk/testing/fake_widget_host.hpp>
#include <string>
#include <vector>

namespace {

using PJ::sdk::testing::FakeWidgetHost;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Read {
  bool ok = false;
  uint32_t total = 0;
  std::vector<double> times;
  std::string error;
};

/// One query_history call through the C vtable, as a plugin makes it.
Read query(
    FakeWidgetHost& fake, double from, double to, uint32_t max_samples = 0, size_t stop_after = SIZE_MAX,
    uint32_t input = 0, uint32_t slot = 0) {
  struct Sink {
    std::vector<double>* times;
    size_t stop_after;
  };
  Read out;
  Sink sink{&out.times, stop_after};
  const PJ_widget_host_t host = fake.makeHost();
  PJ_error_t error{};
  out.ok = host.vtable->query_history(
      host.ctx, input, slot, from, to, max_samples,
      [](void* user, const PJ_widget_value_t* value) noexcept {
        auto* s = static_cast<Sink*>(user);
        s->times->push_back(value->time);
        return s->times->size() < s->stop_after;
      },
      &sink, &out.total, &error);
  if (!out.ok) {
    out.error = error.message;
  }
  return out;
}

void addAt(FakeWidgetHost& fake, double seconds) {
  fake.insert(0, 0, FakeWidgetHost::Sample{.store_time = fake.storeTime(seconds), .bytes = {1}});
}

TEST(FakeWidgetHost, RangeIsInclusiveInDisplayTime) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic").type_name = "kLog";
  for (double t : {1.0, 2.0, 3.0}) {
    addAt(fake, t);
  }
  EXPECT_EQ(query(fake, 1.0, 3.0).times, (std::vector<double>{1.0, 2.0, 3.0}));
  const Read one = query(fake, 2.0, 2.0);
  EXPECT_EQ(one.total, 1u);
  EXPECT_EQ(one.times, (std::vector<double>{2.0}));
  EXPECT_EQ(query(fake, 3.5, kInf).total, 0u);
}

// The property a widget that follows a growing history depends on: a time the host delivered, asked
// for again, still holds that sample. At epoch times a double steps ~238 ns, so it must hold at the
// display-time level, not the nanosecond level.
TEST(FakeWidgetHost, DeliveredTimesQueryBackAtEpochScale) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/rosout");
  constexpr nanoseconds kBase{1'731'500'000'123'456'789};
  constexpr int kCount = 2'000;
  for (int i = 0; i < kCount; ++i) {
    fake.insert(0, 0, FakeWidgetHost::Sample{.store_time = kBase + i * 7'919ns});
  }
  const Read all = query(fake, -kInf, kInf, UINT32_MAX);
  ASSERT_EQ(all.times.size(), static_cast<size_t>(kCount));
  for (size_t i = 0; i < all.times.size(); ++i) {
    const Read from_here = query(fake, all.times[i], kInf, UINT32_MAX);
    ASSERT_FALSE(from_here.times.empty());
    EXPECT_EQ(from_here.times.front(), all.times[i]);
    EXPECT_GE(from_here.total, static_cast<uint32_t>(kCount - static_cast<int>(i)));
  }
}

TEST(FakeWidgetHost, NearbyNanosecondsShareADisplayTimeFarFromTheOrigin) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/rosout");
  constexpr nanoseconds kBase{1'731'500'000'000'000'000};
  fake.insert(0, 0, FakeWidgetHost::Sample{.store_time = kBase});
  fake.insert(0, 0, FakeWidgetHost::Sample{.store_time = kBase + 50ns});
  const Read all = query(fake, -kInf, kInf);
  ASSERT_EQ(all.times.size(), 2u);
  EXPECT_EQ(all.times[0], all.times[1]);
  const Read at = query(fake, all.times[0], all.times[0]);
  EXPECT_EQ(at.total, 2u);
  EXPECT_EQ(at.times.size(), 2u);  // both delivered, not only counted
}

TEST(FakeWidgetHost, DisplayOffsetShiftsTimes) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic");
  addAt(fake, 1.0);
  fake.display_offset -= 10s;
  EXPECT_EQ(query(fake, -kInf, kInf).times, (std::vector<double>{11.0}));
}

TEST(FakeWidgetHost, CapsAndDecimationKeepTheEnds) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic");
  for (int i = 0; i < 10; ++i) {
    addAt(fake, i);
  }
  const Read capped = query(fake, -kInf, kInf, 4);
  EXPECT_EQ(capped.total, 10u);
  ASSERT_EQ(capped.times.size(), 4u);
  EXPECT_EQ(capped.times.front(), 0.0);
  EXPECT_EQ(capped.times.back(), 9.0);

  fake.default_cap = 3;
  EXPECT_EQ(query(fake, -kInf, kInf, 0).times.size(), 3u);
  fake.hard_cap = 5;
  EXPECT_EQ(query(fake, -kInf, kInf, 8).times.size(), 5u);
}

TEST(FakeWidgetHost, UndeliverableSamplesAreCountedNotDelivered) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic");
  fake.insert(0, 0, FakeWidgetHost::Sample{.store_time = fake.storeTime(1.0), .undeliverable = true});
  addAt(fake, 2.0);
  const Read read = query(fake, -kInf, kInf, 1);
  EXPECT_EQ(read.total, 2u);
  EXPECT_TRUE(read.times.empty());  // the one pick was the undeliverable first sample
  EXPECT_EQ(query(fake, -kInf, kInf).times, (std::vector<double>{2.0}));
  // Nor is it the wire's value at the tracker.
  EXPECT_EQ(fake.values(1.5)[0].kind, PJ_WIDGET_VALUE_NONE);
}

TEST(FakeWidgetHost, SinkCanStopEarly) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic");
  for (double t : {1.0, 2.0, 3.0}) {
    addAt(fake, t);
  }
  const Read read = query(fake, -kInf, kInf, 0, 1);
  EXPECT_TRUE(read.ok);
  EXPECT_EQ(read.total, 3u);
  EXPECT_EQ(read.times.size(), 1u);
}

TEST(FakeWidgetHost, FailuresDeliverNothing) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/topic");
  addAt(fake, 1.0);
  const Read unwired = query(fake, -kInf, kInf, 0, SIZE_MAX, 0, 7);
  EXPECT_FALSE(unwired.ok);
  EXPECT_TRUE(unwired.times.empty());

  fake.fail_with = "not resolved yet";
  const Read failed = query(fake, -kInf, kInf);
  EXPECT_FALSE(failed.ok);
  EXPECT_EQ(failed.total, 0u);
  EXPECT_TRUE(failed.times.empty());
  EXPECT_NE(failed.error.find("not resolved yet"), std::string::npos);
  EXPECT_EQ(fake.queries.size(), 2u);
}

TEST(FakeWidgetHost, ValuesCarryTheLatestSampleAtTheTracker) {
  FakeWidgetHost fake;
  fake.wire(0, 0, "/a").type_name = "kLog";
  fake.wire(0, 1, "/b");
  addAt(fake, 1.0);
  addAt(fake, 2.0);
  fake.insert(
      0, 1, FakeWidgetHost::Sample{.store_time = fake.storeTime(5.0), .kind = PJ_WIDGET_VALUE_NUMBER, .number = 4.5});

  const auto at = fake.values(1.5);
  ASSERT_EQ(at.size(), 2u);
  EXPECT_EQ(at[0].kind, PJ_WIDGET_VALUE_OBJECT);
  EXPECT_EQ(at[0].time, 1.0);
  EXPECT_EQ(std::string(at[0].type_name.data, at[0].type_name.size), "kLog");
  EXPECT_EQ(at[1].kind, PJ_WIDGET_VALUE_NONE);  // nothing at or before 1.5 on /b
  EXPECT_EQ(std::string(at[1].label.data, at[1].label.size), "/b");

  const auto later = fake.values(5.0);
  EXPECT_EQ(later[0].time, 2.0);
  EXPECT_EQ(later[1].kind, PJ_WIDGET_VALUE_NUMBER);
  EXPECT_EQ(later[1].number, 4.5);
}

TEST(FakeWidgetHost, RecordsMessagesAndInputsChanged) {
  FakeWidgetHost fake;
  const PJ_widget_host_t host = fake.makeHost();
  host.vtable->report_widget_message(host.ctx, PJ_WIDGET_MESSAGE_WARNING, PJ_string_view_t{"careful", 7});
  host.vtable->report_widget_message(host.ctx, PJ_WIDGET_MESSAGE_INFO, PJ_string_view_t{"fyi", 3});
  host.vtable->notify_inputs_changed(host.ctx);
  EXPECT_EQ(fake.messagesAt(PJ::WidgetMessageLevel::kWarning), (std::vector<std::string>{"careful"}));
  EXPECT_EQ(fake.messages.size(), 2u);
  EXPECT_EQ(fake.inputs_changed_calls, 1);
}

}  // namespace
