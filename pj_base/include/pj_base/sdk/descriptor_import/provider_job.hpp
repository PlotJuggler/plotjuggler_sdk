/**
 * @file provider_job.hpp
 * @brief The callee side of "pj.descriptor_import.v1": everything a provider
 *        plugin's start_import must get right about threads and the C ABI,
 *        implemented once.
 *
 * ProviderJob::start() runs a provider-supplied body on a worker thread and
 * owns the whole PJ_joinable_job_t contract around it: no callback before
 * start_import returns (a start gate), on_dataset zero-or-one, on_terminal
 * exactly-once and last, cancel() idempotent and non-blocking, join()
 * idempotent and safe under concurrent callers, destroy() = cancel + join +
 * free, a self-join from a job callback downgraded to a no-op. The body only
 * decides the outcome.
 *
 * Reading a start request and writing a query result under the struct_size
 * growth contract need no threads: they are header-only in
 * pj_base/sdk/descriptor_import.hpp (readDescriptorImportStartRequest,
 * writeDescriptorQueryResult), beside their consumer-side twins.
 *
 * What stays with the provider: trust policy, credential lookup, transfer
 * ceilings, the transport, artifact validation, promotion, outcome mapping.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

#include "pj_base/descriptor_import_protocol.h"

namespace PJ {
namespace sdk {
namespace descriptor_import {

namespace detail {
struct JobState;
}

/// The body's terminal result.
struct ImportOutcome {
  PJ_descriptor_import_outcome_t outcome = PJ_DESCRIPTOR_IMPORT_FAILED;
  std::string message;
};

/// The body's handle on its job, valid only for the body's duration.
class JobControl {
 public:
  /// [thread-safe] True once cancel() was requested. Poll it between blocking
  /// steps; a body that returns after a cancel should report CANCELLED.
  [[nodiscard]] bool isCancelled() const noexcept;

  /// [thread-safe] Register a hook run ONCE when cancellation arrives —
  /// immediately, on the registering thread, if it already has — so a body
  /// blocked in a transport call can be interrupted. Runs on the cancelling
  /// thread; must be non-blocking and must not call back into the job.
  void onCancel(std::function<void()> hook);

  /// [thread-safe] Deliver the ABI's on_dataset: zero-or-one per job; later
  /// calls are ignored. Must precede the dataset's first publication.
  void notifyDataset(PJ_data_source_handle_t dataset) noexcept;

  /// [thread-safe] Fire `on_expire` once if the body is still running after
  /// `timeout` (a duration ceiling — typically it cancels the transport, not
  /// the job, so the terminal can classify the cause). A non-positive timeout
  /// is ignored. The watchdog is stopped before on_terminal fires.
  void armWatchdog(std::chrono::milliseconds timeout, std::function<void()> on_expire);

 private:
  friend struct detail::JobState;
  explicit JobControl(detail::JobState& state) noexcept : state_(state) {}
  detail::JobState& state_;
};

class ProviderJob {
 public:
  /// Runs on the worker thread after the start gate opens. Exceptions are
  /// contained and reported as FAILED.
  using Body = std::function<ImportOutcome(JobControl&)>;

  /// Validate the callback surface (on_terminal is required, checked under
  /// the caller's struct_size), copy the callback pointers, spawn the gated
  /// worker, populate `out_job`, then release the gate as the last action.
  /// Returns false — with `out_error` filled, `out_job` untouched and nothing
  /// leaked — when the callbacks are unusable or the thread cannot start.
  /// Descriptor parsing, flags and credential resolution are the caller's
  /// job BEFORE calling this (see readDescriptorImportStartRequest).
  [[nodiscard]] static bool start(
      Body body, const PJ_descriptor_import_callbacks_v1_t* callbacks, void* callback_ctx, PJ_joinable_job_t* out_job,
      PJ_error_t* out_error);
};

/// A settle-exactly-once result box for an asynchronous step whose completion
/// callback may fire on any thread, re-entrantly, or after the waiter gave up
/// (e.g. a host's promotion result). Share it by shared_ptr with the callback
/// so a late settle can never touch freed memory.
class SettlementLatch {
 public:
  /// First call wins; later calls are ignored.
  void settle(bool ok, std::string detail);

  /// Block until settled, polling `cancelled` every `poll`. Returns true when
  /// settled; false when `cancelled()` became true first (the latch stays
  /// live and may still settle later). A settle that raced a cancel reports
  /// truthfully: settled wins.
  [[nodiscard]] bool wait(const std::function<bool()>& cancelled, std::chrono::milliseconds poll);

  [[nodiscard]] bool settled() const;
  [[nodiscard]] bool ok() const;
  [[nodiscard]] std::string detail() const;

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool settled_ = false;
  bool ok_ = false;
  std::string detail_;
};

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
