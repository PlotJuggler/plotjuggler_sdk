// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Lazy-subscription SDK surface:
//   - the "pj.topic_subscription.v1" extension auto-wired by
//     DataSourcePluginBase iff kCapabilityLazySubscription is declared,
//     routing set_active_topics -> onActiveTopicsChanged;
//   - DataSourceRuntimeHostView::setAdvertisedTopics marshalling and its
//     distinct old-host fallback error.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/data_source_topic_subscription.h"
#include "pj_base/sdk/data_source_plugin_base.hpp"

namespace {

constexpr char kManifest[] = R"({"id":"mock-lazy","name":"Mock Lazy","version":"1.0.0"})";

// ---------------------------------------------------------------------------
// Plugin-side: extension exposure + onActiveTopicsChanged routing
// ---------------------------------------------------------------------------

class MockLazySource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDelegatedIngest | PJ::kCapabilityLazySubscription;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kRunning;
  }

  PJ::Status onActiveTopicsChanged(PJ::Span<const std::string_view> active_topics) override {
    if (throw_on_change) {
      throw std::runtime_error("reconcile blew up");
    }
    if (fail_on_change) {
      return PJ::unexpected(std::string("reconcile refused"));
    }
    received.emplace_back(active_topics.begin(), active_topics.end());
    return PJ::okStatus();
  }

  std::vector<std::vector<std::string>> received;
  bool throw_on_change = false;
  bool fail_on_change = false;
};

class MockEagerSource : public MockLazySource {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDelegatedIngest;
  }
};

template <typename Plugin>
const PJ_data_source_vtable_t* vtableFor() {
  return PJ::DataSourcePluginBase::vtableWithCreate(
      []() noexcept -> void* {
        try {
          return new Plugin();
        } catch (...) {
          return nullptr;
        }
      },
      kManifest);
}

const PJ_topic_subscription_extension_t* queryExtension(const PJ_data_source_vtable_t* vt, void* ctx) {
  PJ_string_view_t id{PJ_TOPIC_SUBSCRIPTION_EXTENSION_ID, sizeof(PJ_TOPIC_SUBSCRIPTION_EXTENSION_ID) - 1};
  return static_cast<const PJ_topic_subscription_extension_t*>(vt->get_plugin_extension(ctx, id));
}

TEST(TopicSubscriptionExtensionTest, ExposedIffCapabilityDeclared) {
  const auto* lazy_vt = vtableFor<MockLazySource>();
  void* lazy_ctx = lazy_vt->create();
  ASSERT_NE(lazy_ctx, nullptr);
  const auto* ext = queryExtension(lazy_vt, lazy_ctx);
  ASSERT_NE(ext, nullptr);
  EXPECT_GE(ext->struct_size, sizeof(PJ_topic_subscription_extension_t));
  EXPECT_NE(ext->set_active_topics, nullptr);
  lazy_vt->destroy(lazy_ctx);

  const auto* eager_vt = vtableFor<MockEagerSource>();
  void* eager_ctx = eager_vt->create();
  ASSERT_NE(eager_ctx, nullptr);
  EXPECT_EQ(queryExtension(eager_vt, eager_ctx), nullptr);
  eager_vt->destroy(eager_ctx);
}

TEST(TopicSubscriptionExtensionTest, SetActiveTopicsReachesVirtualWithAllNames) {
  const auto* vt = vtableFor<MockLazySource>();
  void* ctx = vt->create();
  const auto* ext = queryExtension(vt, ctx);
  ASSERT_NE(ext, nullptr);

  const PJ_string_view_t names[] = {
      {"imu/data", 8},
      {"camera/points", 13},
  };
  PJ_error_t err{};
  ASSERT_TRUE(ext->set_active_topics(ctx, names, 2, &err));

  auto* self = static_cast<MockLazySource*>(ctx);
  ASSERT_EQ(self->received.size(), 1u);
  EXPECT_EQ(self->received[0], (std::vector<std::string>{"imu/data", "camera/points"}));

  // Empty set is a legal desired state (all topics unsubscribed).
  ASSERT_TRUE(ext->set_active_topics(ctx, nullptr, 0, &err));
  ASSERT_EQ(self->received.size(), 2u);
  EXPECT_TRUE(self->received[1].empty());

  vt->destroy(ctx);
}

TEST(TopicSubscriptionExtensionTest, ErrorsAndExceptionsBecomeFalsePlusError) {
  const auto* vt = vtableFor<MockLazySource>();
  void* ctx = vt->create();
  const auto* ext = queryExtension(vt, ctx);
  ASSERT_NE(ext, nullptr);
  auto* self = static_cast<MockLazySource*>(ctx);

  const PJ_string_view_t names[] = {{"imu/data", 8}};

  self->fail_on_change = true;
  PJ_error_t err{};
  EXPECT_FALSE(ext->set_active_topics(ctx, names, 1, &err));
  EXPECT_NE(std::string_view(err.message).find("reconcile refused"), std::string_view::npos);

  self->fail_on_change = false;
  self->throw_on_change = true;
  PJ_error_t err2{};
  EXPECT_FALSE(ext->set_active_topics(ctx, names, 1, &err2));
  EXPECT_NE(std::string_view(err2.message).find("reconcile blew up"), std::string_view::npos);

  EXPECT_TRUE(self->received.empty());
  vt->destroy(ctx);
}

