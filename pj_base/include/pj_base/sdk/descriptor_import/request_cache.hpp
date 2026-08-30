/**
 * @file request_cache.hpp
 * @brief The request-addressed artifact cache behind descriptor-import
 *        providers: one validated file per request identity, published
 *        atomically, protected by cross-process leases, bounded by an LRU
 *        cleanup.
 *
 * Layout under CacheSpec::root: <digest><artifact_suffix> is the artifact;
 * <name>.partial.<pid> a materialization in progress; <name>.lock the
 * per-identity advisory lock (exclusive while materializing, evicting or
 * cleaning up; shared while any live consumer holds a ReadLease); <name>.touch
 * the LRU stamp (atime is unreliable under relatime/noatime, so hits touch
 * explicitly). Directory 0700, files 0600.
 *
 * Why leases exist: a file-backed dataset lazily re-opens its artifact long
 * after the cache handed the path out, and unlink-while-open does not protect
 * a later open(). Every consumer that may re-read a path holds a ReadLease
 * for as long as it may do so; cleanup and re-materialization skip any
 * identity whose lock is held, in this process or another.
 *
 * The artifact FORMAT is the provider's: validation is an injected callback
 * that must be bounded I/O (lookups run on the GUI thread) and must verify
 * the file's embedded provenance by RE-HASHING it against the digest — never
 * by trusting a stored identity string. The mechanics here are
 * format-agnostic and tested hermetically with stub validators.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_base/sdk/descriptor_import/source_descriptor.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {

/// Where and how a cache stores artifacts. `identity` is the provider's
/// IdentityScheme (the same object its SourceDescriptorPolicy carries), so
/// the identity string a descriptor mints names the same file everywhere.
struct CacheSpec {
  std::filesystem::path root;
  std::string artifact_suffix;  ///< e.g. ".pjmosaico"; must start with '.'
  IdentityScheme identity;
};

/// Why a cache operation could not proceed. `retryable` is true only when the
/// cause is a lock held by another holder (a live materialization or read
/// lease, in any process) — the caller may try again later; everything else
/// (malformed identity, unset root, OS failure, rejected artifact) is final.
struct CacheError {
  std::string message;
  bool retryable = false;
};

/// Budgets for cleanup(). Defaults impose no size budget; set both fields a
/// provider actually enforces.
struct CleanupPolicy {
  std::uintmax_t max_total_bytes = std::numeric_limits<std::uintmax_t>::max();
  std::uintmax_t min_free_bytes = 0;
  /// A partial older than this whose identity lock is free belongs to a dead
  /// writer and is removed.
  std::chrono::hours orphan_partial_age{24};
};

/// Best-effort outcome of cleanup(): it never throws and never deletes a
/// leased or in-flight file, so "target not met" is a legitimate result.
struct CleanupResult {
  std::uintmax_t bytes_scanned = 0;
  std::uintmax_t bytes_reclaimed = 0;
  std::uintmax_t bytes_held_over_target = 0;
  bool target_met = true;
  bool had_errors = false;
};

/// One-line human summary of a CleanupResult (for logs/diagnostics).
[[nodiscard]] std::string cleanupResultSummary(const CleanupResult& result);

/// A shared hold on one identity's lock: while held, cleanup and
/// re-materialization of that identity skip it. Move-only; released on
/// destruction. Leases stack across holders and across processes.
class ReadLease {
 public:
  ReadLease();
  ~ReadLease();
  ReadLease(ReadLease&& other) noexcept;
  ReadLease& operator=(ReadLease&& other) noexcept;
  ReadLease(const ReadLease&) = delete;
  ReadLease& operator=(const ReadLease&) = delete;

  /// False for a default-constructed or moved-from lease, and for a Hit whose
  /// lease could not be taken (the path is then usable but unprotected).
  [[nodiscard]] bool held() const noexcept;
  /// Drop the hold early, before the lease is destroyed.
  void release() noexcept;

 private:
  friend class RequestArtifactCache;
  struct Impl;
  explicit ReadLease(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

/// Decides whether `file` is a structurally valid artifact FOR `digest_hex`.
/// Bounded I/O; must re-hash embedded provenance against the digest.
using ArtifactValidator =
    std::function<bool(const std::filesystem::path& file, const std::string& digest_hex, std::string* error)>;

class RequestArtifactCache {
 public:
  /// A validated artifact path plus the lease protecting it.
  struct Hit {
    std::filesystem::path path;
    ReadLease lease;
  };

  /// An in-progress materialization: write the artifact to partialPath(),
  /// then commit(). Holds the identity's exclusive lock for its lifetime; the
  /// partial is removed on abort() or destruction without commit.
  class WriteTransaction {
   public:
    ~WriteTransaction();
    WriteTransaction(WriteTransaction&& other) noexcept;
    WriteTransaction& operator=(WriteTransaction&& other) noexcept;
    WriteTransaction(const WriteTransaction&) = delete;
    WriteTransaction& operator=(const WriteTransaction&) = delete;

    /// The file this process must write; unique per (identity, pid).
    [[nodiscard]] const std::filesystem::path& partialPath() const noexcept;

    /// Validate the partial through the cache's validator, fsync it, rename
    /// it atomically over the artifact path, fsync the directory (POSIX), stamp
    /// the LRU, and convert the exclusive lock into the returned Hit's shared
    /// lease. On any failure the partial is removed and the reason returned;
    /// the transaction is finished either way. The lock conversion is not
    /// atomic on any platform: when a concurrent exclusive try wins that
    /// window the returned lease is re-acquired shared and the artifact's
    /// existence re-checked, so a Hit always names a file that existed under
    /// its lease.
    [[nodiscard]] Expected<Hit, CacheError> commit();

    /// Remove the partial and release the lock. Idempotent.
    void abort() noexcept;

   private:
    friend class RequestArtifactCache;
    struct Impl;
    explicit WriteTransaction(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
  };

  /// The validator is mandatory: defaulting to "accept" would let any foreign
  /// file at <digest><suffix> classify as a hit. Production injects the
  /// artifact-format validator; tests inject stubs.
  RequestArtifactCache(CacheSpec spec, ArtifactValidator validator);

  [[nodiscard]] const CacheSpec& spec() const noexcept {
    return spec_;
  }

  /// The artifact path for `identity`; empty when the identity is malformed
  /// under spec().identity or the root is unset. Takes no position on whether
  /// the file exists.
  [[nodiscard]] std::filesystem::path pathFor(std::string_view identity) const;

  /// Existing + validator-approved, with a shared lease taken BEFORE the
  /// validation (lease-then-validate) so an evictor cannot unlink between the
  /// check and the use; touches the LRU stamp. When the lease is contended
  /// (an exclusive holder is live) the file is still validated and returned,
  /// with `lease.held() == false`. A miss reports why in `miss_reason`
  /// (absent, or the validator's rejection); the file is NOT deleted on a
  /// rejection — the next materialization renames over it.
  [[nodiscard]] std::optional<Hit> lookup(std::string_view identity, std::string* miss_reason = nullptr);

  /// Shared lease on `identity`'s lock, independently of the file's existence
  /// (e.g. to pin a path a caller obtained earlier). Retryable while an
  /// exclusive holder is live.
  [[nodiscard]] Expected<ReadLease, CacheError> acquireReadLease(std::string_view identity);

  /// Begin materializing `identity`. Retryable while the lock is held — by a
  /// materialization or by a live ReadLease.
  [[nodiscard]] Expected<WriteTransaction, CacheError> beginWrite(std::string_view identity);

  /// Maintenance: remove orphaned partials older than orphan_partial_age
  /// whose lock is free, then evict unlocked artifacts oldest-stamp-first
  /// until max_total_bytes and min_free_bytes both hold. Only files shaped
  /// <digest><suffix> are ever candidates — foreign files in the root are
  /// never touched. Non-recursive.
  [[nodiscard]] CleanupResult cleanup(const CleanupPolicy& policy);

 private:
  CacheSpec spec_;
  ArtifactValidator validator_;
};

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
