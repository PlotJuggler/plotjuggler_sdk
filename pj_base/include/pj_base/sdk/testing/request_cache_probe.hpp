/**
 * @file request_cache_probe.hpp
 * @brief Test-only entry point for RequestArtifactCache cleanup races.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>

#include "pj_base/sdk/descriptor_import/request_cache.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace testing {

/// Same contract as RequestArtifactCache::cleanup(); the probe runs after the
/// directory scan and before stale-partial removal and artifact eviction. A
/// throwing probe is ignored so cleanup still completes.
[[nodiscard]] CleanupResult cleanupWithProbe(
    RequestArtifactCache& cache, const CleanupPolicy& policy, std::function<void()> between_scan_and_evict);

}  // namespace testing
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
