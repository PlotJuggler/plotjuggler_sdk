// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {
namespace {

// Fake host for pj.scene_views.v1: a small model of views-with-topics, not a bare
// recorder, so the round-trip tests below can assert on what a read-back
// actually contains rather than merely that a call was forwarded.
struct FakeSceneViewHost {
  struct Topic {
    std::string topic;
    std::string dataset;
  };
  struct View {
    std::string id;
    std::string kind;
    std::string title;
    std::vector<Topic> topics;
  };

  std::vector<View> views;
  bool should_fail = false;
  std::string last_config_json;  // storage backing the borrowed view_config out-string
  int focus_calls = 0;
  std::string last_focused_id;

  View* find(std::string_view id) {
    auto it = std::find_if(views.begin(), views.end(), [&](const View& v) { return v.id == id; });
    return it == views.end() ? nullptr : &(*it);
  }
};

bool svFail(FakeSceneViewHost* self, PJ_error_t* out_error) noexcept {
  if (self->should_fail) {
    sdk::fillError(out_error, 1, "scene_views", "view boom");
    return true;
  }
  return false;
}

bool svCreateView(
    void* ctx, PJ_string_view_t id, PJ_string_view_t kind, PJ_string_view_t title, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  const auto kind_sv = sdk::toStringView(kind);
  auto* existing = self->find(id_sv);
  if (existing != nullptr) {
    if (existing->kind != kind_sv) {
      existing->kind = std::string(kind_sv);
      existing->topics.clear();
    }
    existing->title = std::string(sdk::toStringView(title));
    return true;
  }
  self->views.push_back(
      FakeSceneViewHost::View{
          .id = std::string(id_sv),
          .kind = std::string(kind_sv),
          .title = std::string(sdk::toStringView(title)),
          .topics = {}});
  return true;
}

bool svCloseView(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  auto it = std::find_if(self->views.begin(), self->views.end(), [&](const auto& v) { return v.id == id_sv; });
  if (it == self->views.end()) {
    sdk::fillError(out_error, 2, "scene_views", "unknown view id");
    return false;
  }
  self->views.erase(it);
  return true;
}

bool svListViewIds(
    void* ctx, PJ_string_view_t* out_ids, uint64_t capacity, uint64_t* out_count, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  const auto total = static_cast<uint64_t>(self->views.size());
  if (capacity == 0) {
    *out_count = total;
    return true;
  }
  const uint64_t filled = std::min(capacity, total);
  for (uint64_t i = 0; i < filled; ++i) {
    out_ids[i] = sdk::toAbiString(self->views[i].id);
  }
  *out_count = total;
  return true;
}

bool svViewConfig(void* ctx, PJ_string_view_t id, PJ_string_view_t* out_config_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  auto* view = self->find(sdk::toStringView(id));
  if (view == nullptr) {
    sdk::fillError(out_error, 2, "scene_views", "unknown view id");
    return false;
  }
  std::string json = "{\"kind\":\"" + view->kind + "\",\"title\":\"" + view->title + "\",\"topics\":[";
  for (size_t i = 0; i < view->topics.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    const auto& topic = view->topics[i];
    json += "{\"topic\":\"" + topic.topic + "\",\"dataset\":\"" + topic.dataset +
            "\",\"type\":\"kPointCloud\",\"visible\":true}";
  }
  json += "]}";
  self->last_config_json = std::move(json);
  *out_config_json = sdk::toAbiString(self->last_config_json);
  return true;
}

bool svAttachTopic(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  auto* view = self->find(sdk::toStringView(id));
  if (view == nullptr) {
    sdk::fillError(out_error, 2, "scene_views", "unknown view id");
    return false;
  }
  const auto topic_sv = sdk::toStringView(topic);
  const auto dataset_sv = sdk::toStringView(dataset_source);
  auto it = std::find_if(view->topics.begin(), view->topics.end(), [&](const auto& t) {
    return t.topic == topic_sv && t.dataset == dataset_sv;
  });
  if (it != view->topics.end()) {
    return true;  // already attached: success
  }
  view->topics.push_back(FakeSceneViewHost::Topic{.topic = std::string(topic_sv), .dataset = std::string(dataset_sv)});
  return true;
}

bool svDetachTopic(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  auto* view = self->find(sdk::toStringView(id));
  if (view == nullptr) {
    sdk::fillError(out_error, 2, "scene_views", "unknown view id");
    return false;
  }
  const auto topic_sv = sdk::toStringView(topic);
  const auto dataset_sv = sdk::toStringView(dataset_source);
  auto it = std::find_if(view->topics.begin(), view->topics.end(), [&](const auto& t) {
    return t.topic == topic_sv && t.dataset == dataset_sv;
  });
  if (it == view->topics.end()) {
    sdk::fillError(out_error, 3, "scene_views", "topic not present");
    return false;
  }
  view->topics.erase(it);
  return true;
}

bool svFocusView(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeSceneViewHost*>(ctx);
  if (svFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  auto* view = self->find(id_sv);
  if (view == nullptr) {
    sdk::fillError(out_error, 2, "scene_views", "unknown view id");
    return false;
  }
  ++self->focus_calls;
  self->last_focused_id = std::string(id_sv);
  return true;
}

PJ_scene_view_host_vtable_t makeSceneViewVtable() {
  return PJ_scene_view_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_scene_view_host_vtable_t),
      .create_view = svCreateView,
      .close_view = svCloseView,
      .list_view_ids = svListViewIds,
      .view_config = svViewConfig,
      .attach_topic = svAttachTopic,
      .detach_topic = svDetachTopic,
      .focus_view = svFocusView,
  };
}

