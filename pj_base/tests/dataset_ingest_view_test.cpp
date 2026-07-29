// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for DatasetIngestHostView — the wider ingest-lifecycle facade over
// the same PJ_data_source_runtime_host_t fat pointer that ParserIngestHostView
// wraps. Covers:
//
//   1. ToolboxRuntimeHostView::createDatasetIngest/releaseDatasetIngest
//      forward to the same create_parser_ingest/release_parser_ingest slots
//      as createParserIngest/releaseParserIngest, and the resulting view's
//      lifecycle calls (progressStart/progressUpdate/isStopRequested/
//      progressFinish) land on the runtime host handed back by
//      create_parser_ingest.
//   2. DatasetIngestHostView::parserIngest() / ensureParserBinding() share
//      the same underlying context — no separate binding surface.
//   3. reportMessage forwards.
//   4. An older toolbox runtime host (predates the create_parser_ingest tail
//      slot) fails both createDatasetIngest and releaseDatasetIngest with the
//      same error strings createParserIngest/releaseParserIngest produce —
//      behavior parity with the narrow facade.
//   5. A default-constructed DatasetIngestHostView is invalid.
//   6. DataSourceRuntimeHostView::datasetIngest() (the DataSource-side
//      accessor, no toolbox layer involved) returns a valid view that
//      forwards to the same recorder.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"
#include "pj_base/toolbox_protocol.h"

namespace {

// Fake DataSource runtime host recording lifecycle calls (modeled on
// push_message_test.cpp's / notify_available_topics_test.cpp's fake hosts).
class FakeRuntimeHost {
 public:
  FakeRuntimeHost() {
    vtable_.protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.report_message = &FakeRuntimeHost::reportMessageThunk;
    vtable_.progress_start = &FakeRuntimeHost::progressStartThunk;
    vtable_.progress_update = &FakeRuntimeHost::progressUpdateThunk;
    vtable_.progress_finish = &FakeRuntimeHost::progressFinishThunk;
    vtable_.is_stop_requested = &FakeRuntimeHost::isStopRequestedThunk;
    vtable_.ensure_parser_binding = &FakeRuntimeHost::ensureParserBindingThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  PJ_data_source_runtime_host_t host() const {
    return host_;
  }

  std::vector<std::string> calls;
  bool stop_requested = false;
  std::vector<std::string> bound_topics;  // topic_name of each ensure_parser_binding call

 private:
  static void reportMessageThunk(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t message) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    self->calls.push_back(
        "report:" + std::to_string(static_cast<int>(level)) + ":" + std::string(message.data, message.size));
  }

  static bool progressStartThunk(
      void* ctx, PJ_string_view_t label, uint64_t total_steps, bool cancellable, PJ_error_t* /*out_error*/) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    self->calls.push_back(
        "start:" + std::string(label.data, label.size) + ":" + std::to_string(total_steps) + ":" +
        (cancellable ? "c" : "n"));
    return true;
  }

  static bool progressUpdateThunk(void* ctx, uint64_t current_step) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    self->calls.push_back("update:" + std::to_string(current_step));
    return true;
  }

  static void progressFinishThunk(void* ctx) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    self->calls.push_back("finish");
  }

  static bool isStopRequestedThunk(void* ctx) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    return self->stop_requested;
  }

  static bool ensureParserBindingThunk(
      void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out_handle,
      PJ_error_t* /*out_error*/) noexcept {
    auto* self = static_cast<FakeRuntimeHost*>(ctx);
    self->bound_topics.emplace_back(request->topic_name.data, request->topic_name.size);
    out_handle->id = 99;
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
};

// Fake toolbox runtime host whose create_parser_ingest hands back a
// FakeRuntimeHost's fat pointer, and which records created/released ids.
class FakeToolboxRuntimeHost {
 public:
  explicit FakeToolboxRuntimeHost(PJ_data_source_runtime_host_t runtime_host) : runtime_host_(runtime_host) {
    vtable_.protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION;
    vtable_.struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t);
    vtable_.create_parser_ingest = &FakeToolboxRuntimeHost::createParserIngestThunk;
    vtable_.release_parser_ingest = &FakeToolboxRuntimeHost::releaseParserIngestThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  // Simulate a toolbox runtime host that predates the parser-ingest tail
  // slots: shrink struct_size to the offset of create_parser_ingest AND null
  // both fields (release_parser_ingest lives after it, so it disappears too).
  void dropCreateParserIngest() {
    vtable_.create_parser_ingest = nullptr;
    vtable_.release_parser_ingest = nullptr;
    vtable_.struct_size = offsetof(PJ_toolbox_runtime_host_vtable_t, create_parser_ingest);
  }

  PJ_toolbox_runtime_host_t host() const {
    return host_;
  }

  std::vector<uint32_t> created;
  std::vector<uint32_t> released;

 private:
  static bool createParserIngestThunk(
      void* ctx, uint32_t data_source_id, PJ_data_source_runtime_host_t* out_host, PJ_error_t* /*out_error*/) noexcept {
    auto* self = static_cast<FakeToolboxRuntimeHost*>(ctx);
    self->created.push_back(data_source_id);
    *out_host = self->runtime_host_;
    return true;
  }

  static bool releaseParserIngestThunk(void* ctx, uint32_t data_source_id, PJ_error_t* /*out_error*/) noexcept {
    auto* self = static_cast<FakeToolboxRuntimeHost*>(ctx);
    self->released.push_back(data_source_id);
    return true;
  }

