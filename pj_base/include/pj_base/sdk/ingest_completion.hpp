/**
 * @file ingest_completion.hpp
 * @brief C++ value types and the shared fail-closed validation for the
 *        complete_ingest terminal (PJ_ingest_completion_t).
 *
 * Hosts and tests share ONE validator so "fails closed for caching" means the
 * same thing everywhere: an undersized struct, unknown outcome, nonzero
 * flags, or malformed topic list makes the completion unusable as capture
 * evidence — it never affects the ingest itself.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"

namespace PJ {
namespace sdk {

/// C++ twin of PJ_ingest_outcome_t.
enum class IngestOutcome : int32_t {
  kFailed = PJ_INGEST_FAILED,
  kCancelled = PJ_INGEST_CANCELLED,
  kCompleted = PJ_INGEST_COMPLETED,
};

/// Host-owned copy of a validated completion (the C struct borrows its list).
struct IngestCompletionRecord {
  IngestOutcome outcome = IngestOutcome::kFailed;
  std::vector<std::string> requested_topics;
};

/// Topic-list bound: a request declaring more topics than this is refused as
/// capture evidence (structural sanity, not a product limit).
inline constexpr uint64_t kMaxIngestCompletionTopics = 100'000;

/// Validate a borrowed completion and copy it into host-owned storage.
/// Fail-closed rules (the caching verdict, never an ingest error):
///  - struct_size must cover the v1 fields; larger (newer) structs are fine —
///    only the known fields are read.
///  - outcome must be a known PJ_ingest_outcome_t value.
///  - flags must be PJ_INGEST_COMPLETION_FLAG_NONE (no v1 flags exist).
///  - the topic list must be self-consistent: a null pointer with a nonzero
///    count, an over-bound count, an empty or duplicate topic name all refuse.
///  - an empty list is structurally VALID (the empty-topic cacheability rule
///    is the capture service's policy, not this validator's).
[[nodiscard]] inline Expected<IngestCompletionRecord> copyIngestCompletion(const PJ_ingest_completion_t* completion) {
  if (completion == nullptr) {
    return unexpected(std::string("completion is null"));
  }
  if (completion->struct_size < sizeof(PJ_ingest_completion_t)) {
    return unexpected(std::string("completion struct_size is smaller than the v1 layout"));
  }
  switch (completion->outcome) {
    case PJ_INGEST_FAILED:
    case PJ_INGEST_CANCELLED:
    case PJ_INGEST_COMPLETED:
      break;
    default:
      return unexpected(std::string("unknown ingest outcome"));
  }
  if (completion->flags != PJ_INGEST_COMPLETION_FLAG_NONE) {
    return unexpected(std::string("unknown completion flags"));
  }
  if (completion->requested_topics == nullptr && completion->requested_topic_count != 0) {
    return unexpected(std::string("null topic list with nonzero count"));
  }
  if (completion->requested_topic_count > kMaxIngestCompletionTopics) {
    return unexpected(std::string("requested topic count exceeds the structural bound"));
  }

  IngestCompletionRecord record;
  record.outcome = static_cast<IngestOutcome>(completion->outcome);
  record.requested_topics.reserve(static_cast<size_t>(completion->requested_topic_count));
  std::unordered_set<std::string_view> seen;
  seen.reserve(static_cast<size_t>(completion->requested_topic_count));
  for (uint64_t index = 0; index < completion->requested_topic_count; ++index) {
    const PJ_string_view_t& name = completion->requested_topics[index];
    if (name.data == nullptr || name.size == 0) {
      return unexpected(std::string("empty topic name in the requested set"));
    }
    const std::string_view view(name.data, name.size);
    if (!seen.insert(view).second) {
      return unexpected(std::string("duplicate topic name in the requested set: ") + std::string(view));
    }
    record.requested_topics.emplace_back(view);
  }
  return record;
}

}  // namespace sdk
}  // namespace PJ