// --- SceneViewHostView -------------------------------------------------------

TEST(SceneViewsApiTest, CreateAndListRoundTrip) {
  FakeSceneViewHost host;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createView("view-a", "3d", "First"));
  ASSERT_TRUE(view.createView("view-b", "2d", "Second"));

  auto ids = view.listViews();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 2u);
  EXPECT_EQ((*ids)[0], "view-a");
  EXPECT_EQ((*ids)[1], "view-b");
}

TEST(SceneViewsApiTest, ConfigReadsBackWhatWasAttached) {
  FakeSceneViewHost host;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createView("view-a", "3d", "My View"));
  ASSERT_TRUE(view.attachTopic("view-a", "lidar/points", "bag1"));
  ASSERT_TRUE(view.attachTopic("view-a", "camera/image", "bag1"));

  auto config = view.configOf("view-a");
  ASSERT_TRUE(config) << config.error();
  EXPECT_NE(config->find("My View"), std::string::npos);
  EXPECT_NE(config->find("lidar/points"), std::string::npos);
  EXPECT_NE(config->find("camera/image"), std::string::npos);

  const auto grown_size = config->size();
  ASSERT_TRUE(view.attachTopic("view-a", "imu/data", "bag1"));
  auto config2 = view.configOf("view-a");
  ASSERT_TRUE(config2) << config2.error();
  EXPECT_NE(config2->find("imu/data"), std::string::npos);
  EXPECT_GT(config2->size(), grown_size);
}

TEST(SceneViewsApiTest, DetachMissingTopicIsAnError) {
  FakeSceneViewHost host;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createView("view-a", "3d"));
  auto status = view.detachTopic("view-a", "lidar/points", "bag1");
  EXPECT_FALSE(status);
}

TEST(SceneViewsApiTest, UnknownIdIsAnError) {
  FakeSceneViewHost host;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  EXPECT_FALSE(view.attachTopic("nope", "lidar/points"));
  EXPECT_FALSE(view.configOf("nope"));
  EXPECT_FALSE(view.closeView("nope"));
  EXPECT_FALSE(view.focusView("nope"));
}

TEST(SceneViewsApiTest, HostFailureSurfacesTheMessage) {
  FakeSceneViewHost host;
  host.should_fail = true;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  auto create_status = view.createView("view-a", "3d");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("view boom"), std::string::npos);

  auto attach_status = view.attachTopic("view-a", "lidar/points");
  EXPECT_FALSE(attach_status);
  EXPECT_NE(attach_status.error().find("view boom"), std::string::npos);

  auto list_status = view.listViews();
  EXPECT_FALSE(list_status);
  EXPECT_NE(list_status.error().find("view boom"), std::string::npos);
}

TEST(SceneViewsApiTest, UnboundViewReportsNotBound) {
  sdk::SceneViewHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  auto create_status = view.createView("view-a", "3d");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("not bound"), std::string::npos);

  auto close_status = view.closeView("view-a");
  EXPECT_FALSE(close_status);
  EXPECT_NE(close_status.error().find("not bound"), std::string::npos);

  auto list_status = view.listViews();
  EXPECT_FALSE(list_status);
  EXPECT_NE(list_status.error().find("not bound"), std::string::npos);

  auto config_status = view.configOf("view-a");
  EXPECT_FALSE(config_status);
  EXPECT_NE(config_status.error().find("not bound"), std::string::npos);

  auto attach_status = view.attachTopic("view-a", "lidar/points");
  EXPECT_FALSE(attach_status);
  EXPECT_NE(attach_status.error().find("not bound"), std::string::npos);

  auto detach_status = view.detachTopic("view-a", "lidar/points");
  EXPECT_FALSE(detach_status);
  EXPECT_NE(detach_status.error().find("not bound"), std::string::npos);

  auto focus_status = view.focusView("view-a");
  EXPECT_FALSE(focus_status);
  EXPECT_NE(focus_status.error().find("not bound"), std::string::npos);
}

TEST(SceneViewsApiTest, DatasetQualifierReachesTheHost) {
  FakeSceneViewHost host;
  const auto vtable = makeSceneViewVtable();
  sdk::SceneViewHostView view(PJ_scene_view_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createView("view-a", "3d"));
  ASSERT_TRUE(view.attachTopic("view-a", "lidar/points", "bag1"));
  ASSERT_TRUE(view.attachTopic("view-a", "camera/image"));

  auto* found = host.find("view-a");
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->topics.size(), 2u);
  EXPECT_EQ(found->topics[0].dataset, "bag1");
  EXPECT_TRUE(found->topics[1].dataset.empty());
}

}  // namespace
}  // namespace PJ
