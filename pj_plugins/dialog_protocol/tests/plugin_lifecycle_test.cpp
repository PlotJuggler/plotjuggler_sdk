// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <pj_plugins/dialog_protocol.h>

#include <cstddef>
#include <cstring>
#include <nlohmann/json.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>

// Defined in mock_dialog.cpp, linked statically
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable() noexcept;

static_assert(offsetof(PJ_dialog_vtable_t, protocol_version) == 0, "dialog v4 prefix pinned");
static_assert(offsetof(PJ_dialog_vtable_t, struct_size) == 4, "dialog v4 prefix pinned");
static_assert(offsetof(PJ_dialog_vtable_t, create) == 8, "dialog create slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, destroy) == 16, "dialog destroy slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, get_manifest) == 24, "dialog manifest slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, get_ui_content) == 32, "dialog UI slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, get_widget_data) == 40, "dialog widget-data slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, on_widget_event) == 48, "dialog event slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, on_tick) == 56, "dialog tick slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, on_accepted) == 64, "dialog accepted slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, on_rejected) == 72, "dialog rejected slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, save_config) == 80, "dialog save-config slot pinned");
static_assert(offsetof(PJ_dialog_vtable_t, load_config) == 88, "v4 dialog baseline slot pinned");
static_assert(PJ_DIALOG_MIN_VTABLE_SIZE == 96, "Dialog MIN vtable size is pinned at v4.0");
static_assert(PJ_DIALOG_MIN_VTABLE_SIZE <= sizeof(PJ_dialog_vtable_t), "MIN must never exceed current");
static_assert(offsetof(PJ_dialog_vtable_t, manifest_json) == 96, "Dialog static manifest tail slot appended");
static_assert(sizeof(PJ_dialog_vtable_t) == 104, "Dialog vtable size updated deliberately on append");

class PluginLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    vt_ = PJ_get_dialog_vtable();
    ASSERT_NE(vt_, nullptr);
    ctx_ = vt_->create();
    ASSERT_NE(ctx_, nullptr);
  }

  void TearDown() override {
    if (ctx_) {
      vt_->destroy(ctx_);
      ctx_ = nullptr;
    }
  }

  const PJ_dialog_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
};

// --- Vtable structure ---

TEST_F(PluginLifecycleTest, ProtocolVersion) {
  EXPECT_EQ(vt_->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
}

TEST_F(PluginLifecycleTest, StructSize) {
  EXPECT_EQ(vt_->struct_size, sizeof(PJ_dialog_vtable_t));
  EXPECT_EQ(PJ_DIALOG_MIN_VTABLE_SIZE, offsetof(PJ_dialog_vtable_t, manifest_json));
}

TEST_F(PluginLifecycleTest, AllFunctionPointersNonNull) {
  EXPECT_NE(vt_->create, nullptr);
  EXPECT_NE(vt_->destroy, nullptr);
  EXPECT_NE(vt_->get_manifest, nullptr);
  EXPECT_NE(vt_->get_ui_content, nullptr);
  EXPECT_NE(vt_->get_widget_data, nullptr);
  EXPECT_NE(vt_->on_widget_event, nullptr);
  EXPECT_NE(vt_->on_tick, nullptr);
  EXPECT_NE(vt_->on_accepted, nullptr);
  EXPECT_NE(vt_->on_rejected, nullptr);
  EXPECT_NE(vt_->save_config, nullptr);
  EXPECT_NE(vt_->load_config, nullptr);
  ASSERT_TRUE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, vt_, manifest_json));
  EXPECT_NE(vt_->manifest_json, nullptr);
}

// --- Manifest ---

TEST_F(PluginLifecycleTest, ManifestIsValidJson) {
  const char* manifest = vt_->get_manifest(ctx_);
  ASSERT_NE(manifest, nullptr);
  auto j = nlohmann::json::parse(manifest, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.contains("name"));
  EXPECT_TRUE(j.contains("version"));
}

TEST_F(PluginLifecycleTest, StaticManifestMatchesRuntimeManifest) {
  ASSERT_TRUE(PJ_HAS_TAIL_SLOT(PJ_dialog_vtable_t, vt_, manifest_json));
  ASSERT_NE(vt_->manifest_json, nullptr);
  EXPECT_EQ(nlohmann::json::parse(vt_->manifest_json), nlohmann::json::parse(vt_->get_manifest(ctx_)));
}

TEST_F(PluginLifecycleTest, ManifestPointerStable) {
  const char* p1 = vt_->get_manifest(ctx_);
  const char* p2 = vt_->get_manifest(ctx_);
  // Stable pointer: same address, not just same content
  EXPECT_EQ(p1, p2);
}

// --- UI Content ---

TEST_F(PluginLifecycleTest, UiContentIsNonEmpty) {
  const char* ui = vt_->get_ui_content(ctx_);
  ASSERT_NE(ui, nullptr);
  EXPECT_GT(std::strlen(ui), 0u);
  // Should contain XML-like content
  EXPECT_NE(std::string(ui).find("<ui"), std::string::npos);
}

// --- Widget Data ---

