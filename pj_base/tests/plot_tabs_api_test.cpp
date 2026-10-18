// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/detail/json.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {
namespace {

// Fake host for pj.plot_tabs.v1 (v1 slots and scene tail slots): a small model of
// tabs-with-curves-or-topics, not a bare recorder, so the round-trip tests below can assert on what a read-back
// actually contains rather than merely that a call was forwarded.
struct FakePlotTabHost {
  struct Curve {
    std::string topic;
    std::string field;
    std::string dataset;
  };
  struct Topic {
    std::string topic;
    std::string dataset;
  };
  struct Tab {
    std::string id;
    std::string kind = "plot";
    std::string title;
    std::vector<Curve> curves;
    std::vector<Topic> topics;
  };

  std::vector<Tab> tabs;
  bool should_fail = false;
  std::string last_config_json;  // storage backing the borrowed tab_config out-string
  int focus_calls = 0;
  std::string last_focused_id;
  std::string unservable_kind;       // a kind this fake knows but cannot serve
  std::map<std::string, int> calls;  // tail-slot entries, counted before any validation

  Tab* find(std::string_view id) {
    auto it = std::find_if(tabs.begin(), tabs.end(), [&](const Tab& t) { return t.id == id; });
    return it == tabs.end() ? nullptr : &(*it);
  }
};

bool ptFail(FakePlotTabHost* self, PJ_error_t* out_error) noexcept {
  if (self->should_fail) {
    sdk::fillError(out_error, 1, "plot_tabs", "tab boom");
    return true;
  }
  return false;
}

// Same id and kind: a plot tab is replaced by one empty plot, a scene tab only gets
// its title updated. A different kind resets the tab to empty.
bool createOfKind(
    FakePlotTabHost* self, PJ_string_view_t id, std::string_view kind, PJ_string_view_t title, PJ_error_t* out_error) {
  if (ptFail(self, out_error)) {
    return false;
  }
  if (!self->unservable_kind.empty() && kind == self->unservable_kind) {
    sdk::fillError(out_error, 4, "plot_tabs", "tab kind '" + std::string(kind) + "' is not available on this host");
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  const std::string title_str(sdk::toStringView(title));
  auto* existing = self->find(id_sv);
  if (existing == nullptr) {
    self->tabs.push_back(FakePlotTabHost::Tab{.id = std::string(id_sv), .kind = std::string(kind), .title = title_str});
    return true;
  }
  if (existing->kind != kind || kind == "plot") {
    existing->kind = std::string(kind);
    existing->curves.clear();
    existing->topics.clear();
  }
  existing->title = title_str;
  return true;
}

bool ptCreateTab(void* ctx, PJ_string_view_t id, PJ_string_view_t title, PJ_error_t* out_error) noexcept {
  return createOfKind(static_cast<FakePlotTabHost*>(ctx), id, "plot", title, out_error);
}

bool ptCreateTabV2(
    void* ctx, PJ_string_view_t id, PJ_string_view_t kind, PJ_string_view_t title, PJ_error_t* out_error) noexcept {
  ++static_cast<FakePlotTabHost*>(ctx)->calls["create_tab_v2"];
  return createOfKind(static_cast<FakePlotTabHost*>(ctx), id, sdk::toStringView(kind), title, out_error);
}

bool ptCloseTab(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  auto it = std::find_if(self->tabs.begin(), self->tabs.end(), [&](const auto& t) { return t.id == id_sv; });
  if (it == self->tabs.end()) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  self->tabs.erase(it);
  return true;
}

bool ptListTabIds(
    void* ctx, PJ_string_view_t* out_ids, uint64_t capacity, uint64_t* out_count, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  const auto total = static_cast<uint64_t>(self->tabs.size());
  if (capacity == 0) {
    *out_count = total;
    return true;
  }
  const uint64_t filled = std::min(capacity, total);
  for (uint64_t i = 0; i < filled; ++i) {
    out_ids[i] = sdk::toAbiString(self->tabs[i].id);
  }
  *out_count = total;
  return true;
}

bool ptTabConfig(void* ctx, PJ_string_view_t id, PJ_string_view_t* out_config_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  std::string json;
  auto field = [&json](std::string_view key, std::string_view value) {
    sdk::detail::appendJsonString(json, key);
    json += ':';
    sdk::detail::appendJsonString(json, value);
  };
  if (tab->kind == "plot") {
    json = "{";
    field("title", tab->title);
    json += ",\"curves\":[";
    for (size_t i = 0; i < tab->curves.size(); ++i) {
      json += i == 0 ? "{" : ",{";
      field("topic", tab->curves[i].topic);
      json += ',';
      field("field", tab->curves[i].field);
      json += ',';
      field("dataset", tab->curves[i].dataset);
      json += '}';
    }
    json += "]}";
  } else {
    json = "{";
    field("kind", tab->kind);
    json += ',';
    field("title", tab->title);
    json += ",\"topics\":[";
    for (size_t i = 0; i < tab->topics.size(); ++i) {
      json += i == 0 ? "{" : ",{";
      field("topic", tab->topics[i].topic);
      json += ',';
      field("dataset", tab->topics[i].dataset);
      json += ",\"type\":\"kPointCloud\",\"visible\":true}";
    }
    json += "]}";
  }
  self->last_config_json = std::move(json);
  *out_config_json = sdk::toAbiString(self->last_config_json);
  return true;
}

bool ptAddCurve(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t field, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  if (tab->kind != "plot") {
    sdk::fillError(
        out_error, 4, "plot_tabs",
        "tab '" + tab->id + "' is a " + tab->kind + " scene tab: use attach_topic/detach_topic");
    return false;
  }
  tab->curves.push_back(
      FakePlotTabHost::Curve{
          .topic = std::string(sdk::toStringView(topic)),
          .field = std::string(sdk::toStringView(field)),
          .dataset = std::string(sdk::toStringView(dataset_source))});
  return true;
}

bool ptRemoveCurve(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t field, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  const auto topic_sv = sdk::toStringView(topic);
  const auto field_sv = sdk::toStringView(field);
  const auto dataset_sv = sdk::toStringView(dataset_source);
  auto it = std::find_if(tab->curves.begin(), tab->curves.end(), [&](const auto& c) {
    return c.topic == topic_sv && c.field == field_sv && c.dataset == dataset_sv;
  });
  if (it == tab->curves.end()) {
    sdk::fillError(out_error, 3, "plot_tabs", "curve not present");
    return false;
  }
  tab->curves.erase(it);
  return true;
}

bool ptClearTab(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  tab->curves.clear();
  tab->topics.clear();
  return true;
}

bool ptAttachTopic(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  ++self->calls["attach_topic"];
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  const auto topic_sv = sdk::toStringView(topic);
  const auto dataset_sv = sdk::toStringView(dataset_source);
  auto it = std::find_if(tab->topics.begin(), tab->topics.end(), [&](const auto& t) {
    return t.topic == topic_sv && t.dataset == dataset_sv;
  });
  if (it != tab->topics.end()) {
    return true;  // already attached: success
  }
  tab->topics.push_back(FakePlotTabHost::Topic{.topic = std::string(topic_sv), .dataset = std::string(dataset_sv)});
  return true;
}

bool ptDetachTopic(
    void* ctx, PJ_string_view_t id, PJ_string_view_t topic, PJ_string_view_t dataset_source,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  ++self->calls["detach_topic"];
  if (ptFail(self, out_error)) {
    return false;
  }
  auto* tab = self->find(sdk::toStringView(id));
  if (tab == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  const auto topic_sv = sdk::toStringView(topic);
  const auto dataset_sv = sdk::toStringView(dataset_source);
  auto it = std::find_if(tab->topics.begin(), tab->topics.end(), [&](const auto& t) {
    return t.topic == topic_sv && t.dataset == dataset_sv;
  });
  if (it == tab->topics.end()) {
    sdk::fillError(out_error, 3, "plot_tabs", "topic not present");
    return false;
  }
  tab->topics.erase(it);
  return true;
}

bool ptFocusTab(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  ++self->calls["focus_tab"];
  if (ptFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  if (self->find(id_sv) == nullptr) {
    sdk::fillError(out_error, 2, "plot_tabs", "unknown tab id");
    return false;
  }
  ++self->focus_calls;
  self->last_focused_id = std::string(id_sv);
  return true;
}

// Full vtable (v1 slots + the four tail slots). Tests that need a released v1 host
// shrink struct_size to PJ_PLOT_TAB_HOST_MIN_VTABLE_SIZE or null a tail slot.
PJ_plot_tab_host_vtable_t makePlotTabVtable() {
  return PJ_plot_tab_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_plot_tab_host_vtable_t),
      .create_tab = ptCreateTab,
      .close_tab = ptCloseTab,
      .list_tab_ids = ptListTabIds,
      .tab_config = ptTabConfig,
      .add_curve = ptAddCurve,
      .remove_curve = ptRemoveCurve,
      .clear_tab = ptClearTab,
      .create_tab_v2 = ptCreateTabV2,
      .attach_topic = ptAttachTopic,
      .detach_topic = ptDetachTopic,
      .focus_tab = ptFocusTab,
  };
}

// --- PlotTabHostView --------------------------------------------------------

TEST(PlotTabApiTest, CreateAndListRoundTrip) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a", "First"));
  ASSERT_TRUE(view.create("tab-b", "Second"));

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 2u);
  EXPECT_EQ((*ids)[0], "tab-a");
  EXPECT_EQ((*ids)[1], "tab-b");
}

