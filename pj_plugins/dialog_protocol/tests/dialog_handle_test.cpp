#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <pj_plugins/host/dialog_handle.hpp>
#include <string>

// Defined in mock_dialog.cpp, linked statically
extern "C" const PJ_dialog_vtable_t* PJ_get_dialog_vtable();

class DialogHandleTest : public ::testing::Test {
 protected:
  const PJ_dialog_vtable_t* vt_ = PJ_get_dialog_vtable();
};

// --- Construction / Destruction (RAII lifecycle) ---

TEST_F(DialogHandleTest, ConstructAndDestroy) {
  // Should not crash — RAII creates and destroys context
  PJ::DialogHandle h(vt_);
  EXPECT_NE(h.vtable(), nullptr);
  EXPECT_NE(h.context(), nullptr);
}

TEST_F(DialogHandleTest, NullVtable) {
  // Null vtable should be safe — no crash
  PJ::DialogHandle h(nullptr);
  EXPECT_EQ(h.vtable(), nullptr);
  EXPECT_EQ(h.context(), nullptr);
}

// --- Move semantics ---

TEST_F(DialogHandleTest, MoveConstruct) {
  PJ::DialogHandle h1(vt_);
  void* ctx = h1.context();

  PJ::DialogHandle h2(std::move(h1));
  EXPECT_EQ(h2.context(), ctx);
  EXPECT_EQ(h2.vtable(), vt_);

  // Moved-from handle should be empty
  EXPECT_EQ(h1.vtable(), nullptr);
  EXPECT_EQ(h1.context(), nullptr);
}

TEST_F(DialogHandleTest, MoveAssign) {
  PJ::DialogHandle h1(vt_);
  PJ::DialogHandle h2(vt_);
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
  PJ::DialogHandle h(vt_);
  void* ctx = h.context();

  // Self-move should be safe — use a reference to avoid -Wself-move
  auto& ref = h;
  h = std::move(ref);
  EXPECT_EQ(h.context(), ctx);
}

// --- Queries ---

TEST_F(DialogHandleTest, ManifestIsValidJson) {
  PJ::DialogHandle h(vt_);
  std::string manifest = h.manifest();
  auto j = nlohmann::json::parse(manifest, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.contains("name"));
  EXPECT_EQ(j["name"], "Mock Dialog");
}

TEST_F(DialogHandleTest, UiContentIsNonEmpty) {
  PJ::DialogHandle h(vt_);
  std::string ui = h.ui_content();
  EXPECT_FALSE(ui.empty());
  EXPECT_NE(ui.find("<ui"), std::string::npos);
}

TEST_F(DialogHandleTest, WidgetDataIsValidJson) {
  PJ::DialogHandle h(vt_);
  std::string wd = h.widget_data();
  auto j = nlohmann::json::parse(wd, nullptr, false);
  EXPECT_FALSE(j.is_discarded());
  EXPECT_TRUE(j.is_object());
  // MockDialog sets name_input text
  EXPECT_EQ(j["name_input"]["text"], "default");
}

// --- Events ---

TEST_F(DialogHandleTest, SendEventUpdatesState) {
  PJ::DialogHandle h(vt_);
  bool refresh = h.sendEvent("name_input", R"({"text": "new_name"})");
  EXPECT_TRUE(refresh);

  auto j = nlohmann::json::parse(h.widget_data());
  EXPECT_EQ(j["name_input"]["text"], "new_name");
}

TEST_F(DialogHandleTest, SendEventUnknownWidget) {
  PJ::DialogHandle h(vt_);
  bool refresh = h.sendEvent("nonexistent", R"({"text": "x"})");
  EXPECT_FALSE(refresh);
}

// --- Tick ---

TEST_F(DialogHandleTest, TickInitiallyFalse) {
  PJ::DialogHandle h(vt_);
  EXPECT_FALSE(h.tick());
}

// --- Config persistence ---

TEST_F(DialogHandleTest, SaveLoadConfigRoundTrip) {
  PJ::DialogHandle h1(vt_);
  (void)h1.sendEvent("name_input", R"({"text": "saved_name"})");
  (void)h1.sendEvent("count_input", R"({"value": 55})");

  std::string config = h1.save_config();
  auto cfg = nlohmann::json::parse(config);
  EXPECT_EQ(cfg["name"], "saved_name");
  EXPECT_EQ(cfg["count"], 55);

  // Load into a new handle
  PJ::DialogHandle h2(vt_);
  bool loaded = h2.load_config(config);
  EXPECT_TRUE(loaded);

  auto j = nlohmann::json::parse(h2.widget_data());
  EXPECT_EQ(j["name_input"]["text"], "saved_name");
  EXPECT_EQ(j["count_input"]["value"], 55);
}

TEST_F(DialogHandleTest, LoadConfigInvalidJson) {
  PJ::DialogHandle h(vt_);
  EXPECT_FALSE(h.load_config("not json"));
}

// --- Error reporting ---

TEST_F(DialogHandleTest, NoErrorInitially) {
  PJ::DialogHandle h(vt_);
  EXPECT_EQ(h.lastError(), "");
}

// --- Accept / Reject ---

TEST_F(DialogHandleTest, AcceptDoesNotCrash) {
  PJ::DialogHandle h(vt_);
  h.accept(R"({"done": true})");
}

TEST_F(DialogHandleTest, RejectDoesNotCrash) {
  PJ::DialogHandle h(vt_);
  h.reject();
}

// --- Borrowed handle ---

TEST_F(DialogHandleTest, BorrowedHandleWorks) {
  PJ::DialogHandle owned(vt_);
  void* ctx = owned.context();

  // Create a borrowed handle from the owned one's context
  auto borrowed = PJ::DialogHandle::borrowed(vt_, ctx);
  EXPECT_NE(borrowed.context(), nullptr);
  EXPECT_EQ(borrowed.context(), ctx);

  // Borrowed handle should work for all query operations
  std::string wd = borrowed.widget_data();
  EXPECT_FALSE(wd.empty());

  bool refresh = borrowed.sendEvent("name_input", R"({"text": "via_borrowed"})");
  EXPECT_TRUE(refresh);

  // Changes via borrowed should be visible in owned
  auto j = nlohmann::json::parse(owned.widget_data());
  EXPECT_EQ(j["name_input"]["text"], "via_borrowed");
}

TEST_F(DialogHandleTest, BorrowedHandleDoesNotDestroyContext) {
  PJ::DialogHandle owned(vt_);
  void* ctx = owned.context();

  {
    // Borrowed handle goes out of scope — should NOT destroy context
    auto borrowed = PJ::DialogHandle::borrowed(vt_, ctx);
    (void)borrowed.widget_data();
  }

  // Owned handle should still work after borrowed is destroyed
  std::string wd = owned.widget_data();
  EXPECT_FALSE(wd.empty());
}

// --- Escape hatch ---

TEST_F(DialogHandleTest, VtableAndContextAccessors) {
  PJ::DialogHandle h(vt_);
  EXPECT_EQ(h.vtable(), vt_);
  EXPECT_NE(h.context(), nullptr);

  // Can call raw vtable functions if needed
  const char* manifest = h.vtable()->get_manifest(h.context());
  EXPECT_NE(manifest, nullptr);
}
