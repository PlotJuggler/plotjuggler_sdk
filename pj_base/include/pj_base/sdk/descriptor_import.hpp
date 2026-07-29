/**
 * @file descriptor_import.hpp
 * @brief C++ wrappers for the descriptor-import v1 extension + service pair:
 * DescriptorImportProviderView, the RAII JoinableJob, SourcePromotionHostView,
 * and their supporting value types. See pj_base/descriptor_import_protocol.h
 * for the full ABI/lifetime/threading contract this header wraps.
 *
 * Two halves, in order:
 *   - Extension CONSUMER half: a host reading a plugin's
 *     "pj.descriptor_import.v1" extension (DescriptorImportProviderView +
 *     JoinableJob).
 *   - Service half: a provider plugin consuming the host's
 *     "pj.source_promotion.v1" service via the bind() service registry
 *     (SourcePromotionHostView + the PJ::sdk::SourcePromotionHostService
 *     trait).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "pj_base/descriptor_import_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/types.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Extension consumer half: "pj.descriptor_import.v1"
// A host reading a plugin's extension, obtained from that plugin family's
// get_plugin_extension() hook.
// ---------------------------------------------------------------------------

/// Fail-closed C++ mirror of PJ_descriptor_trust_t. Unknown/future values map
/// to kRefused (see PJ_descriptor_trust_t doc-comment). Initialized directly
/// from the C enumerators so the mirror can never drift.
enum class DescriptorTrust : int32_t {
  kRefused = PJ_DESCRIPTOR_TRUST_REFUSED,
  kNeedsConfirmation = PJ_DESCRIPTOR_TRUST_NEEDS_CONFIRMATION,
  kTrusted = PJ_DESCRIPTOR_TRUST_TRUSTED,
};

/// Fail-closed C++ mirror of PJ_descriptor_import_outcome_t. Unknown/future
/// values map to kFailed. Initialized directly from the C enumerators so the
/// mirror can never drift.
enum class DescriptorImportOutcome : int32_t {
  kFailed = PJ_DESCRIPTOR_IMPORT_FAILED,
  kCancelled = PJ_DESCRIPTOR_IMPORT_CANCELLED,
  kSucceededEagerOnly = PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY,
  kSucceededPromoted = PJ_DESCRIPTOR_IMPORT_SUCCEEDED_PROMOTED,
};

/// queryDescriptor result, copied immediately out of the C ABI's borrowed
/// views (which are only valid until the next query_descriptor call on the
/// same plugin instance — see the protocol header's lifetime contract).
struct DescriptorQueryResult {
  DescriptorTrust trust = DescriptorTrust::kRefused;
  bool is_materialized = false;
  std::string source_identity;
  std::string local_path_utf8;
  std::string message;
  uint64_t estimated_bytes = 0;  ///< 0 = unknown
};

struct DescriptorImportStartRequest {
  std::string descriptor_json;
  uint64_t flags = PJ_DESCRIPTOR_IMPORT_START_FLAG_NONE;
  uint64_t max_transfer_bytes = 0;  ///< 0 = no caller ceiling
};

/// RAII owner of a PJ_joinable_job_t plus the callback closures backing it.
/// Move-only.
///
/// The destructor (and any reset via move-assignment) destroys the job
/// (cancel+join) BEFORE releasing the closures, so a late callback can never
/// touch freed state. NEVER let a JoinableJob be destroyed, reset, or
/// assigned-over from inside one of its own callbacks (on_dataset/
/// on_terminal) — destroy()/join() block until the callback thread quiesces,
/// so doing so from that same thread is a self-join deadlock (or, per the
/// underlying pthread/std::thread implementation, a terminate()). Discarding
/// the Expected<JoinableJob> returned by startImport (e.g. ignoring the
/// return value) destroys it immediately, which cancels the import.
///
/// cancel() and join() are thread-safe with respect to the underlying JOB
/// (the provider's vtable slots are documented [thread-safe] /
/// [blocking, not-callback-thread]), but the C++ handle itself is NOT
/// synchronized: the owner must keep the JoinableJob alive for the duration
/// of any concurrent cancel()/join() call (e.g. via shared_ptr<JoinableJob>
/// when multiple threads may reach for it) and must never race a call
/// against this object's own destruction or move-assignment.
class JoinableJob {
 public:
  JoinableJob() = default;

  JoinableJob(const JoinableJob&) = delete;
  JoinableJob& operator=(const JoinableJob&) = delete;

  JoinableJob(JoinableJob&& other) noexcept : job_(other.job_), callback_ctx_(std::move(other.callback_ctx_)) {
    other.job_ = PJ_joinable_job_t{};
  }

  JoinableJob& operator=(JoinableJob&& other) noexcept {
    if (this != &other) {
      reset();
      job_ = other.job_;
      callback_ctx_ = std::move(other.callback_ctx_);
      other.job_ = PJ_joinable_job_t{};
    }
    return *this;
  }

  ~JoinableJob() {
    reset();
  }

  [[nodiscard]] bool valid() const noexcept {
    return job_.vtable != nullptr;
  }

  /// [thread-safe] Non-blocking, idempotent, best-effort cancellation. See
  /// the class doc-comment for the synchronization contract on this handle.
  void cancel() const noexcept {
    if (valid() && hasCancel(job_.vtable)) {
      job_.vtable->cancel(job_.ctx);
    }
  }

  /// [blocking, not-callback-thread] Idempotent. Returns after on_terminal
  /// has returned.
  void join() const noexcept {
    if (valid() && hasJoin(job_.vtable)) {
      job_.vtable->join(job_.ctx);
    }
  }

 private:
  friend class DescriptorImportProviderView;

  /// Callback closures plus a dispatch guard that lets a caller who has
  /// decided this context can never be safely freed (an ABI-violating job —
  /// see DescriptorImportProviderView::startImport) permanently disable
  /// dispatch through it instead.
  ///
  /// The guard is a SINGLE combined atomic — bit 31 (kDisabledBit) is the
  /// disabled flag, bits 0..30 are the in-flight dispatch count — not two
  /// independent atomics. That's required for correctness, not just
  /// tidiness: with two independent atomics, a thunk's fetch_add(in_flight)
  /// and quiesce()'s disabled.store() are unrelated writes to unrelated
  /// objects, so on a weak memory model each side's corresponding load can
  /// observe a stale value from before the other side's write — the thunk
  /// reads disabled==false while quiesce() concurrently reads
  /// in_flight==0 — letting quiesce() return while the thunk goes on to
  /// invoke the user's closure (a classic store-buffering hazard; no
  /// happens-before relation forms between two independently-ordered
  /// atomics like that). Packing both into one atomic closes this: the
  /// thunk's entry fetch_add and quiesce()'s fetch_or are read-modify-write
  /// operations on the SAME atomic object, and the standard guarantees all
  /// RMW operations on one atomic object are related by that object's
  /// single total modification order. So either the fetch_add precedes the
  /// fetch_or — in which case quiesce()'s wait loop cannot observe the
  /// count back at zero until the thunk's closing fetch_sub, whose release
  /// synchronizes-with the wait loop's acquire load, making the closure's
  /// completion happen-before quiesce() returns — or the fetch_add follows
  /// the fetch_or, in which case the value the thunk reads already carries
  /// kDisabledBit and it backs out without ever touching the closure. No
  /// interleaving lets quiesce() return while a dispatch is pending or in
  /// flight.
  struct CallbackContext {
    std::function<void(DatasetId)> on_dataset;
    std::function<void(DescriptorImportOutcome, std::string)> on_terminal;

    static constexpr uint32_t kDisabledBit = 0x80000000u;
    std::atomic<uint32_t> dispatch_state{0};
  };

  JoinableJob(PJ_joinable_job_t job, std::unique_ptr<CallbackContext> callback_ctx)
      : job_(job), callback_ctx_(std::move(callback_ctx)) {}

  /// Tail-slot capacity check mirroring PJ_HAS_TAIL_SLOT, applied to
  /// PJ_joinable_job_vtable_t's cancel/join/destroy members.
  static bool hasSlot(const PJ_joinable_job_vtable_t* vt, std::size_t offset, std::size_t size) noexcept {
    return vt->struct_size >= offset + size;
  }

  static bool hasCancel(const PJ_joinable_job_vtable_t* vt) noexcept {
    return hasSlot(vt, offsetof(PJ_joinable_job_vtable_t, cancel), sizeof(vt->cancel)) && vt->cancel != nullptr;
  }

  static bool hasJoin(const PJ_joinable_job_vtable_t* vt) noexcept {
    return hasSlot(vt, offsetof(PJ_joinable_job_vtable_t, join), sizeof(vt->join)) && vt->join != nullptr;
  }

  static bool hasDestroy(const PJ_joinable_job_vtable_t* vt) noexcept {
    return hasSlot(vt, offsetof(PJ_joinable_job_vtable_t, destroy), sizeof(vt->destroy)) && vt->destroy != nullptr;
  }

  /// Permanently disables ctx's thunks, then blocks until any dispatch
  /// already accounted for in ctx's dispatch_state has finished. See
  /// CallbackContext's doc-comment for why the single combined atomic makes
  /// this correct under a weak memory model. Once this returns, ctx's
  /// on_dataset/on_terminal are guaranteed to never run again — safe to free
  /// the closures (or leak the allocation) afterwards. Only ever called from
  /// startImport's ABI-violation path or reset()'s defensive leak branch
  /// below; NEVER call it from inside one of ctx's own callbacks (that
  /// thread would be waiting on itself).
  static void quiesce(CallbackContext& ctx) noexcept {
    ctx.dispatch_state.fetch_or(CallbackContext::kDisabledBit, std::memory_order_acq_rel);
    while ((ctx.dispatch_state.load(std::memory_order_acquire) & ~CallbackContext::kDisabledBit) != 0) {
      std::this_thread::yield();
    }
  }

  /// Destroy the job (cancel+join if necessary) BEFORE releasing the
  /// closures it may still be calling into. Idempotent.
  void reset() noexcept {
    if (valid()) {
      if (hasDestroy(job_.vtable)) {
        job_.vtable->destroy(job_.ctx);
      } else {
        // A job with no usable destroy slot can't be safely stopped: its
        // callback thread may still call into callback_ctx_ at any future
        // time, and we have no way to join it. Quiesce (permanently disable
        // dispatch, waiting out anything already in flight) so the closures
        // can never run again, THEN leak the allocation rather than risk a
        // use-after-free — this only happens when a provider violates the
        // ABI (see DescriptorImportProviderView::startImport, which handles
        // the common case of this directly; this branch is defense in depth
        // for any other path that might construct a JoinableJob).
        if (callback_ctx_) {
          quiesce(*callback_ctx_);
        }
        (void)callback_ctx_.release();
      }
    }
    job_ = PJ_joinable_job_t{};
    callback_ctx_.reset();
  }

  PJ_joinable_job_t job_{};
  std::unique_ptr<CallbackContext> callback_ctx_;
};

/// Typed consumer of the "pj.descriptor_import.v1" extension.
///
/// Non-owning: this view is just a (pointer, ctx) pair borrowed from the
/// plugin instance and must not outlive it. Construct from the raw pointer
/// returned by get_plugin_extension / pluginExtension plus the SAME
/// plugin-instance ctx that hook was called with. Per the protocol header,
/// the plugin instance and its DSO must stay alive until every job obtained
/// from it has been destroyed — this view holds no keep-alive of its own, so
/// hosts must independently hold their ToolboxHandle/library owner for as
/// long as any JoinableJob obtained through it is still alive.
class DescriptorImportProviderView {
 public:
  DescriptorImportProviderView() = default;

  DescriptorImportProviderView(const void* extension, void* plugin_ctx)
      : ext_(static_cast<const PJ_descriptor_import_provider_v1_t*>(extension)), plugin_ctx_(plugin_ctx) {}

  [[nodiscard]] bool valid() const noexcept {
    return ext_ != nullptr &&
           ext_->struct_size >=
               offsetof(PJ_descriptor_import_provider_v1_t, start_import) + sizeof(ext_->start_import) &&
           ext_->query_descriptor != nullptr && ext_->start_import != nullptr;
  }

  /// [main-thread, strictly bounded] See PJ_descriptor_import_provider_v1_t::query_descriptor.
  [[nodiscard]] Expected<DescriptorQueryResult> queryDescriptor(std::string_view descriptor_json) const {
    if (!valid()) {
      return unexpected("descriptor import provider extension is not available");
    }
    PJ_descriptor_query_result_v1_t raw{};
    raw.struct_size = sizeof(raw);
    PJ_error_t err{};
    if (!ext_->query_descriptor(plugin_ctx_, sdk::toAbiString(descriptor_json), &raw, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    DescriptorQueryResult result;
    switch (raw.trust) {
      case PJ_DESCRIPTOR_TRUST_NEEDS_CONFIRMATION:
        result.trust = DescriptorTrust::kNeedsConfirmation;
        break;
      case PJ_DESCRIPTOR_TRUST_TRUSTED:
        result.trust = DescriptorTrust::kTrusted;
        break;
      case PJ_DESCRIPTOR_TRUST_REFUSED:
      default:
        result.trust = DescriptorTrust::kRefused;
        break;
    }
    result.is_materialized = raw.is_materialized != 0;
    result.source_identity = std::string(sdk::toStringView(raw.source_identity));
    result.local_path_utf8 = std::string(sdk::toStringView(raw.local_path_utf8));
    result.message = std::string(sdk::toStringView(raw.message));
    result.estimated_bytes = raw.estimated_bytes;
    return result;
  }

  /// [main-thread] See PJ_descriptor_import_provider_v1_t::start_import. On
  /// success, on_dataset fires zero-or-one time and on_terminal fires
  /// exactly once, both on a job-callback thread — never before this call
  /// returns. @p on_dataset and @p on_terminal must not throw: an escaping
  /// exception is swallowed at the ABI boundary (see onTerminalThunk), and
  /// for on_terminal specifically this means the completion notification is
  /// lost — the caller will never observe that import's outcome.
  ///
  /// On an ERROR return, @p on_dataset and @p on_terminal are guaranteed to
  /// never be invoked — including when the error comes from a provider that
  /// returned true from the underlying ABI call but handed back a job this
  /// wrapper cannot safely stop: such a job's callback context is quiesced
  /// (permanently disabled, waiting out anything already in flight) before
  /// this function returns, so the caller may safely tear down whatever its
  /// closures captured by reference as soon as it sees the error.
  [[nodiscard]] Expected<JoinableJob> startImport(
      const DescriptorImportStartRequest& request, std::function<void(DatasetId)> on_dataset,
      std::function<void(DescriptorImportOutcome, std::string)> on_terminal) const {
    if (!valid()) {
      return unexpected("descriptor import provider extension is not available");
    }

    PJ_descriptor_import_start_request_v1_t raw_request{};
    raw_request.struct_size = sizeof(raw_request);
    raw_request.descriptor_json = sdk::toAbiString(request.descriptor_json);
    raw_request.flags = request.flags;
    raw_request.max_transfer_bytes = request.max_transfer_bytes;

    PJ_descriptor_import_callbacks_v1_t raw_callbacks{};
    raw_callbacks.struct_size = sizeof(raw_callbacks);
    raw_callbacks.on_dataset = &DescriptorImportProviderView::onDatasetThunk;
    raw_callbacks.on_terminal = &DescriptorImportProviderView::onTerminalThunk;

    // CallbackContext is not movable (it holds atomics — see the class
    // doc-comment), so construct it in place and assign the closures rather
    // than move a temporary into make_unique.
    auto callback_ctx = std::make_unique<JoinableJob::CallbackContext>();
    callback_ctx->on_dataset = std::move(on_dataset);
    callback_ctx->on_terminal = std::move(on_terminal);

    PJ_joinable_job_t raw_job{};
    PJ_error_t err{};
    if (!ext_->start_import(plugin_ctx_, &raw_request, &raw_callbacks, callback_ctx.get(), &raw_job, &err)) {
      return unexpected(sdk::errorToString(err));
    }
    // Contract check (PJ_descriptor_import_provider_v1_t::start_import):
    // "On true: out_job is valid". Validate ALL THREE vtable slots the ABI
    // promises, not just destroy: a job with a usable destroy but a missing
    // cancel/join would otherwise construct successfully and then break
    // join()'s "returns after on_terminal has returned" contract silently.
    const bool has_vtable = raw_job.vtable != nullptr;
    const bool destroy_ok = has_vtable && JoinableJob::hasDestroy(raw_job.vtable);
    const bool cancel_ok = has_vtable && JoinableJob::hasCancel(raw_job.vtable);
    const bool join_ok = has_vtable && JoinableJob::hasJoin(raw_job.vtable);

    if (destroy_ok && cancel_ok && join_ok) {
      return JoinableJob(raw_job, std::move(callback_ctx));
    }

    if (destroy_ok) {
      // destroy() itself cancels+joins per the ABI, so calling it here fully
      // quiesces the callback stream before we return — the context can then
      // free normally when callback_ctx goes out of scope, no leak needed.
      raw_job.vtable->destroy(raw_job.ctx);
      const std::string missing = !cancel_ok && !join_ok ? "cancel and join" : (!cancel_ok ? "cancel" : "join");
      return unexpected(
          "descriptor import provider returned a job missing a usable " + missing + " slot (violates ABI contract)");
    }

    // destroy itself is unusable: there is no way to safely stop the job, so
    // callback_ctx can never be freed normally — a late callback could still
    // fire against freed user state (not just a freed allocation: the
    // caller, having seen this error, is free to destroy whatever its
    // closures captured by reference). Quiesce (permanently disable
    // dispatch, waiting out anything already in flight) so the closures are
    // guaranteed inert, THEN release (deliberately leak) the allocation
    // rather than risk a use-after-free. This only happens when a provider
    // violates the ABI.
    JoinableJob::quiesce(*callback_ctx);
    (void)callback_ctx.release();
    return unexpected(
        "descriptor import provider returned true from start_import with an unusable job (violates ABI contract)");
  }

 private:
  static DescriptorImportOutcome mapOutcome(PJ_descriptor_import_outcome_t outcome) noexcept {
    switch (outcome) {
      case PJ_DESCRIPTOR_IMPORT_CANCELLED:
        return DescriptorImportOutcome::kCancelled;
      case PJ_DESCRIPTOR_IMPORT_SUCCEEDED_EAGER_ONLY:
        return DescriptorImportOutcome::kSucceededEagerOnly;
      case PJ_DESCRIPTOR_IMPORT_SUCCEEDED_PROMOTED:
        return DescriptorImportOutcome::kSucceededPromoted;
      case PJ_DESCRIPTOR_IMPORT_FAILED:
      default:
        return DescriptorImportOutcome::kFailed;
    }
  }

  static void onDatasetThunk(void* callback_ctx, PJ_data_source_handle_t dataset) noexcept {
    if (callback_ctx == nullptr) {
      return;
    }
    auto* ctx = static_cast<JoinableJob::CallbackContext*>(callback_ctx);
    // Dispatch guard: increment the shared dispatch_state FIRST, then check
    // the bit it carries back (see CallbackContext's doc-comment for why
    // this single combined atomic — not two independent ones — is what
    // makes this correct under a weak memory model). If a
    // JoinableJob::quiesce() call has (or races to) disable this context,
    // back out without touching the user's closure at all. This is what
    // makes it safe to leak an ABI-violating job's context instead of
    // freeing it: the context becomes permanently inert rather than merely
    // unreachable.
    const uint32_t prev = ctx->dispatch_state.fetch_add(1, std::memory_order_acq_rel);
    if ((prev & JoinableJob::CallbackContext::kDisabledBit) != 0) {
      ctx->dispatch_state.fetch_sub(1, std::memory_order_acq_rel);
      return;
    }
    try {
      if (ctx->on_dataset) {
        ctx->on_dataset(dataset.id);
      }
    } catch (...) {
      // Job-callback thread crossing the ABI boundary: never let an
      // exception from the host's closure unwind into the plugin.
    }
    ctx->dispatch_state.fetch_sub(1, std::memory_order_release);
  }

  static void onTerminalThunk(
      void* callback_ctx, PJ_descriptor_import_outcome_t outcome, PJ_string_view_t message) noexcept {
    if (callback_ctx == nullptr) {
      return;
    }
    auto* ctx = static_cast<JoinableJob::CallbackContext*>(callback_ctx);
    // Dispatch guard — see onDatasetThunk's comment.
    const uint32_t prev = ctx->dispatch_state.fetch_add(1, std::memory_order_acq_rel);
    if ((prev & JoinableJob::CallbackContext::kDisabledBit) != 0) {
      ctx->dispatch_state.fetch_sub(1, std::memory_order_acq_rel);
      return;
    }
    try {
      if (ctx->on_terminal) {
        ctx->on_terminal(mapOutcome(outcome), std::string(sdk::toStringView(message)));
      }
    } catch (...) {
      // Job-callback thread crossing the ABI boundary: never let an
      // exception from the host's closure unwind into the plugin. Unlike
      // on_dataset (zero-or-one, non-terminal), on_terminal is exactly-once
      // and last — if it throws, the exception is swallowed here AND the
      // completion notification is lost; the caller's on_terminal callable
      // will never observe this import's outcome.
    }
    ctx->dispatch_state.fetch_sub(1, std::memory_order_release);
  }

  const PJ_descriptor_import_provider_v1_t* ext_ = nullptr;
  void* plugin_ctx_ = nullptr;
};

// ---------------------------------------------------------------------------
// Service half: "pj.source_promotion.v1"
// A provider plugin consuming the host's optional source-promotion service,
// acquired from the bind() service registry (see
// PJ::sdk::SourcePromotionHostService below). Absence from the registry means
// the host has no source-promotion support.
// ---------------------------------------------------------------------------

/// Promotion request (C++ mirror). All strings are copied by the host during
/// promoteToFileSource() — the request may be destroyed as soon as
/// promoteToFileSource() returns.
struct SourcePromotionRequest {
  DatasetId dataset{};
  std::string source_identity;
  std::string local_path_utf8;
  std::string loader_plugin_id;
  std::string loader_config_json;
  std::string descriptor_json;
};

/// Typed consumer of the "pj.source_promotion.v1" host service.
///
/// Non-owning: this view wraps a borrowed fat pointer obtained from bind()'s
/// service registry lookup and holds no keep-alive of its own. It must not
/// outlive the plugin's bound service scope. valid() only checks the fat
/// pointer's own shape (non-null ctx/vtable, struct_size, non-null
/// promote_to_file_source slot) — it CANNOT detect a stale pointer into a
/// registry that has since been torn down. promoteToFileSource()'s normal
/// shape is asynchronous, with @p on_result typically firing well after
/// bind() has already returned (see promoteToFileSource()'s doc-comment) —
/// that is the normal promotion shape, not an edge case — so a caller that
/// holds onto this view (or a copy of it) to complete a pending
/// promoteToFileSource() later must independently ensure the underlying
/// binding/registry stays alive for as long as that promotion is
/// outstanding.
///
/// Per PJ_source_promotion_host_vtable_t's doc-comment, the host binds this
/// service PER PLUGIN INSTANCE — ctx identifies the provider and the host
/// derives the provider's manifest id from that binding itself. A plugin
/// must therefore never share a bound view across plugin instances (e.g.
/// cache one in a static and hand it to a sibling instance): doing so would
/// let one provider promote a source under another provider's identity.
class SourcePromotionHostView {
 public:
  SourcePromotionHostView() = default;
  explicit SourcePromotionHostView(PJ_source_promotion_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const noexcept {
    return host_.ctx != nullptr && host_.vtable != nullptr &&
           host_.vtable->struct_size >= offsetof(PJ_source_promotion_host_vtable_t, promote_to_file_source) +
                                            sizeof(host_.vtable->promote_to_file_source) &&
           host_.vtable->promote_to_file_source != nullptr;
  }

  /// [thread-safe, asynchronous] See PJ_source_promotion_host_vtable_t::promote_to_file_source.
  /// An ok return means the request was ACCEPTED/queued only — it does NOT
  /// mean the promotion transaction itself succeeded. Success or failure of
  /// the transaction arrives exclusively through @p on_result's `ok`
  /// argument (mirrors the C header's accepted-vs-succeeded distinction —
  /// see PJ_source_promotion_host_vtable_t::promote_to_file_source's
  /// doc-comment).
  ///
  /// On acceptance, @p on_result runs exactly once, on the host's
  /// promotion-result callback thread [host-callback-thread]: serialized per
  /// request, but NOT promised to be the main/GUI thread — do not touch
  /// main-thread-only state from @p on_result. It may run re-entrantly,
  /// before this call even returns, or well after (the normal shape — see
  /// the class doc's lifetime note above). A synchronous rejection returns
  /// an error Status and @p on_result never runs. @p on_result must not
  /// throw; an escaping exception is swallowed (the result is then lost —
  /// the caller will never observe this promoteToFileSource() call's
  /// outcome).
  [[nodiscard]] Status promoteToFileSource(
      const SourcePromotionRequest& request, std::function<void(bool, std::string)> on_result) const {
    if (!valid()) {
      return unexpected("source promotion host service is not available");
    }

    PJ_source_promotion_request_v1_t raw{};
    raw.struct_size = sizeof(raw);
    raw.dataset = PJ_data_source_handle_t{request.dataset};
    raw.source_identity = sdk::toAbiString(request.source_identity);
    raw.local_path_utf8 = sdk::toAbiString(request.local_path_utf8);
    raw.loader_plugin_id = sdk::toAbiString(request.loader_plugin_id);
    raw.loader_config_json = sdk::toAbiString(request.loader_config_json);
    raw.descriptor_json = sdk::toAbiString(request.descriptor_json);

    // Heap-allocate the closure so it can outlive this call: on acceptance
    // the thunk owns it and frees it exactly once when result_cb runs.
    auto ctx = std::make_unique<std::function<void(bool, std::string)>>(std::move(on_result));

    // Transfer ownership to the raw pointer BEFORE calling into the host: a
    // re-entrant thunk (result_cb invoked synchronously, before
    // promoteToFileSource() returns) may free the pointee before this
    // function resumes below. unique_ptr::release() only ever reads/clears
    // its OWN stored pointer value — never the pointee — so this stays
    // formally correct regardless of whether the thunk has already run by
    // the time we get here.
    auto* raw_ctx = ctx.release();

    PJ_error_t err{};
    if (!host_.vtable->promote_to_file_source(host_.ctx, &raw, &SourcePromotionHostView::resultThunk, raw_ctx, &err)) {
      // Synchronous rejection: result_cb will never run. Reclaim ownership
      // so the context is still freed exactly once.
      const std::unique_ptr<std::function<void(bool, std::string)>> reclaimed{raw_ctx};
      return unexpected(sdk::errorToString(err));
    }
    // Accepted: the thunk now owns the context (already released above).
    return okStatus();
  }

 private:
  /// [host-callback-thread] Per
  /// PJ_source_promotion_host_vtable_t::promote_to_file_source's contract,
  /// the host must invoke an ACCEPTED request's result_cb EXACTLY ONCE. A
  /// second invocation is undefined behavior — use-after-free on the closure
  /// this thunk already freed on the first call — and there is no cheap
  /// defense against it: a "consumed" flag would itself have to live in the
  /// same heap block this thunk frees on the first call.
  static void resultThunk(void* callback_ctx, bool ok, PJ_string_view_t message) noexcept {
    if (callback_ctx == nullptr) {
      return;
    }
    // Take ownership FIRST so the context is freed exactly once even if the
    // callback below throws.
    std::unique_ptr<std::function<void(bool, std::string)>> fn{
        static_cast<std::function<void(bool, std::string)>*>(callback_ctx)};
    try {
      if (*fn) {
        (*fn)(ok, std::string(sdk::toStringView(message)));
      }
    } catch (...) {
      // Host-callback thread crossing the ABI boundary: never let an
      // exception from the caller's closure unwind into the host. The
      // result is then lost — the caller's on_result will never observe
      // this promoteToFileSource() call's outcome.
    }
  }

  PJ_source_promotion_host_t host_{};
};

}  // namespace PJ

namespace PJ::sdk {

/// Service trait for the optional per-plugin-instance source-promotion
/// service ("pj.source_promotion.v1"). Absence from the registry means the
/// host has no source-promotion support. See SourcePromotionHostView's
/// doc-comment for the per-plugin-instance binding contract.
struct SourcePromotionHostService {
  static constexpr const char* kName = PJ_SOURCE_PROMOTION_HOST_SERVICE_V1;
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_source_promotion_host_t;
  using Vtable = PJ_source_promotion_host_vtable_t;
  using View = ::PJ::SourcePromotionHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

}  // namespace PJ::sdk