TEST(PlotTabApiTest, ListOnAnEmptyHostSucceedsWithNoTabs) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  EXPECT_TRUE(ids->empty());
}

TEST(PlotTabApiTest, ListGrowthReturnsAnErrorWithoutReadingPastCapacity) {
  int calls = 0;
  auto vtable = makePlotTabVtable();
  vtable.list_tab_ids = [](void* ctx, PJ_string_view_t* out_ids, uint64_t capacity, uint64_t* out_count,
                           PJ_error_t*) noexcept {
    ++*static_cast<int*>(ctx);
    *out_count = capacity == 0 ? 1 : 2;
    if (capacity != 0) {
      out_ids[0] = sdk::toAbiString("tab-a");
    }
    return true;
  };
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &calls, .vtable = &vtable});

  auto ids = view.list();
  ASSERT_FALSE(ids);
  EXPECT_NE(ids.error().find("retry"), std::string::npos);
  EXPECT_EQ(calls, 2);
}

TEST(PlotTabApiTest, ConfigReadsBackWhatWasActuallyAdded) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a", "My Tab"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu/accel", "x", "bag1"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu/gyro", "y", "bag1"));

  auto config = view.configOf("tab-a");
  ASSERT_TRUE(config) << config.error();
  EXPECT_NE(config->find("My Tab"), std::string::npos);
  EXPECT_NE(config->find("imu/accel"), std::string::npos);
  EXPECT_NE(config->find("imu/gyro"), std::string::npos);

  const auto grown_size = config->size();
  ASSERT_TRUE(view.addCurve("tab-a", "imu/mag", "z", "bag1"));
  auto config2 = view.configOf("tab-a");
  ASSERT_TRUE(config2) << config2.error();
  EXPECT_NE(config2->find("imu/mag"), std::string::npos);
  EXPECT_GT(config2->size(), grown_size);
}

