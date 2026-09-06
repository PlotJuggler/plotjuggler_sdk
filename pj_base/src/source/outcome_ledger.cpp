// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/source/outcome_ledger.hpp"

namespace PJ::sdk::source {

IngestOutcomeLedger::IngestOutcomeLedger(const std::vector<std::string>& requested_topics) {
  for (const auto& topic : requested_topics) {
    outcomes_.emplace(topic, TopicOutcome::kPending);
  }
}

bool IngestOutcomeLedger::record(std::string_view topic, TopicOutcome topic_outcome) {
  const auto found = outcomes_.find(topic);
  if (found == outcomes_.end()) {
    return false;
  }
  found->second = topic_outcome;
  return true;
}

TopicOutcome IngestOutcomeLedger::outcome(std::string_view topic) const {
  const auto found = outcomes_.find(topic);
  return found == outcomes_.end() ? TopicOutcome::kPending : found->second;
}

IngestCompletion IngestOutcomeLedger::computeCompletion(bool cancelled) const {
  if (cancelled) {
    return {IngestOutcome::kCancelled, PJ_INGEST_COMPLETION_FLAG_NONE};
  }
  PJ_ingest_completion_flags_t flags = PJ_INGEST_COMPLETION_FLAG_NONE;
  for (const auto& [topic, topic_outcome] : outcomes_) {
    switch (topic_outcome) {
      case TopicOutcome::kOk:
        break;
      case TopicOutcome::kEmptyOk:
        flags |= PJ_INGEST_COMPLETION_FLAG_ATTESTS_EMPTY_TOPICS;
        break;
      default:
        return {};
    }
  }
  return {IngestOutcome::kCompleted, flags};
}

}  // namespace PJ::sdk::source