  PJ_data_source_runtime_host_t runtime_host_;
  PJ_toolbox_runtime_host_vtable_t vtable_{};
  PJ_toolbox_runtime_host_t host_{};
};

TEST(DatasetIngestView, CreateForwardsLifecycleCallsToTheSameContext) {
  FakeRuntimeHost runtime;
  FakeToolboxRuntimeHost toolbox(runtime.host());
  PJ::ToolboxRuntimeHostView view(toolbox.host());

  auto ingest = view.createDatasetIngest(42);
  ASSERT_TRUE(ingest) << (ingest ? "" : ingest.error());
  ASSERT_TRUE(ingest->valid());
  EXPECT_EQ(toolbox.created, std::vector<uint32_t>({42U}));

  EXPECT_TRUE(ingest->progressStart("replay", 100, true));
  EXPECT_TRUE(ingest->progressUpdate(50));
  EXPECT_FALSE(ingest->isStopRequested());
  runtime.stop_requested = true;
  EXPECT_TRUE(ingest->isStopRequested());
  ingest->progressFinish();

  EXPECT_EQ(runtime.calls, std::vector<std::string>({"start:replay:100:c", "update:50", "finish"}));

  auto released = view.releaseDatasetIngest(42);
  EXPECT_TRUE(released) << (released ? "" : released.error());
  EXPECT_EQ(toolbox.released, std::vector<uint32_t>({42U}));
}

TEST(DatasetIngestView, ParserAccessSharesTheContext) {
  FakeRuntimeHost runtime;
  FakeToolboxRuntimeHost toolbox(runtime.host());
  PJ::ToolboxRuntimeHostView view(toolbox.host());

  auto ingest = view.createDatasetIngest(1);
  ASSERT_TRUE(ingest);

  // The narrow parser-only facade is a live view over the same context.
  PJ::ParserIngestHostView narrow = ingest->parserIngest();
  EXPECT_TRUE(narrow.valid());

  // Driving ensureParserBinding through the wide facade lands on the same
  // fake runtime host as driving it through the narrow one would.
  PJ::ParserBindingRequest request{"/topic", "protobuf", "some.Type", {}, "{}"};
  auto handle = ingest->ensureParserBinding(request);
  ASSERT_TRUE(handle) << (handle ? "" : handle.error());
  EXPECT_EQ(handle->id, 99U);
  ASSERT_EQ(runtime.bound_topics.size(), 1U);
  EXPECT_EQ(runtime.bound_topics[0], "/topic");

  // And the narrow facade obtained from the same dataset ingest reaches the
  // identical context — a second binding call is recorded on the same host.
  auto handle2 = narrow.ensureParserBinding(request);
  ASSERT_TRUE(handle2);
  EXPECT_EQ(runtime.bound_topics.size(), 2U);
}

TEST(DatasetIngestView, ReportMessageForwards) {
  FakeRuntimeHost runtime;
  FakeToolboxRuntimeHost toolbox(runtime.host());
  PJ::ToolboxRuntimeHostView view(toolbox.host());

  auto ingest = view.createDatasetIngest(5);
  ASSERT_TRUE(ingest);

  ingest->reportMessage(PJ::DataSourceMessageLevel::kInfo, "hello");

  ASSERT_EQ(runtime.calls.size(), 1U);
  EXPECT_EQ(runtime.calls[0], "report:0:hello");
}

TEST(DatasetIngestView, OlderHostWithoutTailSlotErrors) {
  FakeRuntimeHost runtime;
  FakeToolboxRuntimeHost toolbox(runtime.host());
  toolbox.dropCreateParserIngest();
  PJ::ToolboxRuntimeHostView view(toolbox.host());

  auto ingest = view.createDatasetIngest(7);
  ASSERT_FALSE(ingest);
  EXPECT_NE(ingest.error().find("create_parser_ingest"), std::string::npos) << ingest.error();
  EXPECT_NE(ingest.error().find("older host"), std::string::npos) << ingest.error();
  EXPECT_TRUE(toolbox.created.empty());

  // Structural parity: createDatasetIngest and createParserIngest share the
  // acquireParserIngestContext helper, so on the same older-host fixture they
  // must fail with the byte-identical error string, not just a similar one.
  auto parser_ingest = view.createParserIngest(7);
  ASSERT_FALSE(parser_ingest);
  EXPECT_EQ(ingest.error(), parser_ingest.error());

  auto released = view.releaseDatasetIngest(7);
  ASSERT_FALSE(released);
  EXPECT_NE(released.error().find("release_parser_ingest"), std::string::npos) << released.error();
  EXPECT_NE(released.error().find("older host"), std::string::npos) << released.error();
  EXPECT_TRUE(toolbox.released.empty());
}

TEST(DatasetIngestView, DefaultConstructedViewIsInvalid) {
  PJ::DatasetIngestHostView view;
  EXPECT_FALSE(view.valid());
}

TEST(DatasetIngestView, DataSourceDatasetIngestAccessor) {
  FakeRuntimeHost runtime;
  PJ::DataSourceRuntimeHostView data_source_view(runtime.host());

  auto ingest = data_source_view.datasetIngest();
  ASSERT_TRUE(ingest.valid());

  EXPECT_TRUE(ingest.progressStart("import", 10, false));
  EXPECT_EQ(runtime.calls, std::vector<std::string>({"start:import:10:n"}));
}

}  // namespace