TEST(PlotTabApiTest, RemoveCurveThatIsNotThereIsAnError) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a"));
  auto status = view.removeCurve("tab-a", "imu/accel", "x", "bag1");
  EXPECT_FALSE(status);
}

TEST(PlotTabApiTest, ClearKeepsTheTab) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a", "My Tab"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu/accel", "x", "bag1"));
  ASSERT_TRUE(view.clear("tab-a"));

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 1u);
  EXPECT_EQ((*ids)[0], "tab-a");

  auto config = view.configOf("tab-a");
  ASSERT_TRUE(config) << config.error();
  EXPECT_EQ(config->find("imu/accel"), std::string::npos);
}

TEST(PlotTabApiTest, CloseRemovesTheTab) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a"));
  ASSERT_TRUE(view.close("tab-a"));

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  EXPECT_TRUE(ids->empty());

  auto config = view.configOf("tab-a");
  EXPECT_FALSE(config);
}

TEST(PlotTabApiTest, UnknownIdIsAnError) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  EXPECT_FALSE(view.addCurve("nope", "imu/accel", "x"));
  EXPECT_FALSE(view.configOf("nope"));
  EXPECT_FALSE(view.close("nope"));
}

TEST(PlotTabApiTest, HostFailureSurfacesTheMessage) {
  FakePlotTabHost host;
  host.should_fail = true;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  auto create_status = view.create("tab-a");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("tab boom"), std::string::npos);

  auto add_status = view.addCurve("tab-a", "imu/accel", "x");
  EXPECT_FALSE(add_status);
  EXPECT_NE(add_status.error().find("tab boom"), std::string::npos);

  auto list_status = view.list();
  EXPECT_FALSE(list_status);
  EXPECT_NE(list_status.error().find("tab boom"), std::string::npos);
}

