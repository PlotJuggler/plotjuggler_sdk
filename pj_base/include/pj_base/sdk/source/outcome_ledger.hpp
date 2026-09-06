// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/sdk/ingest_completion.hpp"

namespace PJ::sdk::source {

enum class TopicOutcome { kPending, kOk, kEmptyOk, kFailed };

struct IngestCompletion {
  IngestOutcome outcome = IngestOutcome::kFailed;
  PJ_ingest_completion_flags_t flags = PJ_INGEST_COMPLETION_FLAG_NONE;
};

/// Pure ledger over the ENTIRE declared request, including topics never visited.
/// Caller owns synchronization and declares the resolved topic set before ingest.
/// Attach canonical bytes once after context creation and before pushing; report
/// completion before closing the ingest bracket. A provider terminal must not
/// claim success if the batch's provisional dataset was discarded (e.g. all-empty
/// rollback), even when the ledger's ingest completion is COMPLETED.
class IngestOutcomeLedger {
 public:
  explicit IngestOutcomeLedger(const std::vector<std::string>& requested_topics);

  /// False for undeclared topics; they cannot change the declared request.
  [[nodiscard]] bool record(std::string_view topic, TopicOutcome outcome);
  /// Unknown topics default to kPending.
  [[nodiscard]] TopicOutcome outcome(std::string_view topic) const;

  /// cancelled must include a LIVE host isStopRequested() read at decision time,
  /// not just a poller's cached observation. Cancellation overrides every outcome.
  [[nodiscard]] IngestCompletion computeCompletion(bool cancelled) const;

 private:
  std::map<std::string, TopicOutcome, std::less<>> outcomes_;
};

}  // namespace PJ::sdk::source
