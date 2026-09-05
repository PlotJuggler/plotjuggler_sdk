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

// Fake host for pj.plot_tabs.v1: a small model of tabs-with-curves, not a bare
// recorder, so the round-trip tests below can assert on what a read-back
// actually contains rather than merely that a call was forwarded.
struct FakePlotTabHost {
  struct Curve {
    std::string topic;
    std::string field;
    std::string dataset;
  };
  struct Tab {
    std::string id;
    std::string title;
    std::vector<Curve> curves;
  };

  std::vector<Tab> tabs;
  bool should_fail = false;
  std::string last_config_json;  // storage backing the borrowed tab_config out-string

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

bool ptCreateTab(void* ctx, PJ_string_view_t id, PJ_string_view_t title, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlotTabHost*>(ctx);
  if (ptFail(self, out_error)) {
    return false;
  }
  const auto id_sv = sdk::toStringView(id);
  auto* existing = self->find(id_sv);
  if (existing != nullptr) {
    existing->curves.clear();
    existing->title = std::string(sdk::toStringView(title));
    return true;
  }
  self->tabs.push_back(
      FakePlotTabHost::Tab{.id = std::string(id_sv), .title = std::string(sdk::toStringView(title)), .curves = {}});
  return true;
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
  std::string json = "{\"title\":\"" + tab->title + "\",\"curves\":[";
  for (size_t i = 0; i < tab->curves.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    const auto& curve = tab->curves[i];
    json +=
        "{\"topic\":\"" + curve.topic + "\",\"field\":\"" + curve.field + "\",\"dataset\":\"" + curve.dataset + "\"}";
  }
  json += "]}";
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
  return true;
}

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

}  // namespace
}  // namespace PJ
