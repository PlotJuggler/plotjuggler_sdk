/**
 * @file provider_job_probe.hpp
 * @brief Test-only entry point for ProviderJob: start a job with a probe that
 *        runs after out_job is populated and the gated worker exists, BEFORE
 *        the gate is released — the window in which the ABI forbids any job
 *        callback. Production code calls ProviderJob::start().
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>

#include "pj_base/sdk/descriptor_import/provider_job.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace testing {

/// Same contract as ProviderJob::start(); `before_gate_release` runs inside
/// the call with the worker parked. A throwing probe still releases the gate.
[[nodiscard]] bool startWithGateProbe(
    ProviderJob::Body body, const PJ_descriptor_import_callbacks_v1_t* callbacks, void* callback_ctx,
    PJ_joinable_job_t* out_job, PJ_error_t* out_error, std::function<void()> before_gate_release);

}  // namespace testing
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