// ---------------------------------------------------------------------------
// Host-view side: setAdvertisedTopics marshalling + old-host fallback
// ---------------------------------------------------------------------------

struct CapturedTopic {
  std::string topic_name;
  std::string encoding;
  std::string type_name;
  std::vector<uint8_t> schema;
  std::string config_json;
};

struct AdvertiseCapture {
  std::vector<CapturedTopic> topics;
  int calls = 0;
};

PJ_data_source_runtime_host_vtable_t makeAdvertiseVtable() {
  PJ_data_source_runtime_host_vtable_t vt{};
  vt.protocol_version = 1;
  vt.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
  vt.set_advertised_topics = [](void* ctx, const PJ_parser_binding_request_t* topics, uint64_t count,
                                PJ_error_t*) noexcept -> bool {
    auto* capture = static_cast<AdvertiseCapture*>(ctx);
    capture->calls += 1;
    capture->topics.clear();
    for (uint64_t i = 0; i < count; ++i) {
      const auto& t = topics[i];
      capture->topics.push_back(
          CapturedTopic{
              std::string(t.topic_name.data, t.topic_name.size),
              std::string(t.parser_encoding.data, t.parser_encoding.size),
              std::string(t.type_name.data, t.type_name.size),
              std::vector<uint8_t>(t.schema.data, t.schema.data + t.schema.size),
              std::string(t.parser_config_json.data, t.parser_config_json.size),
          });
    }
    return true;
  };
  return vt;
}

TEST(SetAdvertisedTopicsTest, MarshalsAllRequestFields) {
  AdvertiseCapture capture;
  auto vt = makeAdvertiseVtable();
  PJ::DataSourceRuntimeHostView view(PJ_data_source_runtime_host_t{&capture, &vt});

  const uint8_t schema_bytes[] = {0xAA, 0xBB};
  const PJ::ParserBindingRequest requests[] = {
      {
          .topic_name = "imu/data",
          .parser_encoding = "ros2msg",
          .type_name = "sensor_msgs/msg/Imu",
          .schema = PJ::Span<const uint8_t>(schema_bytes, sizeof(schema_bytes)),
          .parser_config_json = R"({"a":1})",
      },
      {
          .topic_name = "camera/points",
          .parser_encoding = "cdr",
          .type_name = "sensor_msgs/msg/PointCloud2",
          .schema = {},
          .parser_config_json = "",
      },
  };

  auto status = view.setAdvertisedTopics(PJ::Span<const PJ::ParserBindingRequest>(requests, 2));
  ASSERT_TRUE(status) << status.error();
  ASSERT_EQ(capture.calls, 1);
  ASSERT_EQ(capture.topics.size(), 2u);
  EXPECT_EQ(capture.topics[0].topic_name, "imu/data");
  EXPECT_EQ(capture.topics[0].encoding, "ros2msg");
  EXPECT_EQ(capture.topics[0].type_name, "sensor_msgs/msg/Imu");
  EXPECT_EQ(capture.topics[0].schema, (std::vector<uint8_t>{0xAA, 0xBB}));
  EXPECT_EQ(capture.topics[0].config_json, R"({"a":1})");
  EXPECT_EQ(capture.topics[1].topic_name, "camera/points");
  EXPECT_TRUE(capture.topics[1].schema.empty());

  // Empty advertise (source has no topics yet) is valid.
  auto empty_status = view.setAdvertisedTopics({});
  ASSERT_TRUE(empty_status) << empty_status.error();
  EXPECT_EQ(capture.calls, 2);
  EXPECT_TRUE(capture.topics.empty());
}

TEST(SetAdvertisedTopicsTest, OldHostYieldsDistinctFallbackError) {
  AdvertiseCapture capture;
  auto vt = makeAdvertiseVtable();
  // A host built before the tail slot reports a smaller struct_size.
  vt.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, set_advertised_topics);
  PJ::DataSourceRuntimeHostView view(PJ_data_source_runtime_host_t{&capture, &vt});

  auto status = view.setAdvertisedTopics({});
  ASSERT_FALSE(status);
  EXPECT_EQ(status.error(), "host does not support lazy subscription");
  EXPECT_EQ(capture.calls, 0);

  PJ::DataSourceRuntimeHostView unbound;
  auto unbound_status = unbound.setAdvertisedTopics({});
  ASSERT_FALSE(unbound_status);
  EXPECT_EQ(unbound_status.error(), "runtime host is not bound");
}

}  // namespace
