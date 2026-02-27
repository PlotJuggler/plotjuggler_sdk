#include <pj/dialog_protocol.h>
#include <pj/sdk/widget_data.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <string>

// Defined in mock_streamer.cpp, linked statically
extern "C" const pj_dialog_vtable_t* pj_get_dialog_vtable();

class PluginLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    vt_ = pj_get_dialog_vtable();
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

  const pj_dialog_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
};

// --- Vtable structure ---

TEST_F(PluginLifecycleTest, ProtocolVersion) {
  EXPECT_EQ(vt_->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
}

TEST_F(PluginLifecycleTest, StructSize) {
  EXPECT_EQ(vt_->struct_size, sizeof(pj_dialog_vtable_t));
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
  EXPECT_NE(vt_->get_last_error, nullptr);
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
  bool refresh = vt_->on_widget_event(ctx_, "host_input", R"({"text": "192.168.1.1"})");
  EXPECT_TRUE(refresh);
  // Verify the change took effect
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx_));
  EXPECT_EQ(j["host_input"]["text"], "192.168.1.1");
}

TEST_F(PluginLifecycleTest, OnWidgetEventUnknownWidget) {
  bool refresh = vt_->on_widget_event(ctx_, "nonexistent_widget", R"({"text": "x"})");
  EXPECT_FALSE(refresh);
}

// --- Tick ---

TEST_F(PluginLifecycleTest, OnTickInitiallyFalse) {
  // Before connecting, tick should not request a refresh
  EXPECT_FALSE(vt_->on_tick(ctx_));
}

TEST_F(PluginLifecycleTest, OnTickDiscoverTopics) {
  // Connect first
  vt_->on_widget_event(ctx_, "connect_btn", R"({"clicked": true})");
  // Tick a few times to trigger topic discovery
  bool refresh = false;
  for (int i = 0; i < 5; ++i) {
    refresh = vt_->on_tick(ctx_);
    if (refresh) break;
  }
  EXPECT_TRUE(refresh);
  // Topics should now be visible in widget data
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx_));
  EXPECT_TRUE(j.contains("topic_list"));
  EXPECT_TRUE(j["topic_list"].contains("list_items"));
  EXPECT_GT(j["topic_list"]["list_items"].size(), 0u);
}

// --- Config round-trip ---

TEST_F(PluginLifecycleTest, SaveLoadConfigRoundTrip) {
  // Set some state
  vt_->on_widget_event(ctx_, "host_input", R"({"text": "myhost.local"})");
  vt_->on_widget_event(ctx_, "port_input", R"({"value": 1234})");
  vt_->on_widget_event(ctx_, "use_tls_check", R"({"checked": true})");

  // Save config
  const char* config = vt_->save_config(ctx_);
  ASSERT_NE(config, nullptr);
  std::string saved_config(config);

  // Create a new context and load the config
  void* ctx2 = vt_->create();
  ASSERT_NE(ctx2, nullptr);
  bool loaded = vt_->load_config(ctx2, saved_config.c_str());
  EXPECT_TRUE(loaded);

  // Verify the state was restored
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx2));
  EXPECT_EQ(j["host_input"]["text"], "myhost.local");
  EXPECT_EQ(j["port_input"]["value"], 1234);
  EXPECT_EQ(j["use_tls_check"]["checked"], true);

  vt_->destroy(ctx2);
}

// --- Error reporting ---

TEST_F(PluginLifecycleTest, NoErrorInitially) {
  const char* err = vt_->get_last_error(ctx_);
  EXPECT_EQ(err, nullptr);
}

TEST_F(PluginLifecycleTest, ErrorAfterTrigger) {
  // Clear host, then try to connect — should set an error
  vt_->on_widget_event(ctx_, "host_input", R"({"text": ""})");
  vt_->on_widget_event(ctx_, "connect_btn", R"({"clicked": true})");
  const char* err = vt_->get_last_error(ctx_);
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("empty"), std::string::npos);
  // Error should be cleared after reading
  EXPECT_EQ(vt_->get_last_error(ctx_), nullptr);
}

TEST_F(PluginLifecycleTest, LoadConfigWithInvalidJson) {
  bool loaded = vt_->load_config(ctx_, "not valid json");
  EXPECT_FALSE(loaded);
}

TEST_F(PluginLifecycleTest, LoadConfigWithWrongTypes) {
  // port as string instead of int — should not crash, should still return true
  // (type-safe loading just skips invalid fields)
  bool loaded = vt_->load_config(ctx_, R"({"host": 42, "port": "not_int"})");
  EXPECT_TRUE(loaded);
  // Verify host was NOT overwritten (was string, got int — skipped)
  auto j = nlohmann::json::parse(vt_->get_widget_data(ctx_));
  EXPECT_EQ(j["host_input"]["text"], "localhost");
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
  const pj_dialog_vtable_t* vt1 = pj_get_dialog_vtable();
  const pj_dialog_vtable_t* vt2 = pj_get_dialog_vtable();
  EXPECT_EQ(vt1, vt2);
}
