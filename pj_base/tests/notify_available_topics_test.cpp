// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the per-topic pause/resume ABI additions:
//
//   1. DataSourceRuntimeHostView::notifyAvailableTopics flows each advertised
//      topic (name/encoding/type/schema) through the new runtime-host tail
//      slot notify_available_topics.
//   2. When the host predates the slot (short struct_size / NULL field), the
//      call returns an explicit error rather than degrading silently — so a
//      NEW plugin on an OLD host can detect the absence and fall back.
//   3. The "pj.topic_subscription.v1" plugin extension round-trips a full
//      active-topic set to the plugin, and an unknown extension id yields
//      nullptr (an OLD host on a NEW plugin gets a clean no-op).

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"
#include "pj_base/sdk/data_source_plugin_base.hpp"

namespace {

// One captured advertised topic (strings/bytes copied out of the borrowed views).
struct CapturedTopic {
  std::string topic_name;
  std::string parser_encoding;
  std::string type_name;
  std::vector<uint8_t> schema;
};

// Mock runtime host — captures notify_available_topics calls.
class MockHost {
 public:
  MockHost() {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.notify_available_topics = &MockHost::notifyThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  // Simulate an older host that predates the slot: shrink struct_size to the
  // offset of notify_available_topics AND null the field.
  void dropNotifyAvailableTopics() {
    vtable_.notify_available_topics = nullptr;
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, notify_available_topics);
  }

  PJ::DataSourceRuntimeHostView view() const {
    return PJ::DataSourceRuntimeHostView(host_);
  }

  std::vector<CapturedTopic>& captured() {
    return captured_;
  }
  int callCount() const {
    return call_count_;
  }

 private:
  static bool notifyThunk(void* ctx, const PJ_available_topic_t* topics, uint64_t count, PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<MockHost*>(ctx);
    self->call_count_++;
    self->captured_.clear();
    for (uint64_t i = 0; i < count; ++i) {
      const auto& t = topics[i];
      self->captured_.push_back(
          CapturedTopic{
              std::string(t.topic_name.data, t.topic_name.size),
              std::string(t.parser_encoding.data, t.parser_encoding.size),
              std::string(t.type_name.data, t.type_name.size),
              std::vector<uint8_t>(t.schema.data, t.schema.data + t.schema.size),
          });
    }
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
  std::vector<CapturedTopic> captured_;
  int call_count_ = 0;
};

TEST(NotifyAvailableTopicsTest, AdvertisedTopicsFlowThroughSlot) {
  MockHost host;
  const std::vector<uint8_t> schema_bytes{0xDE, 0xAD, 0xBE, 0xEF};
  const std::vector<PJ::AvailableTopic> topics{
      {"/camera/image", "protobuf", "foxglove.RawImage",
       PJ::Span<const uint8_t>(schema_bytes.data(), schema_bytes.size())},
      {"/odom", "ros2msg", "nav_msgs/msg/Odometry", {}},
  };

  auto status = host.view().notifyAvailableTopics(PJ::Span<const PJ::AvailableTopic>(topics.data(), topics.size()));
  ASSERT_TRUE(status) << (status ? "" : status.error());
  ASSERT_EQ(host.captured().size(), 2U);

  EXPECT_EQ(host.captured()[0].topic_name, "/camera/image");
  EXPECT_EQ(host.captured()[0].parser_encoding, "protobuf");
  EXPECT_EQ(host.captured()[0].type_name, "foxglove.RawImage");
  EXPECT_EQ(host.captured()[0].schema, schema_bytes);

  EXPECT_EQ(host.captured()[1].topic_name, "/odom");
  EXPECT_EQ(host.captured()[1].parser_encoding, "ros2msg");
  EXPECT_TRUE(host.captured()[1].schema.empty());
}

TEST(NotifyAvailableTopicsTest, EmptySetIsValid) {
  MockHost host;
  auto status = host.view().notifyAvailableTopics(PJ::Span<const PJ::AvailableTopic>{});
  EXPECT_TRUE(status);
  EXPECT_EQ(host.callCount(), 1);
  EXPECT_TRUE(host.captured().empty());
}

TEST(NotifyAvailableTopicsTest, ReturnsErrorWhenSlotMissing) {
  MockHost host;
  host.dropNotifyAvailableTopics();
  const std::vector<PJ::AvailableTopic> topics{{"/x", "protobuf", "T", {}}};

  auto status = host.view().notifyAvailableTopics(PJ::Span<const PJ::AvailableTopic>(topics.data(), topics.size()));
  EXPECT_FALSE(status);  // explicit failure so a new plugin can fall back
  EXPECT_EQ(host.callCount(), 0);
}

// ---------- pj.topic_subscription.v1 extension (plugin side) ----------

// A minimal source exposing the per-topic subscription extension. The extension
// records the active-topic set the host pushes in, so the test can assert the
// full set round-tripped with the plugin instance as ctx.
class ExtensionSource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDelegatedIngest | PJ::kCapabilityPerTopicPause;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kIdle;
  }
  const void* pluginExtension(std::string_view id) override {
    if (id == PJ_TOPIC_SUBSCRIPTION_EXTENSION_V1) {
      return &ext_;
    }
    return nullptr;
  }

