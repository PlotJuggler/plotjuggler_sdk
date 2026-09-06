// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/testing/delegated_ingest_fixture.hpp"

#include <gtest/gtest.h>

#include <future>
#include <memory>

#include "pj_base/sdk/source/outcome_ledger.hpp"
#include "pj_base/sdk/source/stop_bridge.hpp"

namespace PJ::sdk::testing {
namespace {
using namespace std::chrono_literals;

ParserBindingRequest bindingRequest() {
  return {"/imu", "cdr", "Imu", {}, "{}"};
}

TEST(DelegatedIngestFixture, RecordsFullLifecycleAndAnchorsSurviveFetcherRelease) {
  DelegatedIngestFixture fixture;
  auto source = fixture.writeView().createDataSource("dataset");
  ASSERT_TRUE(source);
  auto runtime = fixture.runtimeView();
  auto ingest = runtime.createDatasetIngest(source->id);
  ASSERT_TRUE(ingest);
  runtime.notifyDataChanged();
  ingest->reportMessage(DataSourceMessageLevel::kInfo, "download");
  ASSERT_TRUE(ingest->attachSourceRecord(R"({"kind":"test","v":1,"request":{}})"));
  ASSERT_TRUE(ingest->progressStart("fetch", 10, true));
  EXPECT_TRUE(ingest->progressUpdate(5));
  auto binding = ingest->ensureParserBinding(bindingRequest());
  ASSERT_TRUE(binding);
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1, 2, 3});
  std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
  int fetched = 0;
  ASSERT_TRUE(ingest->pushMessage(*binding, 42, [owned = std::move(bytes), &fetched] {
    ++fetched;
    return PayloadView(owned);
  }));
  EXPECT_EQ(fetched, 2);
  EXPECT_TRUE(weak.expired());
  source::IngestOutcomeLedger ledger({"/imu"});
  ASSERT_TRUE(ledger.record("/imu", source::TopicOutcome::kOk));
  const auto completion = ledger.computeCompletion(ingest->isStopRequested());
  const std::string_view topics[]{"/imu"};
  ASSERT_TRUE(ingest->completeIngest(completion.outcome, {topics, 1}, completion.flags));
  ingest->progressFinish();
  ASSERT_TRUE(runtime.releaseDatasetIngest(source->id));
  const auto record = fixture.snapshot();
  ASSERT_EQ(record.pushes.size(), 1u);
  EXPECT_EQ(record.pushes[0].bytes, (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_EQ(record.pushes[0].timestamp_ns, 42);
  EXPECT_EQ(record.bindings[0].topic, "/imu");
  EXPECT_EQ(record.completions[0].requested_topics, (std::vector<std::string>{"/imu"}));
  EXPECT_TRUE(record.dataset_survives);
  EXPECT_FALSE(record.ingest_live);
  EXPECT_EQ(
      record.events,
      (std::vector<std::string>{
          "create_dataset", "create_ingest", "notify_data_changed", "notification", "attach_source_record",
          "progress_start", "progress_update", "binding", "push_enter", "fetcher_release", "push", "anchors_release",
          "complete_ingest", "progress_finish", "release_ingest"}));
}

TEST(DelegatedIngestFixture, FailureInjectionReleasesPayloadsAndDiscardRemovesDataset) {
  DelegatedIngestFixture fixture;
  ASSERT_TRUE(fixture.writeView().createDataSource("test"));
  fixture.refuse_create = true;
  EXPECT_FALSE(fixture.runtimeView().createDatasetIngest(1));
  fixture.refuse_create = false;
  auto ingest = fixture.runtimeView().createDatasetIngest(1);
  ASSERT_TRUE(ingest);
  fixture.refuse_progress_start = true;
  EXPECT_FALSE(ingest->progressStart("test", 10, true));
  fixture.refuse_binding = true;
  EXPECT_FALSE(ingest->ensureParserBinding(bindingRequest()));
  fixture.refuse_binding = false;
  auto binding = ingest->ensureParserBinding(bindingRequest());
  ASSERT_TRUE(binding);
  fixture.refuse_push = true;
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(1, 7);
  std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
  EXPECT_FALSE(ingest->pushMessage(*binding, 0, [owned = std::move(bytes)] { return PayloadView(owned); }));
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(fixture.snapshot().pushes.empty());
  EXPECT_TRUE(fixture.runtimeView().discardParserIngest(1));
  EXPECT_FALSE(fixture.snapshot().dataset_survives);
  EXPECT_FALSE(fixture.snapshot().ingest_live);
}

TEST(DelegatedIngestFixture, RejectsNonIdempotentAndFailedSecondFetch) {
  DelegatedIngestFixture fixture;
  ASSERT_TRUE(fixture.writeView().createDataSource("test"));
  auto ingest = fixture.runtimeView().createDatasetIngest(1);
  ASSERT_TRUE(ingest);
  auto binding = ingest->ensureParserBinding(bindingRequest());
  ASSERT_TRUE(binding);
  std::uint8_t counter = 0;
  EXPECT_FALSE(ingest->pushMessage(*binding, 0, [&counter] { return std::vector<std::uint8_t>{++counter}; }));
  int calls = 0;
  EXPECT_FALSE(ingest->pushMessage(*binding, 0, [&calls] {
    if (++calls == 2) {
      throw std::runtime_error("second fetch failed");
    }
    return std::vector<std::uint8_t>{1};
  }));
  EXPECT_FALSE(ingest->pushMessage({999}, 0, [] { return std::vector<std::uint8_t>{1}; }));
  EXPECT_TRUE(fixture.snapshot().pushes.empty());
  EXPECT_TRUE(fixture.runtimeView().releaseDatasetIngest(1));
}

TEST(DelegatedIngestFixture, CompletionUsesSdkFailClosedValidatorAndTruncatedVtables) {
  DelegatedIngestFixture fixture;
  ASSERT_TRUE(fixture.writeView().createDataSource("test"));
  auto ingest = fixture.runtimeView().createDatasetIngest(1);
  ASSERT_TRUE(ingest);
  const std::string_view duplicate[]{"/imu", "/imu"};
  EXPECT_FALSE(ingest->completeIngest(IngestOutcome::kCompleted, {duplicate, 2}));
  EXPECT_FALSE(ingest->completeIngest(IngestOutcome::kFailed, {}, PJ_INGEST_COMPLETION_FLAG_ATTESTS_EMPTY_TOPICS));
  EXPECT_TRUE(fixture.snapshot().completions.empty());
  fixture.emulateHostWithoutCompleteIngest();
  EXPECT_FALSE(ingest->completeIngest(IngestOutcome::kCompleted, {}));
  EXPECT_TRUE(ingest->attachSourceRecord("{}"));
  fixture.emulateRuntimeWithoutDiscard();
  EXPECT_FALSE(fixture.runtimeView().discardParserIngest(1));
  fixture.emulateHostWithoutProgress();
  EXPECT_FALSE(ingest->progressStart("test", 10, true));
  EXPECT_FALSE(ingest->progressUpdate(1));
  EXPECT_TRUE(fixture.runtimeView().releaseDatasetIngest(1));
}

TEST(DelegatedIngestFixture, HostStopAndProviderCancelWakeBlockedIngestBeforeJoin) {
  for (bool host_button : {false, true}) {
    DelegatedIngestFixture fixture;
    fixture.block_push_until_stop = true;
    ASSERT_TRUE(fixture.writeView().createDataSource("test"));
    auto ingest = fixture.runtimeView().createDatasetIngest(1);
    ASSERT_TRUE(ingest);
    auto binding = ingest->ensureParserBinding(bindingRequest());
    ASSERT_TRUE(binding);
    std::atomic<int> cancelled{0};
    std::promise<void> observed_stop;
    auto stop_view = fixture.dataSourceView();
    source::StopPoller poller(
        [&] { return stop_view.isStopRequested(); },
        [&] {
          ++cancelled;
          observed_stop.set_value();
        },
        1ms);
    auto pushing = std::async(std::launch::async, [&] {
      return ingest->pushMessage(*binding, 0, [] { return std::vector<std::uint8_t>{1}; });
    });
    EXPECT_TRUE(fixture.waitUntilPushBlocked());
    if (host_button) {
      fixture.requestStopFromHost();
    } else {
      stop_view.requestStop(DataSourceState::kStopped, "cancel before join");
    }
    EXPECT_EQ(pushing.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(pushing.get());
    EXPECT_TRUE(stop_view.isStopRequested());
    EXPECT_FALSE(ingest->progressUpdate(1));
    EXPECT_EQ(observed_stop.get_future().wait_for(2s), std::future_status::ready);
    poller.stopAndJoin();
    EXPECT_EQ(cancelled.load(), 1);
    EXPECT_TRUE(fixture.runtimeView().discardParserIngest(1));
    const auto record = fixture.snapshot();
    EXPECT_EQ(record.stop_requests.size(), host_button ? 0u : 1u);
    EXPECT_FALSE(record.dataset_survives);
  }
}

}  // namespace
}  // namespace PJ::sdk::testing
