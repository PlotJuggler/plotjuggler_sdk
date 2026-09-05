// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the complete_ingest terminal (0.30.0):
//
//   1. DataSourceRuntimeHostView::completeIngest flows outcome + the borrowed
//      requested-topic list through the slot; the host copies during the call
//      (copyIngestCompletion) and owns the strings afterwards.
//   2. Old-host negotiation: short struct_size / NULL slot yields an explicit
//      error and the plugin proceeds uncached; struct_size alone gates.
//   3. copyIngestCompletion fails CLOSED on every malformed shape: undersized
//      struct, unknown outcome, nonzero flags, inconsistent or over-bound or
//      duplicate topic lists. A larger (newer) struct_size stays readable.
//   4. ToolboxRuntimeHostView::discardParserIngest reaches the formalized
//      toolbox tail slot with the same negotiation rules.

#include "pj_base/sdk/ingest_completion.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"
#include "pj_base/toolbox_protocol.h"

namespace {

using PJ::sdk::copyIngestCompletion;
using PJ::sdk::IngestCompletionRecord;
using PJ::sdk::IngestOutcome;

// Mock runtime host — copies the borrowed completion during the call, exactly
// as a real host must.
class MockRuntimeHost {
 public:
  MockRuntimeHost() {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.complete_ingest = &MockRuntimeHost::completeThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  void dropCompleteIngest() {
    vtable_.complete_ingest = nullptr;
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, complete_ingest);
  }

  void shrinkStructSizeOnly() {
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, complete_ingest);
  }

  PJ::DataSourceRuntimeHostView view() const {
    return PJ::DataSourceRuntimeHostView(host_);
  }

  int call_count = 0;
  PJ::Expected<IngestCompletionRecord> copied = PJ::unexpected(std::string("never called"));

 private:
  static bool completeThunk(void* ctx, const PJ_ingest_completion_t* completion, PJ_error_t* err) noexcept {
    auto* self = static_cast<MockRuntimeHost*>(ctx);
    self->call_count++;
    self->copied = copyIngestCompletion(completion);
    if (!self->copied && err != nullptr) {
      std::snprintf(err->message, sizeof(err->message), "%s", self->copied.error().c_str());
      return false;
    }
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
};

TEST(IngestCompletionTest, OutcomeAndTopicsFlowThroughAndAreCopied) {
  MockRuntimeHost host;
  const std::vector<std::string_view> topics{"/imu", "/camera/front", "/empty_but_requested"};

  auto status = host.view().completeIngest(IngestOutcome::kCompleted, {topics.data(), topics.size()});
  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.call_count, 1);
  ASSERT_TRUE(host.copied) << host.copied.error();
  EXPECT_EQ(host.copied->outcome, IngestOutcome::kCompleted);
  EXPECT_EQ(host.copied->requested_topics, (std::vector<std::string>{"/imu", "/camera/front", "/empty_but_requested"}));
}

TEST(IngestCompletionTest, FailedAndCancelledAreValidTerminals) {
  for (const auto outcome : {IngestOutcome::kFailed, IngestOutcome::kCancelled}) {
    MockRuntimeHost host;
    auto status = host.view().completeIngest(outcome, {});
    ASSERT_TRUE(status) << status.error();
    ASSERT_TRUE(host.copied);
    EXPECT_EQ(host.copied->outcome, outcome);
    EXPECT_TRUE(host.copied->requested_topics.empty());
  }
}

TEST(IngestCompletionTest, ReturnsErrorWhenSlotMissing) {
  MockRuntimeHost host;
  host.dropCompleteIngest();
  auto status = host.view().completeIngest(IngestOutcome::kCompleted, {});
  EXPECT_FALSE(status);  // explicit: the plugin proceeds uncached
  EXPECT_EQ(host.call_count, 0);
}

TEST(IngestCompletionTest, ShortStructSizeAloneGatesTheSlot) {
  MockRuntimeHost host;
  host.shrinkStructSizeOnly();  // stale non-null pointer past the reported size
  auto status = host.view().completeIngest(IngestOutcome::kCompleted, {});
  EXPECT_FALSE(status);
  EXPECT_EQ(host.call_count, 0);
}

TEST(IngestCompletionTest, UnboundHostReportsNotBound) {
  PJ::DataSourceRuntimeHostView view;
  auto status = view.completeIngest(IngestOutcome::kCompleted, {});
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("not bound"), std::string::npos);
}

// ---- copyIngestCompletion: fail-closed validation --------------------------

