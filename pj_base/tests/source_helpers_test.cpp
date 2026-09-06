// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>

#include "pj_base/sdk/platform.hpp"
#include "pj_base/sdk/source/limits.hpp"
#include "pj_base/sdk/source/origin.hpp"
#include "pj_base/sdk/source/outcome_ledger.hpp"
#include "pj_base/sdk/source/source_descriptor.hpp"
#include "pj_base/sdk/source/stop_bridge.hpp"
#include "pj_base/sdk/source/transfer_rate.hpp"

namespace PJ::sdk::source {
namespace {
using namespace std::chrono_literals;
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    if (auto prev = getEnv(name)) {
      had_prev_ = true;
      prev_ = *prev;
    }
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value);
#else
    ::setenv(name_.c_str(), value, 1);
#endif
  }

  ~ScopedEnv() {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), had_prev_ ? prev_.c_str() : "");
#else
    if (had_prev_) {
      ::setenv(name_.c_str(), prev_.c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
#endif
  }

  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

 private:
  std::string name_;
  bool had_prev_ = false;
  std::string prev_;
};

TEST(SourceLimits, StrictDecimalEnvironmentCeilings) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(envLimit("PJ_SOURCE_LIMIT_MISSING_XYZ", 99), 99);
  for (const char* invalid :
       {"", "0junk", "-1", "+1", " 0", "0 ", "1.0", "1e2", "18446744073709551616", "000000000000000000000"}) {
    ScopedEnv environment("PJ_SOURCE_LIMIT_TEST", invalid);
    EXPECT_EQ(envLimit("PJ_SOURCE_LIMIT_TEST", 99), 99) << invalid;
  }
  for (const auto& [input, expected] :
       std::vector<std::pair<const char*, std::uint64_t>>{{"0", 0}, {"0012", 12}, {"18446744073709551615", maximum}}) {
    ScopedEnv environment("PJ_SOURCE_LIMIT_TEST", input);
    EXPECT_EQ(envLimit("PJ_SOURCE_LIMIT_TEST", 99), expected);
  }
  EXPECT_EQ(minNonzero(0, 0), 0);
  EXPECT_EQ(minNonzero(0, 5), 5);
  EXPECT_EQ(minNonzero(5, 0), 5);
  EXPECT_EQ(minNonzero(5, 7), 5);
  EXPECT_EQ(minNonzero(7, 5), 5);
}

TEST(SourceDecimalNs, BoundedDigitsOnly) {
  EXPECT_EQ(parseDecimalNs("0"), 0);
  EXPECT_EQ(parseDecimalNs("00012"), 12);
  EXPECT_EQ(parseDecimalNs("9223372036854775807"), std::numeric_limits<std::int64_t>::max());
  for (const char* invalid :
       {"", "-1", "+1", " 1", "1 ", "0junk", "1e2", "1.0", "9223372036854775808", "18446744073709551615",
        "000000000000000000000"}) {
    EXPECT_FALSE(parseDecimalNs(invalid)) << invalid;
  }
}

TEST(SourceOrigin, SchemelessCharsetAndPortContract) {
  for (const char* valid : {"localhost:1", "host_name.example-1:65535", "127.0.0.1:80", ".:1"}) {
    EXPECT_TRUE(validateSchemelessOrigin(valid)) << valid;
  }
  for (const char* invalid :
       {"", "host", ":80", "Host:80", "grpc://host:80", "user@host:80", "host:80/path", "host:80?x", "host:80#x",
        "[::1]:80", "::1:80", "host:080", "host:0", "host:65536", "host:+80", "host: 80", "host:80 "}) {
    EXPECT_FALSE(validateSchemelessOrigin(invalid)) << invalid;
  }
}

