// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Host-side DataSourceHandle wrapper over the "pj.topic_subscription.v1"
// extension: topicSubscriptionExtension() gating and setActiveTopics()
// marshalling/error-path, exercised through the real handle (not the raw
// vtable, which pj_base/tests/topic_subscription_extension_test.cpp already
// covers at the SDK level).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "pj_base/sdk/data_source_plugin_base.hpp"
#include "pj_plugins/host/data_source_handle.hpp"

namespace {

constexpr char kManifest[] = R"({"id":"h","name":"h","version":"1.0.0"})";

class LazySource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityLazySubscription;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kRunning;
  }

  PJ::Status onActiveTopicsChanged(PJ::Span<const std::string_view> active_topics) override {
    received.emplace_back(active_topics.begin(), active_topics.end());
    return PJ::okStatus();
  }

  std::vector<std::vector<std::string>> received;
};

class EagerSource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream;
  }
  PJ::Status start() override {
    return PJ::okStatus();
  }
  void stop() override {}
  PJ::DataSourceState currentState() const override {
    return PJ::DataSourceState::kRunning;
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

TEST(DataSourceHandleSubscriptionTest, LazyHandleExposesExtensionAndForwardsTopics) {
  PJ::DataSourceHandle handle(vtableFor<LazySource>());
  ASSERT_TRUE(handle.valid());
  ASSERT_NE(handle.topicSubscriptionExtension(), nullptr);

  const std::vector<std::string> names{"imu/data", "camera/points"};
  auto status = handle.setActiveTopics(PJ::Span<const std::string>(names));
  ASSERT_TRUE(status) << status.error();

  auto* plugin = static_cast<LazySource*>(handle.context());
  ASSERT_EQ(plugin->received.size(), 1u);
  EXPECT_EQ(plugin->received[0], (std::vector<std::string>{"imu/data", "camera/points"}));

  auto empty_status = handle.setActiveTopics({});
  ASSERT_TRUE(empty_status) << empty_status.error();
  ASSERT_EQ(plugin->received.size(), 2u);
  EXPECT_TRUE(plugin->received[1].empty());
}

TEST(DataSourceHandleSubscriptionTest, EagerHandleHasNoExtensionAndRejectsSetActiveTopics) {
  PJ::DataSourceHandle handle(vtableFor<EagerSource>());
  ASSERT_TRUE(handle.valid());
  EXPECT_EQ(handle.topicSubscriptionExtension(), nullptr);

  const std::vector<std::string> names{"imu/data"};
  auto status = handle.setActiveTopics(PJ::Span<const std::string>(names));
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("does not support topic subscription"), std::string::npos);
}

}  // namespace