TEST_F(PluginLifecycleTest, WidgetDataIsValidJson) {
  const char* wd = vt_->get_widget_data(ctx_);
  ASSERT_NE(wd, nullptr);
  auto j = nlohmann::json::parse(wd, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.is_object());
}

TEST_F(PluginLifecycleTest, WidgetDataPointerValidUntilNextCall) {
  const char* p1 = vt_->get_widget_data(ctx_);
  ASSERT_NE(p1, nullptr);
  std::string s1(p1);
  // Another call may invalidate p1, but we saved a copy
  const char* p2 = vt_->get_widget_data(ctx_);
  ASSERT_NE(p2, nullptr);
  // Both should produce valid JSON (content may or may not differ)
  EXPECT_FALSE(nlohmann::json::parse(s1, nullptr, false).is_discarded());
  EXPECT_FALSE(nlohmann::json::parse(p2, nullptr, false).is_discarded());
}

// --- Widget Events ---

TEST_F(PluginLifecycleTest, OnWidgetEventTextChanged) {
  bool refresh = vt_->on_widget_event(ctx_, "name_input", R"({"text": "my_source"})", nullptr);
  EXPECT_TRUE(refresh);
  // Verify the change took effect
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx_));
  EXPECT_EQ(j["name_input"]["text"], "my_source");
}

TEST_F(PluginLifecycleTest, OnWidgetEventUnknownWidget) {
  bool refresh = vt_->on_widget_event(ctx_, "nonexistent_widget", R"({"text": "x"})", nullptr);
  EXPECT_FALSE(refresh);
}

// --- Tick ---

TEST_F(PluginLifecycleTest, OnTickInitiallyFalse) {
  // mock_dialog has no tick behavior — always returns false
  EXPECT_FALSE(vt_->on_tick(ctx_, nullptr));
}

// --- Config round-trip ---

TEST_F(PluginLifecycleTest, SaveLoadConfigRoundTrip) {
  // Set some state
  vt_->on_widget_event(ctx_, "name_input", R"({"text": "test_name"})", nullptr);
  vt_->on_widget_event(ctx_, "count_input", R"({"value": 42})", nullptr);
  vt_->on_widget_event(ctx_, "verbose_check", R"({"checked": true})", nullptr);

  // Save config
  PJ_string_view_t saved_sv{};
  ASSERT_TRUE(vt_->save_config(ctx_, &saved_sv, nullptr));
  ASSERT_NE(saved_sv.data, nullptr);
  std::string saved_config(saved_sv.data, saved_sv.size);

  // Create a new context and load the config
  void* ctx2 = vt_->create();
  ASSERT_NE(ctx2, nullptr);
  PJ_string_view_t load_sv{saved_config.data(), saved_config.size()};
  bool loaded = vt_->load_config(ctx2, load_sv, nullptr);
  EXPECT_TRUE(loaded);

  // Verify the state was restored
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx2));
  EXPECT_EQ(j["name_input"]["text"], "test_name");
  EXPECT_EQ(j["count_input"]["value"], 42);
  EXPECT_EQ(j["verbose_check"]["checked"], true);

  vt_->destroy(ctx2);
}

TEST_F(PluginLifecycleTest, LoadConfigWithInvalidJson) {
  const char kBad[] = "not valid json";
  PJ_string_view_t sv{kBad, sizeof(kBad) - 1};
  bool loaded = vt_->load_config(ctx_, sv, nullptr);
  EXPECT_FALSE(loaded);
}

TEST_F(PluginLifecycleTest, LoadConfigWithWrongTypes) {
  // name as int instead of string — should not crash, should still return true
  // (type-safe loading just skips invalid fields)
  const char kJson[] = R"({"name": 42, "count": "not_int"})";
  PJ_string_view_t sv{kJson, sizeof(kJson) - 1};
  bool loaded = vt_->load_config(ctx_, sv, nullptr);
  EXPECT_TRUE(loaded);
  // Verify name was NOT overwritten (was string, got int — skipped)
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx_));
  EXPECT_EQ(j["name_input"]["text"], "default");
}

// --- Accepted / Rejected ---

TEST_F(PluginLifecycleTest, OnAcceptedDoesNotCrash) {
  vt_->on_accepted(ctx_, R"({"some": "state"})");
  // Just verify it doesn't crash
}

TEST_F(PluginLifecycleTest, OnRejectedDoesNotCrash) {
  vt_->on_rejected(ctx_);
  // Just verify it doesn't crash
}

// --- Create/Destroy lifecycle ---

TEST_F(PluginLifecycleTest, MultipleCreateDestroy) {
  // Create and destroy multiple instances — should not leak (run under ASAN)
  for (int i = 0; i < 10; ++i) {
    void* ctx = vt_->create();
    ASSERT_NE(ctx, nullptr);
    vt_->get_widget_data(ctx);
    vt_->destroy(ctx);
  }
}

// --- VTable pointer is stable ---

TEST(PluginVTableTest, VtablePointerStable) {
  const PJ_dialog_vtable_t* vt1 = PJ_get_dialog_vtable();
  const PJ_dialog_vtable_t* vt2 = PJ_get_dialog_vtable();
  EXPECT_EQ(vt1, vt2);
}