PJ_ingest_completion_t validCompletion(const std::vector<PJ_string_view_t>& topics) {
  PJ_ingest_completion_t completion{};
  completion.struct_size = sizeof(PJ_ingest_completion_t);
  completion.outcome = PJ_INGEST_COMPLETED;
  completion.flags = PJ_INGEST_COMPLETION_FLAG_NONE;
  completion.requested_topics = topics.data();
  completion.requested_topic_count = topics.size();
  return completion;
}

PJ_string_view_t abi(std::string_view text) {
  return PJ_string_view_t{text.data(), text.size()};
}

TEST(CopyIngestCompletionTest, NullAndUndersizedStructsRefuse) {
  EXPECT_FALSE(copyIngestCompletion(nullptr));

  auto completion = validCompletion({});
  completion.struct_size = sizeof(PJ_ingest_completion_t) - 1;
  EXPECT_FALSE(copyIngestCompletion(&completion));
}

TEST(CopyIngestCompletionTest, LargerNewerStructStaysReadable) {
  auto completion = validCompletion({});
  completion.struct_size = sizeof(PJ_ingest_completion_t) + 64;  // a future caller
  ASSERT_TRUE(copyIngestCompletion(&completion));
}

TEST(CopyIngestCompletionTest, UnknownOutcomeAndFlagsRefuse) {
  auto completion = validCompletion({});
  completion.outcome = static_cast<PJ_ingest_outcome_t>(99);
  EXPECT_FALSE(copyIngestCompletion(&completion));

  completion = validCompletion({});
  completion.flags = UINT64_C(1);  // no v1 flags exist
  EXPECT_FALSE(copyIngestCompletion(&completion));
}

TEST(CopyIngestCompletionTest, MalformedTopicListsRefuse) {
  auto completion = validCompletion({});
  completion.requested_topic_count = 1;  // null list, nonzero count
  EXPECT_FALSE(copyIngestCompletion(&completion));

  completion = validCompletion({});
  completion.requested_topic_count = PJ::sdk::kMaxIngestCompletionTopics + 1;
  completion.requested_topics = reinterpret_cast<const PJ_string_view_t*>(&completion);  // never dereferenced
  EXPECT_FALSE(copyIngestCompletion(&completion));

  const std::vector<PJ_string_view_t> with_empty{abi("/ok"), PJ_string_view_t{nullptr, 0}};
  auto empty_name = validCompletion(with_empty);
  EXPECT_FALSE(copyIngestCompletion(&empty_name));

  const std::vector<PJ_string_view_t> with_duplicate{abi("/dup"), abi("/other"), abi("/dup")};
  auto duplicate = validCompletion(with_duplicate);
  auto result = copyIngestCompletion(&duplicate);
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("/dup"), std::string::npos);
}

TEST(CopyIngestCompletionTest, EmptyListIsStructurallyValid) {
  auto completion = validCompletion({});
  auto result = copyIngestCompletion(&completion);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->requested_topics.empty());  // cacheability policy lives in the capture service
}

// ---- discard_parser_ingest (formalized toolbox tail slot) ------------------

class MockToolboxHost {
 public:
  MockToolboxHost() {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t);
    vtable_.discard_parser_ingest = &MockToolboxHost::discardThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  void dropDiscard() {
    vtable_.discard_parser_ingest = nullptr;
    vtable_.struct_size = offsetof(PJ_toolbox_runtime_host_vtable_t, discard_parser_ingest);
  }

  PJ::ToolboxRuntimeHostView view() const {
    return PJ::ToolboxRuntimeHostView(host_);
  }

  uint32_t discarded_id = 0;
  int call_count = 0;

 private:
  static bool discardThunk(void* ctx, uint32_t data_source_id, PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<MockToolboxHost*>(ctx);
    self->call_count++;
    self->discarded_id = data_source_id;
    return true;
  }

  PJ_toolbox_runtime_host_vtable_t vtable_{};
  PJ_toolbox_runtime_host_t host_{};
};

TEST(DiscardParserIngestTest, ReachesTheFormalizedSlot) {
  MockToolboxHost host;
  auto status = host.view().discardParserIngest(42);
  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.call_count, 1);
  EXPECT_EQ(host.discarded_id, 42u);
}

TEST(DiscardParserIngestTest, OlderHostReportsMissingSlot) {
  MockToolboxHost host;
  host.dropDiscard();
  auto status = host.view().discardParserIngest(42);
  ASSERT_FALSE(status);
  EXPECT_EQ(host.call_count, 0);
  EXPECT_NE(status.error().find("older host"), std::string::npos);
}

}  // namespace
