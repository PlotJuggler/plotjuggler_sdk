// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/descriptor_import/provider_job.hpp"

#include <atomic>
#include <cstddef>
#include <semaphore>
#include <thread>
#include <utility>

#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/testing/provider_job_probe.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {

namespace detail {

struct JobState {
  ProviderJob::Body body;
  decltype(PJ_descriptor_import_callbacks_v1_t::on_dataset) on_dataset = nullptr;
  decltype(PJ_descriptor_import_callbacks_v1_t::on_terminal) on_terminal = nullptr;
  void* callback_ctx = nullptr;

  // The worker's FIRST action is start_gate.acquire(): the ABI forbids any job
  // callback before start_import returns, and start() releases the gate as
  // its last action, after out_job is fully populated.
  std::binary_semaphore start_gate{0};
  std::atomic<bool> cancelled{false};
  std::atomic<bool> dataset_notified{false};
  std::atomic<bool> terminal_fired{false};

  std::thread worker;
  // Immutable after start(): the id for the self-join guard — reading
  // worker.get_id() during a concurrent join() would race the join's
  // modification of the thread object.
  std::thread::id worker_id;

  // join()-state: the ABI slot is [thread-safe]/idempotent, but
  // std::thread::join is neither — exactly ONE caller performs the join,
  // every other caller waits for `joined`.
  std::mutex join_mu;
  std::condition_variable join_cv;
  bool join_in_progress = false;
  bool joined = false;

  std::mutex hook_mu;
  std::function<void()> cancel_hook;
  bool hook_fired = false;
  bool hook_running = false;  ///< a requestCancel() is inside the hook right now
  std::condition_variable hook_cv;

  std::mutex watchdog_mu;
  std::condition_variable watchdog_cv;
  bool watchdog_stop = false;
  std::thread watchdog;

  // [thread-safe] Idempotent, non-blocking: flag + the registered hook once.
  // The hook can be reached from the ABI's noexcept cancel slot, so its
  // exceptions are contained here; disarmHook() waits out an invocation in
  // flight before the body's locals die.
  void requestCancel() {
    cancelled.store(true, std::memory_order_release);
    std::function<void()> hook;
    {
      std::lock_guard<std::mutex> lock(hook_mu);
      if (!hook_fired && cancel_hook) {
        hook_fired = true;
        hook_running = true;
        hook = std::move(cancel_hook);
      }
    }
    if (hook) {
      try {
        hook();
      } catch (...) {}
      {
        std::lock_guard<std::mutex> lock(hook_mu);
        hook_running = false;
      }
      hook_cv.notify_all();
    }
  }

  // Called once the body has returned: a hook that captured body-locals must
  // never run again (destroy() cancels unconditionally, even after success),
  // and one already running is waited out so those locals outlive it.
  void disarmHook() {
    std::unique_lock<std::mutex> lock(hook_mu);
    cancel_hook = nullptr;
    hook_fired = true;
    hook_cv.wait(lock, [this] { return !hook_running; });
  }

  [[nodiscard]] bool isCancelled() const noexcept {
    return cancelled.load(std::memory_order_acquire);
  }

  void stopWatchdog() {
    {
      std::lock_guard<std::mutex> lock(watchdog_mu);
      watchdog_stop = true;
    }
    watchdog_cv.notify_all();
    if (watchdog.joinable()) {
      watchdog.join();
    }
  }

  // Exactly-once terminal (defensive flag; run() fires it once).
  void fireTerminal(PJ_descriptor_import_outcome_t outcome, const std::string& message) noexcept {
    if (terminal_fired.exchange(true)) {
      return;
    }
    if (on_terminal != nullptr) {
      on_terminal(callback_ctx, outcome, toAbiString(message));
    }
  }

  void run() noexcept {
    start_gate.acquire();
    ImportOutcome result;
    if (isCancelled()) {
      result.outcome = PJ_DESCRIPTOR_IMPORT_CANCELLED;
      result.message = "import cancelled";
    } else {
      try {
        JobControl control(*this);
        result = body(control);
      } catch (...) {
        result.outcome = PJ_DESCRIPTOR_IMPORT_FAILED;
        result.message = "internal error while running the import";
      }
    }
    // The body is done and its locals are dying: the cancel hook is disarmed
    // (and an in-flight invocation waited out) before anything else happens.
    try {
      disarmHook();
    } catch (...) {}
    // A watchdog must never fire after (or during) the terminal.
    try {
      stopWatchdog();
    } catch (...) {}
    fireTerminal(result.outcome, result.message);
  }
};

}  // namespace detail