  std::vector<std::string> active_topics;

 private:
  static bool setActiveTopicsThunk(
      void* ctx, const PJ_string_view_t* names, uint64_t count, PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<ExtensionSource*>(ctx);
    self->active_topics.assign(count, {});
    for (uint64_t i = 0; i < count; ++i) {
      self->active_topics[i].assign(names[i].data, names[i].size);
    }
    return true;
  }

  PJ_topic_subscription_v1_t ext_{sizeof(PJ_topic_subscription_v1_t), &ExtensionSource::setActiveTopicsThunk};
};

TEST(TopicSubscriptionExtensionTest, SetActiveTopicsRoundTrips) {
  ExtensionSource src;
  EXPECT_TRUE(src.capabilities() & PJ::kCapabilityPerTopicPause);

  const auto* ext =
      static_cast<const PJ_topic_subscription_v1_t*>(src.pluginExtension(PJ_TOPIC_SUBSCRIPTION_EXTENSION_V1));
  ASSERT_NE(ext, nullptr);
  ASSERT_GE(ext->struct_size, offsetof(PJ_topic_subscription_v1_t, set_active_topics) + sizeof(ext->set_active_topics));
  ASSERT_NE(ext->set_active_topics, nullptr);

  const PJ_string_view_t names[2] = {{"a", 1}, {"/topic/b", 8}};
  PJ_error_t err{};
  ASSERT_TRUE(ext->set_active_topics(&src, names, 2, &err));
  ASSERT_EQ(src.active_topics.size(), 2U);
  EXPECT_EQ(src.active_topics[0], "a");
  EXPECT_EQ(src.active_topics[1], "/topic/b");

  // Empty set pauses everything.
  ASSERT_TRUE(ext->set_active_topics(&src, nullptr, 0, &err));
  EXPECT_TRUE(src.active_topics.empty());
}

TEST(TopicSubscriptionExtensionTest, UnknownExtensionIdIsNull) {
  ExtensionSource src;
  EXPECT_EQ(src.pluginExtension("pj.nonexistent.v1"), nullptr);
}

// A source WITHOUT the extension (old plugin) — pluginExtension defaults to
// nullptr, so a host querying the id gets a clean no-op signal.
class PlainSource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kIdle;
  }
};

TEST(TopicSubscriptionExtensionTest, PluginWithoutExtensionReturnsNull) {
  PlainSource src;
  EXPECT_FALSE(src.capabilities() & PJ::kCapabilityPerTopicPause);
  EXPECT_EQ(src.pluginExtension(PJ_TOPIC_SUBSCRIPTION_EXTENSION_V1), nullptr);
}

}  // namespace
