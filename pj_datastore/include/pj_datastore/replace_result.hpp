#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// Outcome of DataEngine::replaceDatasetFrom(): which primary topics had their
/// data swapped, which were created fresh, and which were retired (their source
/// topic vanished from the new data). All ids are PRIMARY engine ids — the whole
/// point of replace is that they stay stable so curve keys do not change.
///
/// (ObjectStore has the parallel ObjectDatasetReplaceResult, declared in
/// object_store.hpp next to ObjectTopicId.)
struct DatasetReplaceResult {
  /// Primary topics whose chunks were replaced (topic name matched staged).
  std::vector<TopicId> replaced_topics;
  /// Primary topics newly created (name existed in staged but not primary).
  std::vector<TopicId> added_topics;
  /// Primary topics with no staged match: chunks cleared and the id retired
  /// (excluded from listTopics; getTopicStorage still returns an empty storage
  /// so any in-flight reader sees an empty deque rather than dangling).
  /// Observability only — the engine self-hides these via listTopics, so callers
  /// need not act on them (the catalog rebuild drops them on its own).
  std::vector<TopicId> retired_topics;
};

}  // namespace PJ