// ---------------------------------------------------------------------------
// JobControl
// ---------------------------------------------------------------------------

bool JobControl::isCancelled() const noexcept {
  return state_.isCancelled();
}

void JobControl::onCancel(std::function<void()> hook) {
  if (!hook) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(state_.hook_mu);
    if (!state_.isCancelled()) {
      state_.cancel_hook = std::move(hook);
      return;
    }
    if (state_.hook_fired) {
      return;
    }
    state_.hook_fired = true;
  }
  try {
    hook();  // already cancelled: fire now, outside the lock
  } catch (...) {}
}

void JobControl::notifyDataset(PJ_data_source_handle_t dataset) noexcept {
  if (state_.dataset_notified.exchange(true)) {
    return;
  }
  if (state_.on_dataset != nullptr) {
    state_.on_dataset(state_.callback_ctx, dataset);
  }
}

void JobControl::armWatchdog(std::chrono::milliseconds timeout, std::function<void()> on_expire) {
  if (timeout.count() <= 0 || !on_expire) {
    return;
  }
  // Re-arming from inside on_expire would join the watchdog thread from
  // itself: downgraded to a no-op (armWatchdog is body-thread-only).
  if (state_.watchdog.joinable() && state_.watchdog.get_id() == std::this_thread::get_id()) {
    return;
  }
  state_.stopWatchdog();
  {
    std::lock_guard<std::mutex> lock(state_.watchdog_mu);
    state_.watchdog_stop = false;
  }
  detail::JobState* state = &state_;
  std::binary_semaphore started{0};
  state_.watchdog = std::thread([state, timeout, on_expire = std::move(on_expire), &started]() {
    std::unique_lock<std::mutex> lock(state->watchdog_mu);
    started.release();
    const bool stopped = state->watchdog_cv.wait_for(lock, timeout, [state] { return state->watchdog_stop; });
    lock.unlock();
    if (!stopped) {
      try {
        on_expire();
      } catch (...) {}
    }
  });
  started.acquire();
}

// ---------------------------------------------------------------------------
// The job vtable trio
// ---------------------------------------------------------------------------