TEST(PlotTabApiTest, UnboundViewReportsNotBound) {
  sdk::PlotTabHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  auto create_status = view.create("tab-a");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("not bound"), std::string::npos);

  auto close_status = view.close("tab-a");
  EXPECT_FALSE(close_status);
  EXPECT_NE(close_status.error().find("not bound"), std::string::npos);

  auto list_status = view.list();
  EXPECT_FALSE(list_status);
  EXPECT_NE(list_status.error().find("not bound"), std::string::npos);

  auto config_status = view.configOf("tab-a");
  EXPECT_FALSE(config_status);
  EXPECT_NE(config_status.error().find("not bound"), std::string::npos);

  auto add_status = view.addCurve("tab-a", "imu/accel", "x");
  EXPECT_FALSE(add_status);
  EXPECT_NE(add_status.error().find("not bound"), std::string::npos);

  auto remove_status = view.removeCurve("tab-a", "imu/accel", "x");
  EXPECT_FALSE(remove_status);
  EXPECT_NE(remove_status.error().find("not bound"), std::string::npos);

  auto clear_status = view.clear("tab-a");
  EXPECT_FALSE(clear_status);
  EXPECT_NE(clear_status.error().find("not bound"), std::string::npos);
}

TEST(PlotTabApiTest, DatasetQualifierReachesTheHost) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.create("tab-a"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu/accel", "x", "bag1"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu/gyro", "y"));

  auto* tab = host.find("tab-a");
  ASSERT_NE(tab, nullptr);
  ASSERT_EQ(tab->curves.size(), 2u);
  EXPECT_EQ(tab->curves[0].dataset, "bag1");
  EXPECT_TRUE(tab->curves[1].dataset.empty());
}

// --- Scene tabs (tail slots) ---------------------------------------------------

TEST(PlotTabSceneApiTest, CreateAndListRoundTrip) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createTabV2("view-a", "3d", "First"));
  ASSERT_TRUE(view.createTabV2("view-b", "2d", "Second"));

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 2u);
  EXPECT_EQ((*ids)[0], "view-a");
  EXPECT_EQ((*ids)[1], "view-b");
}

TEST(PlotTabSceneApiTest, ConfigReadsBackWhatWasAttached) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  const std::string title = R"(My "quoted" \ View)";
  ASSERT_TRUE(view.createTabV2("view-a", "3d", title));
  ASSERT_TRUE(view.attachTopic("view-a", "lidar/points", "bag1"));
  ASSERT_TRUE(view.attachTopic("view-a", R"(camera/"raw")", "bag1"));
  ASSERT_TRUE(view.attachTopic("view-a", "lidar/points", "bag1"));  // idempotent

  auto config = view.configOf("view-a");
  ASSERT_TRUE(config) << config.error();
  const auto parsed = nlohmann::json::parse(*config);  // throws on invalid JSON
  EXPECT_EQ(parsed.at("kind"), "3d");
  EXPECT_EQ(parsed.at("title"), title);
  const auto& topics = parsed.at("topics");
  ASSERT_EQ(topics.size(), 2u);
  EXPECT_EQ(topics[0].at("topic"), "lidar/points");
  EXPECT_EQ(topics[0].at("dataset"), "bag1");
  EXPECT_EQ(topics[1].at("topic"), R"(camera/"raw")");
}

