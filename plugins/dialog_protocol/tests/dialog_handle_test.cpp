#include <PJ/host/dialog_handle.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

// Defined in mock_streamer.cpp, linked statically
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable();

class DialogHandleTest : public ::testing::Test {
 protected:
  const PJ_dialog_vtable_t* vt_ = PJ_get_dialog_vtable();
};

// --- Construction / Destruction (RAII lifecycle) ---

TEST_F(DialogHandleTest, ConstructAndDestroy) {
  // Should not crash — RAII creates and destroys context
  PJ::host::DialogHandle h(vt_);
  EXPECT_NE(h.vtable(), nullptr);
  EXPECT_NE(h.context(), nullptr);
}

TEST_F(DialogHandleTest, NullVtable) {
  // Null vtable should be safe — no crash
  PJ::host::DialogHandle h(nullptr);
  EXPECT_EQ(h.vtable(), nullptr);
  EXPECT_EQ(h.context(), nullptr);
}

// --- Move semantics ---

TEST_F(DialogHandleTest, MoveConstruct) {
  PJ::host::DialogHandle h1(vt_);
  void* ctx = h1.context();

  PJ::host::DialogHandle h2(std::move(h1));
  EXPECT_EQ(h2.context(), ctx);
  EXPECT_EQ(h2.vtable(), vt_);

  // Moved-from handle should be empty
  EXPECT_EQ(h1.vtable(), nullptr);
  EXPECT_EQ(h1.context(), nullptr);
}

TEST_F(DialogHandleTest, MoveAssign) {
  PJ::host::DialogHandle h1(vt_);
  PJ::host::DialogHandle h2(vt_);
  void* ctx1 = h1.context();
  void* ctx2 = h2.context();

  // Move-assign h1 into h2 — contexts are swapped (h1 gets h2's old context,
  // which is destroyed when h1 goes out of scope)
  h2 = std::move(h1);
  EXPECT_EQ(h2.context(), ctx1);
  // h1 now holds h2's old context (valid, will be cleaned up by h1's dtor)
  EXPECT_EQ(h1.context(), ctx2);
}

TEST_F(DialogHandleTest, SelfMoveAssign) {
  PJ::host::DialogHandle h(vt_);
  void* ctx = h.context();

  // Self-move should be safe
  h = std::move(h);
  EXPECT_EQ(h.context(), ctx);
}

// --- Queries ---

TEST_F(DialogHandleTest, ManifestIsValidJson) {
  PJ::host::DialogHandle h(vt_);
  std::string manifest = h.manifest();
  auto j = nlohmann::json::parse(manifest, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.contains("name"));
  EXPECT_EQ(j["name"], "Mock Streamer");
}

TEST_F(DialogHandleTest, UiContentIsNonEmpty) {
  PJ::host::DialogHandle h(vt_);
  std::string ui = h.ui_content();
  EXPECT_FALSE(ui.empty());
  EXPECT_NE(ui.find("<ui"), std::string::npos);
}

TEST_F(DialogHandleTest, WidgetDataIsValidJson) {
  PJ::host::DialogHandle h(vt_);
  std::string wd = h.widget_data();
  auto j = nlohmann::json::parse(wd, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.is_object());
  // MockStreamer sets host_input text
  EXPECT_EQ(j["host_input"]["text"], "localhost");
}

// --- Events ---

TEST_F(DialogHandleTest, SendEventUpdatesState) {
  PJ::host::DialogHandle h(vt_);
  bool refresh = h.send_event("host_input", R"({"text": "10.0.0.1"})");
  EXPECT_TRUE(refresh);

  auto j = nlohmann::json::parse(h.widget_data());
  EXPECT_EQ(j["host_input"]["text"], "10.0.0.1");
}

TEST_F(DialogHandleTest, SendEventUnknownWidget) {
  PJ::host::DialogHandle h(vt_);
  bool refresh = h.send_event("nonexistent", R"({"text": "x"})");
  EXPECT_FALSE(refresh);
}

// --- Tick ---

TEST_F(DialogHandleTest, TickInitiallyFalse) {
  PJ::host::DialogHandle h(vt_);
  EXPECT_FALSE(h.tick());
}

TEST_F(DialogHandleTest, TickDiscoverTopics) {
  PJ::host::DialogHandle h(vt_);
  // Connect
  (void)h.send_event("connect_btn", R"({"clicked": true})");

  bool refresh = false;
  for (int i = 0; i < 5; ++i) {
    refresh = h.tick();
    if (refresh) break;
  }
  EXPECT_TRUE(refresh);

  auto j = nlohmann::json::parse(h.widget_data());
  EXPECT_TRUE(j.contains("topic_list"));
  EXPECT_GT(j["topic_list"]["list_items"].size(), 0u);
}

// --- Config persistence ---

TEST_F(DialogHandleTest, SaveLoadConfigRoundTrip) {
  PJ::host::DialogHandle h1(vt_);
  (void)h1.send_event("host_input", R"({"text": "saved-host"})");
  (void)h1.send_event("port_input", R"({"value": 5555})");

  std::string config = h1.save_config();
  auto cfg = nlohmann::json::parse(config);
  EXPECT_EQ(cfg["host"], "saved-host");
  EXPECT_EQ(cfg["port"], 5555);

  // Load into a new handle
  PJ::host::DialogHandle h2(vt_);
  bool loaded = h2.load_config(config);
  EXPECT_TRUE(loaded);

  auto j = nlohmann::json::parse(h2.widget_data());
  EXPECT_EQ(j["host_input"]["text"], "saved-host");
  EXPECT_EQ(j["port_input"]["value"], 5555);
}

TEST_F(DialogHandleTest, LoadConfigInvalidJson) {
  PJ::host::DialogHandle h(vt_);
  EXPECT_FALSE(h.load_config("not json"));
}

// --- Error reporting ---

TEST_F(DialogHandleTest, NoErrorInitially) {
  PJ::host::DialogHandle h(vt_);
  EXPECT_EQ(h.last_error(), "");
}

TEST_F(DialogHandleTest, ErrorAfterTrigger) {
  PJ::host::DialogHandle h(vt_);
  // Clear host, then try to connect — triggers "Host cannot be empty"
  (void)h.send_event("host_input", R"({"text": ""})");
  (void)h.send_event("connect_btn", R"({"clicked": true})");

  std::string err = h.last_error();
  EXPECT_NE(err.find("empty"), std::string::npos);

  // Error should be cleared after reading
  EXPECT_EQ(h.last_error(), "");
}

// --- Accept / Reject ---

TEST_F(DialogHandleTest, AcceptDoesNotCrash) {
  PJ::host::DialogHandle h(vt_);
  h.accept(R"({"done": true})");
}

TEST_F(DialogHandleTest, RejectDoesNotCrash) {
  PJ::host::DialogHandle h(vt_);
  h.reject();
}

// --- Escape hatch ---

TEST_F(DialogHandleTest, VtableAndContextAccessors) {
  PJ::host::DialogHandle h(vt_);
  EXPECT_EQ(h.vtable(), vt_);
  EXPECT_NE(h.context(), nullptr);

  // Can call raw vtable functions if needed
  const char* manifest = h.vtable()->get_manifest(h.context());
  EXPECT_NE(manifest, nullptr);
}