namespace {

void jobCancel(void* ctx) noexcept {
  if (ctx != nullptr) {
    static_cast<detail::JobState*>(ctx)->requestCancel();
  }
}

void jobJoin(void* ctx) noexcept {
  if (ctx == nullptr) {
    return;
  }
  auto* state = static_cast<detail::JobState*>(ctx);
  // A job callback joining its own thread is deadlock-or-terminate; the ABI
  // forbids it, and this guard downgrades a violation to a no-op.
  if (state->worker_id == std::this_thread::get_id()) {
    return;
  }
  try {
    std::unique_lock<std::mutex> lock(state->join_mu);
    if (state->joined) {
      return;
    }
    if (state->join_in_progress) {
      state->join_cv.wait(lock, [state] { return state->joined; });
      return;
    }
    state->join_in_progress = true;
    lock.unlock();
    try {
      if (state->worker.joinable()) {
        state->worker.join();
      }
    } catch (...) {
      // The thread either finished or the join failed; the terminal contract
      // is run()'s, not join()'s.
    }
    lock.lock();
    state->joined = true;
    state->join_cv.notify_all();
  } catch (...) {
    // Lock/wait failure: nothing safe left to do inside noexcept.
  }
}

// A job only exists after start() returned true, and start() released the
// gate before returning, so cancel + join here can never wait on it.
void jobDestroy(void* ctx) noexcept {
  if (ctx == nullptr) {
    return;
  }
  jobCancel(ctx);
  jobJoin(ctx);
  delete static_cast<detail::JobState*>(ctx);
}

constexpr PJ_joinable_job_vtable_t kJobVtable{sizeof(PJ_joinable_job_vtable_t), 0, &jobCancel, &jobJoin, &jobDestroy};

constexpr const char* kErrorDomain = "descriptor_import";

bool startJob(
    ProviderJob::Body body, const PJ_descriptor_import_callbacks_v1_t* callbacks, void* callback_ctx,
    PJ_joinable_job_t* out_job, PJ_error_t* out_error, const std::function<void()>& before_gate_release) {
  if (out_job == nullptr) {
    fillError(out_error, 1, kErrorDomain, "null out_job");
    return false;
  }
  if (!body) {
    fillError(out_error, 1, kErrorDomain, "a job body is required");
    return false;
  }
  const bool terminal_covered =
      callbacks != nullptr && fieldCovered(
                                  callbacks->struct_size, offsetof(PJ_descriptor_import_callbacks_v1_t, on_terminal),
                                  sizeof(callbacks->on_terminal));
  if (!terminal_covered || callbacks->on_terminal == nullptr) {
    fillError(out_error, 1, kErrorDomain, "on_terminal callback is required");
    return false;
  }

  auto* state = new detail::JobState();
  state->body = std::move(body);
  state->on_terminal = callbacks->on_terminal;
  if (fieldCovered(
          callbacks->struct_size, offsetof(PJ_descriptor_import_callbacks_v1_t, on_dataset),
          sizeof(callbacks->on_dataset))) {
    state->on_dataset = callbacks->on_dataset;  // may be null (zero-or-one)
  }
  state->callback_ctx = callback_ctx;

  // Spawn the GATED worker first (its first action is start_gate.acquire(),
  // so it cannot touch anything before the release below): a thread-spawn
  // failure must return false with out_job UNTOUCHED and no leaked state.
  try {
    state->worker = std::thread([state]() { state->run(); });
  } catch (...) {
    delete state;
    fillError(out_error, 1, kErrorDomain, "could not start the import worker thread");
    return false;
  }
  state->worker_id = state->worker.get_id();

  out_job->ctx = state;
  out_job->vtable = &kJobVtable;

  if (before_gate_release) {
    try {
      before_gate_release();
    } catch (...) {
      // A test probe must never leave the worker parked forever.
    }
  }
  // Released only now — after out_job is fully populated and this call is
  // about to return — so the worker's FIRST action cannot proceed earlier.
  state->start_gate.release();
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ProviderJob
// ---------------------------------------------------------------------------

bool ProviderJob::start(
    Body body, const PJ_descriptor_import_callbacks_v1_t* callbacks, void* callback_ctx, PJ_joinable_job_t* out_job,
    PJ_error_t* out_error) {
  return startJob(std::move(body), callbacks, callback_ctx, out_job, out_error, {});
}

namespace testing {

bool startWithGateProbe(
    ProviderJob::Body body, const PJ_descriptor_import_callbacks_v1_t* callbacks, void* callback_ctx,
    PJ_joinable_job_t* out_job, PJ_error_t* out_error, std::function<void()> before_gate_release) {
  return startJob(std::move(body), callbacks, callback_ctx, out_job, out_error, before_gate_release);
}

}  // namespace testing

// ---------------------------------------------------------------------------
// SettlementLatch
// ---------------------------------------------------------------------------

void SettlementLatch::settle(bool ok, std::string detail) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (settled_) {
      return;
    }
    settled_ = true;
    ok_ = ok;
    detail_ = std::move(detail);
  }
  cv_.notify_all();
}

bool SettlementLatch::wait(const std::function<bool()>& cancelled, std::chrono::milliseconds poll) {
  std::unique_lock<std::mutex> lock(mu_);
  for (;;) {
    if (settled_) {
      return true;
    }
    if (cancelled && cancelled()) {
      return false;
    }
    cv_.wait_for(lock, poll);
  }
}

bool SettlementLatch::settled() const {
  std::lock_guard<std::mutex> lock(mu_);
  return settled_;
}

bool SettlementLatch::ok() const {
  std::lock_guard<std::mutex> lock(mu_);
  return ok_;
}

std::string SettlementLatch::detail() const {
  std::lock_guard<std::mutex> lock(mu_);
  return detail_;
}

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