TEST(PlotTabSceneApiTest, DetachMissingTopicIsAnError) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createTabV2("view-a", "3d"));
  auto status = view.detachTopic("view-a", "lidar/points", "bag1");
  EXPECT_FALSE(status);
}

TEST(PlotTabSceneApiTest, UnknownIdIsAnError) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  EXPECT_FALSE(view.attachTopic("nope", "lidar/points"));
  EXPECT_FALSE(view.configOf("nope"));
  EXPECT_FALSE(view.close("nope"));
  EXPECT_FALSE(view.focusTab("nope"));
}

TEST(PlotTabSceneApiTest, HostFailureSurfacesTheMessage) {
  FakePlotTabHost host;
  host.should_fail = true;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  auto create_status = view.createTabV2("view-a", "3d");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("tab boom"), std::string::npos);

  auto attach_status = view.attachTopic("view-a", "lidar/points");
  EXPECT_FALSE(attach_status);
  EXPECT_NE(attach_status.error().find("tab boom"), std::string::npos);

  auto list_status = view.list();
  EXPECT_FALSE(list_status);
  EXPECT_NE(list_status.error().find("tab boom"), std::string::npos);
}

TEST(PlotTabSceneApiTest, UnboundViewReportsNotBound) {
  sdk::PlotTabHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  auto create_status = view.createTabV2("view-a", "3d");
  EXPECT_FALSE(create_status);
  EXPECT_NE(create_status.error().find("not bound"), std::string::npos);

  auto close_status = view.close("view-a");
  EXPECT_FALSE(close_status);
  EXPECT_NE(close_status.error().find("not bound"), std::string::npos);

  auto list_status = view.list();
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

  auto focus_status = view.focusTab("view-a");
  EXPECT_FALSE(focus_status);
  EXPECT_NE(focus_status.error().find("not bound"), std::string::npos);
}

TEST(PlotTabSceneApiTest, DatasetQualifierReachesTheHost) {
  FakePlotTabHost host;
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createTabV2("view-a", "3d"));
  ASSERT_TRUE(view.attachTopic("view-a", "lidar/points", "bag1"));
  ASSERT_TRUE(view.attachTopic("view-a", "camera/image"));

  auto* found = host.find("view-a");
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->topics.size(), 2u);
  EXPECT_EQ(found->topics[0].dataset, "bag1");
  EXPECT_TRUE(found->topics[1].dataset.empty());
}

// Each tail slot is gated on its own: a host may serve some and not others.
TEST(PlotTabSceneApiTest, EachTailSlotIsGatedOnItsOwn) {
  struct Case {
    const char* slot;
    void (*null_it)(PJ_plot_tab_host_vtable_t&);
  };
  const Case cases[] = {
      {"create_tab_v2", [](PJ_plot_tab_host_vtable_t& vt) { vt.create_tab_v2 = nullptr; }},
      {"attach_topic", [](PJ_plot_tab_host_vtable_t& vt) { vt.attach_topic = nullptr; }},
      {"detach_topic", [](PJ_plot_tab_host_vtable_t& vt) { vt.detach_topic = nullptr; }},
      {"focus_tab", [](PJ_plot_tab_host_vtable_t& vt) { vt.focus_tab = nullptr; }},
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.slot);
    FakePlotTabHost host;
    auto vtable = makePlotTabVtable();
    test_case.null_it(vtable);
    sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});
    ASSERT_TRUE(view.create("tab-a"));  // v1 slots are unaffected

    const std::string slot = test_case.slot;
    const auto create = view.createTabV2("tab-a", "3d");
    const auto attach = view.attachTopic("tab-a", "lidar/points");
    const auto detach = view.detachTopic("tab-a", "lidar/points");
    const auto focus = view.focusTab("tab-a");
    const std::pair<const char*, const Status*> results[] = {
        {"create_tab_v2", &create}, {"attach_topic", &attach}, {"detach_topic", &detach}, {"focus_tab", &focus}};
    for (const auto& [name, status] : results) {
      if (slot == name) {
        ASSERT_FALSE(*status);
        EXPECT_EQ(status->error(), "plot tab host does not support " + slot);
      } else if (!*status) {  // e.g. detach after a refused attach: a host error, never a gate
        EXPECT_EQ(status->error().find("does not support"), std::string::npos) << name << ": " << status->error();
      }
    }
  }
}