TEST(SourceLedger, WholeRequestPendingFailureAndCancellation) {
  IngestOutcomeLedger ledger({"a", "b"});
  EXPECT_EQ(ledger.outcome("unknown"), TopicOutcome::kPending);
  EXPECT_FALSE(ledger.record("unknown", TopicOutcome::kOk));
  EXPECT_EQ(ledger.computeCompletion(false).outcome, IngestOutcome::kFailed);
  ASSERT_TRUE(ledger.record("a", TopicOutcome::kOk));
  EXPECT_EQ(ledger.computeCompletion(false).outcome, IngestOutcome::kFailed);
  ASSERT_TRUE(ledger.record("b", TopicOutcome::kFailed));
  EXPECT_EQ(ledger.computeCompletion(false).outcome, IngestOutcome::kFailed);
  EXPECT_EQ(ledger.computeCompletion(true).outcome, IngestOutcome::kCancelled);
  ASSERT_TRUE(ledger.record("b", TopicOutcome::kEmptyOk));
  EXPECT_EQ(ledger.computeCompletion(false).outcome, IngestOutcome::kCompleted);
  EXPECT_EQ(ledger.computeCompletion(false).flags, PJ_INGEST_COMPLETION_FLAG_ATTESTS_EMPTY_TOPICS);
  EXPECT_EQ(ledger.computeCompletion(true).flags, PJ_INGEST_COMPLETION_FLAG_NONE);
  ASSERT_TRUE(ledger.record("b", TopicOutcome::kOk));
  EXPECT_EQ(ledger.computeCompletion(false).flags, PJ_INGEST_COMPLETION_FLAG_NONE);
  ASSERT_TRUE(ledger.record("a", TopicOutcome::kEmptyOk));
  ASSERT_TRUE(ledger.record("b", TopicOutcome::kEmptyOk));
  EXPECT_EQ(ledger.computeCompletion(false).outcome, IngestOutcome::kCompleted);
  // Structurally empty requests remain the host's cacheability policy.
  EXPECT_EQ(IngestOutcomeLedger({}).computeCompletion(false).outcome, IngestOutcome::kCompleted);
}

TEST(SourceStopPoller, LiveCheckCancelsOnceAndJoinFinishesCallback) {
  std::atomic<bool> stop{false};
  std::atomic<int> calls{0};
  std::promise<void> checked;
  std::promise<void> cancelled;
  bool first = true;
  StopPoller poller(
      [&] {
        if (first) {
          first = false;
          checked.set_value();
        }
        return stop.load();
      },
      [&] {
        ++calls;
        cancelled.set_value();
      },
      1ms);
  ASSERT_EQ(checked.get_future().wait_for(2s), std::future_status::ready);
  stop.store(true);
  EXPECT_EQ(cancelled.get_future().wait_for(2s), std::future_status::ready);
  poller.stopAndJoin();
  poller.stopAndJoin();
  EXPECT_EQ(calls.load(), 1);
}

TEST(SourceStopPoller, DestructionJoinsBeforeContextReleaseAndWakesLongWait) {
  auto context = std::make_shared<int>(0);
  std::weak_ptr<int> weak = context;
  std::promise<void> checked;
  {
    StopPoller poller(
        [context, &checked] {
          ++*context;
          checked.set_value();
          return false;
        },
        [] {}, 1h);
    ASSERT_EQ(checked.get_future().wait_for(2s), std::future_status::ready);
  }
  EXPECT_EQ(*context, 1);
  context.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_THROW(StopPoller({}, [] {}), std::invalid_argument);
  EXPECT_THROW(StopPoller([] { return false; }, {}), std::invalid_argument);
  EXPECT_THROW(StopPoller([] { return false; }, [] {}, 0ms), std::invalid_argument);
}

TEST(SourceTransferRate, MonotonicWindowCounterResetAndIdle) {
  RollingTransferRate rate;
  const RollingTransferRate::Clock::time_point start{};
  EXPECT_EQ(rate.bytesPerSecond(), 0.0);
  rate.add(100, start);
  rate.add(200, start);  // same timestamp is coalesced
  EXPECT_EQ(rate.bytesPerSecond(), 0.0);
  rate.add(400, start + 2s);
  EXPECT_DOUBLE_EQ(rate.bytesPerSecond(), 100.0);
  rate.add(900, start + 7s);
  EXPECT_DOUBLE_EQ(rate.bytesPerSecond(), 100.0);
  rate.add(900, start + 13s);
  EXPECT_EQ(rate.bytesPerSecond(), 0.0);
  rate.add(0, start + 14s);
  EXPECT_EQ(rate.bytesPerSecond(), 0.0);
  rate.add(100, start + 13s);
  EXPECT_EQ(rate.bytesPerSecond(), 0.0);
}
}  // namespace
}  // namespace PJ::sdk::source