// Every wrapper at every prefix boundary: a slot the struct does not cover is never
// entered (counted before the fake validates anything), a covered one is entered once.
TEST(PlotTabSceneApiTest, EveryWrapperHonoursEveryPrefixBoundary) {
  const std::pair<const char*, size_t> slot_ends[] = {
      {"create_tab_v2", offsetof(PJ_plot_tab_host_vtable_t, attach_topic)},
      {"attach_topic", offsetof(PJ_plot_tab_host_vtable_t, detach_topic)},
      {"detach_topic", offsetof(PJ_plot_tab_host_vtable_t, focus_tab)},
      {"focus_tab", sizeof(PJ_plot_tab_host_vtable_t)},
  };
  const size_t boundaries[] = {
      PJ_PLOT_TAB_HOST_MIN_VTABLE_SIZE, slot_ends[0].second, slot_ends[1].second, slot_ends[2].second,
      slot_ends[3].second};
  for (const size_t boundary : boundaries) {
    SCOPED_TRACE(boundary);
    FakePlotTabHost host;
    auto vtable = makePlotTabVtable();
    vtable.struct_size = static_cast<uint32_t>(boundary);
    sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});
    const Status results[] = {
        view.createTabV2("tab-a", "3d"), view.attachTopic("tab-a", "t"), view.detachTopic("tab-a", "t"),
        view.focusTab("tab-a")};
    for (size_t i = 0; i < 4; ++i) {
      const auto& [slot, end] = slot_ends[i];
      SCOPED_TRACE(slot);
      if (end <= boundary) {
        EXPECT_EQ(host.calls[slot], 1);
      } else {
        EXPECT_EQ(host.calls[slot], 0);
        ASSERT_FALSE(results[i]);
        EXPECT_EQ(results[i].error(), std::string("plot tab host does not support ") + slot);
      }
    }
  }
}

// A kind the host knows but cannot serve in this build is refused naming it.
// It is validated before anything is replaced: a refused create leaves an existing tab.
TEST(PlotTabSceneApiTest, UnservableKindErrorNamesTheKindAndKeepsTheExistingTab) {
  FakePlotTabHost host;
  host.unservable_kind = "2d";
  const auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.create("tab-a", "Mine"));
  ASSERT_TRUE(view.addCurve("tab-a", "imu", "x"));

  const auto status = view.createTabV2("tab-a", "2d");
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("'2d'"), std::string::npos);
  const auto config = view.configOf("tab-a");
  ASSERT_TRUE(config) << config.error();
  const auto parsed = nlohmann::json::parse(*config);
  EXPECT_EQ(parsed.at("title"), "Mine");
  ASSERT_EQ(parsed.at("curves").size(), 1u);
  EXPECT_EQ(parsed.at("curves")[0].at("field"), "x");
  EXPECT_TRUE(view.createTabV2("tab-b", "3d"));
}

TEST(PlotTabSceneApiTest, AddCurveOnSceneTabSurfacesHostError) {
  FakePlotTabHost host;
  auto vtable = makePlotTabVtable();
  sdk::PlotTabHostView view(PJ_plot_tab_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.createTabV2("tab-a", "3d"));
  auto status = view.addCurve("tab-a", "imu/accel", "x");
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("tab 'tab-a' is a 3d scene tab: use attach_topic/detach_topic"), std::string::npos);
}

}  // namespace
}  // namespace PJ
