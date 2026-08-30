// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/descriptor_import/request_cache.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

#include "descriptor_import/file_lock.hpp"
#include "descriptor_import/fs_durability.hpp"

#if defined(_WIN32)
// windows.h min/max macros break std::max / numeric_limits<>::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace PJ {
namespace sdk {
namespace descriptor_import {

namespace fs = std::filesystem;

namespace {

long long currentPid() {
#if defined(_WIN32)
  return static_cast<long long>(::GetCurrentProcessId());
#else
  return static_cast<long long>(::getpid());
#endif
}

std::uintmax_t saturatedAdd(std::uintmax_t a, std::uintmax_t b) {
  if (b > std::numeric_limits<std::uintmax_t>::max() - a) {
    return std::numeric_limits<std::uintmax_t>::max();
  }
  return a + b;
}

// The ONE owner of the on-disk naming: artifact, sidecars, partials, and the
// classifier cleanup uses to tell managed files from foreign ones.
class CacheLayout {
 public:
  explicit CacheLayout(const CacheSpec& spec) : spec_(spec) {}

  [[nodiscard]] fs::path artifact(std::string_view digest) const {
    return spec_.root / (std::string(digest) + spec_.artifact_suffix);
  }
  [[nodiscard]] static fs::path lockOf(const fs::path& artifact) {
    return fs::path(artifact.string() + ".lock");
  }
  [[nodiscard]] static fs::path touchOf(const fs::path& artifact) {
    return fs::path(artifact.string() + ".touch");
  }
  [[nodiscard]] static fs::path partialOf(const fs::path& artifact) {
    return fs::path(artifact.string() + ".partial." + std::to_string(currentPid()));
  }

  enum class Kind { kArtifact, kPartial, kOther };
  struct Classified {
    Kind kind = Kind::kOther;
    fs::path artifact;  ///< the artifact this file belongs to (kArtifact/kPartial)
  };

  /// Managed names only: "<digest><suffix>" and "<digest><suffix>.partial.<pid>";
  /// sidecars, foreign files and malformed digests are kOther.
  [[nodiscard]] Classified classify(const fs::path& file) const {
    const std::string name = file.filename().string();
    const std::size_t hex = spec_.identity.hexChars();
    const auto digestOk = [&name, hex] {
      return name.size() >= hex && std::all_of(
                                       name.begin(), name.begin() + static_cast<std::ptrdiff_t>(hex),
                                       [](const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    };
    Classified out;
    if (!digestOk()) {
      return out;
    }
    const std::string_view rest = std::string_view(name).substr(hex);
    if (rest == spec_.artifact_suffix) {
      out.kind = Kind::kArtifact;
      out.artifact = file;
      return out;
    }
    const std::string marker = spec_.artifact_suffix + ".partial.";
    if (rest.size() > marker.size() && rest.substr(0, marker.size()) == marker) {
      const std::string_view pid = rest.substr(marker.size());
      if (std::all_of(pid.begin(), pid.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
        out.kind = Kind::kPartial;
        out.artifact = file.parent_path() / (name.substr(0, hex) + spec_.artifact_suffix);
      }
    }
    return out;
  }

 private:
  const CacheSpec& spec_;
};

// Update (or create) the LRU stamp beside `artifact`. The sidecar's mtime is
// the eviction order; lookup hits and commits both move it to now.
void touchStamp(const fs::path& artifact) {
  const fs::path stamp = CacheLayout::touchOf(artifact);
  { std::ofstream out(stamp, std::ios::binary | std::ios::trunc); }
  std::error_code ec;
  fs::last_write_time(stamp, fs::file_time_type::clock::now(), ec);
  detail::chmod0600(stamp);
}

}  // namespace

std::string cleanupResultSummary(const CleanupResult& result) {
  std::string summary = "cache cleanup scanned " + std::to_string(result.bytes_scanned) + " bytes and reclaimed " +
                        std::to_string(result.bytes_reclaimed) + " bytes; target " +
                        (result.target_met ? "met" : "not met");
  if (result.bytes_held_over_target != 0) {
    summary += ", " + std::to_string(result.bytes_held_over_target) + " bytes remain over target";
  }
  if (result.had_errors) {
    summary += ", filesystem or lock errors occurred";
  }
  return summary;
}

// ---------------------------------------------------------------------------
// ReadLease
// ---------------------------------------------------------------------------

struct ReadLease::Impl {
  detail::FileLock lock;
};

ReadLease::ReadLease() = default;
ReadLease::~ReadLease() = default;
ReadLease::ReadLease(ReadLease&& other) noexcept = default;
ReadLease& ReadLease::operator=(ReadLease&& other) noexcept = default;
ReadLease::ReadLease(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

bool ReadLease::held() const noexcept {
  return impl_ != nullptr;
}

void ReadLease::release() noexcept {
  impl_.reset();
}

// ---------------------------------------------------------------------------
// WriteTransaction
// ---------------------------------------------------------------------------

struct RequestArtifactCache::WriteTransaction::Impl {
  CacheSpec spec;
  ArtifactValidator validator;
  std::string digest;
  fs::path artifact;
  fs::path partial;
  std::optional<detail::FileLock> lock;
  bool finished = false;
};

RequestArtifactCache::WriteTransaction::WriteTransaction(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

RequestArtifactCache::WriteTransaction::~WriteTransaction() {
  abort();
}

RequestArtifactCache::WriteTransaction::WriteTransaction(WriteTransaction&& other) noexcept
    : impl_(std::move(other.impl_)) {}

RequestArtifactCache::WriteTransaction& RequestArtifactCache::WriteTransaction::operator=(
    WriteTransaction&& other) noexcept {
  if (this != &other) {
    abort();
    impl_ = std::move(other.impl_);
  }
  return *this;
}

const fs::path& RequestArtifactCache::WriteTransaction::partialPath() const noexcept {
  return impl_->partial;
}

Expected<RequestArtifactCache::Hit, CacheError> RequestArtifactCache::WriteTransaction::commit() {
  if (!impl_ || impl_->finished) {
    return unexpected(CacheError{"write transaction already finished"});
  }
  Impl& impl = *impl_;
  impl.finished = true;
  const auto fail = [&impl](std::string reason) {
    std::error_code ec;
    fs::remove(impl.partial, ec);
    impl.lock.reset();
    return unexpected(CacheError{std::move(reason)});
  };
  const std::string what = "cache commit rejected " + impl.partial.filename().string() + ": ";
  if (!impl.validator) {
    return fail(what + "no artifact validator configured");
  }
  std::string reason;
  if (!impl.validator(impl.partial, impl.digest, &reason)) {
    return fail(what + reason);
  }
  detail::chmod0600(impl.partial);
  if (!detail::syncFile(impl.partial, &reason)) {
    return fail(reason);
  }
  std::error_code ec;
  fs::rename(impl.partial, impl.artifact, ec);
  if (ec) {
    return fail("atomic rename failed: " + ec.message());
  }
  detail::syncDir(impl.spec.root);
  touchStamp(impl.artifact);

  Hit hit;
  hit.path = impl.artifact;
  std::optional<detail::FileLock> lease;
  if (impl.lock->downgradeToShared(&reason)) {
    lease = std::move(*impl.lock);
  } else {
    // The exclusive lock was released in the failed conversion: re-acquire
    // shared, then re-check existence under it — the published file may have
    // been evicted in that window.
    lease = detail::FileLock::tryShared(CacheLayout::lockOf(impl.artifact), &reason);
    std::error_code exists_ec;
    if (lease.has_value() && (!fs::is_regular_file(impl.artifact, exists_ec) || exists_ec)) {
      impl.lock.reset();
      return unexpected(CacheError{"artifact vanished during the lease handoff"});
    }
    // No lease at all: a Hit without one — the caller sees held() == false.
  }
  impl.lock.reset();
  if (lease.has_value()) {
    hit.lease = ReadLease(std::make_unique<ReadLease::Impl>(ReadLease::Impl{std::move(*lease)}));
  }
  return hit;
}

void RequestArtifactCache::WriteTransaction::abort() noexcept {
  if (!impl_ || impl_->finished) {
    return;
  }
  impl_->finished = true;
  std::error_code ec;
  fs::remove(impl_->partial, ec);
  impl_->lock.reset();
}

// ---------------------------------------------------------------------------
// RequestArtifactCache
// ---------------------------------------------------------------------------

RequestArtifactCache::RequestArtifactCache(CacheSpec spec, ArtifactValidator validator)
    : spec_(std::move(spec)), validator_(std::move(validator)) {}

namespace {

struct Resolved {
  std::string digest;
  fs::path artifact;
};

// The shared preamble of every identity-addressed operation: digest shape,
// configured root, and the artifact path. `create_root` makes the root
// (0700) for operations that will take a lock there.
Expected<Resolved, CacheError> resolveIdentity(const CacheSpec& spec, std::string_view identity, bool create_root) {
  const auto digest = spec.identity.digestOf(identity);
  if (!digest.has_value()) {
    return unexpected(
        CacheError{
            "invalid descriptor identity (want " + spec.identity.prefix + "<" +
            std::to_string(spec.identity.hexChars()) + " lowercase hex>)"});
  }
  if (spec.root.empty()) {
    return unexpected(CacheError{"cache root is not configured"});
  }
  if (create_root) {
    detail::ensureDir0700(spec.root);
  }
  return Resolved{*digest, CacheLayout(spec).artifact(*digest)};
}

}  // namespace

fs::path RequestArtifactCache::pathFor(std::string_view identity) const {
  const auto resolved = resolveIdentity(spec_, identity, /*create_root=*/false);
  return resolved ? resolved->artifact : fs::path();
}

std::optional<RequestArtifactCache::Hit> RequestArtifactCache::lookup(
    std::string_view identity, std::string* miss_reason) {
  const auto miss = [miss_reason](std::string reason) {
    if (miss_reason != nullptr) {
      *miss_reason = std::move(reason);
    }
    return std::nullopt;
  };
  const auto resolved = resolveIdentity(spec_, identity, /*create_root=*/false);
  if (!resolved) {
    return miss(resolved.error().message);
  }
  if (!validator_) {
    return miss("no artifact validator configured");
  }
  std::error_code ec;
  if (!fs::is_regular_file(resolved->artifact, ec) || ec) {
    return miss("no artifact for this identity");
  }
  // Lease-then-validate: the shared lock is taken before the validation so an
  // evictor cannot unlink the file between the check and the caller's use.
  std::string lease_error;
  auto lease = detail::FileLock::tryShared(CacheLayout::lockOf(resolved->artifact), &lease_error);
  std::string reason;
  if (!validator_(resolved->artifact, resolved->digest, &reason)) {
    return miss("artifact rejected: " + reason);
  }
  touchStamp(resolved->artifact);
  Hit hit;
  hit.path = resolved->artifact;
  if (lease.has_value()) {
    hit.lease = ReadLease(std::make_unique<ReadLease::Impl>(ReadLease::Impl{std::move(*lease)}));
  }
  return hit;
}

Expected<ReadLease, CacheError> RequestArtifactCache::acquireReadLease(std::string_view identity) {
  auto resolved = resolveIdentity(spec_, identity, /*create_root=*/true);
  if (!resolved) {
    return unexpected(std::move(resolved).error());
  }
  std::string lock_error;
  auto lease = detail::FileLock::tryShared(CacheLayout::lockOf(resolved->artifact), &lock_error);
  if (!lease.has_value()) {
    return unexpected(CacheError{"cache read lease unavailable: " + lock_error, /*retryable=*/true});
  }
  return ReadLease(std::make_unique<ReadLease::Impl>(ReadLease::Impl{std::move(*lease)}));
}

Expected<RequestArtifactCache::WriteTransaction, CacheError> RequestArtifactCache::beginWrite(
    std::string_view identity) {
  auto resolved = resolveIdentity(spec_, identity, /*create_root=*/true);
  if (!resolved) {
    return unexpected(std::move(resolved).error());
  }
  std::string lock_error;
  bool contended = false;
  auto lock = detail::FileLock::tryExclusive(CacheLayout::lockOf(resolved->artifact), &lock_error, &contended);
  if (!lock.has_value()) {
    return unexpected(CacheError{"cache materialize lock unavailable: " + lock_error, contended});
  }
  auto impl = std::make_unique<WriteTransaction::Impl>();
  impl->spec = spec_;
  impl->validator = validator_;
  impl->digest = resolved->digest;
  impl->artifact = resolved->artifact;
  impl->partial = CacheLayout::partialOf(resolved->artifact);
  impl->lock = std::move(lock);
  return WriteTransaction(std::move(impl));
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------

namespace {

struct StalePartial {
  fs::path file;
  fs::path artifact;
};

struct EvictionCandidate {
  fs::path file;
  std::uintmax_t size;
  fs::file_time_type stamp;
};

struct Scan {
  std::vector<StalePartial> stale_partials;
  std::vector<EvictionCandidate> candidates;
  std::uintmax_t total = 0;
};

// One directory pass classifying every regular file into the two buckets
// cleanup acts on. Sidecars, fresh partials and foreign files are ignored.
Scan scanRoot(const CacheLayout& layout, const fs::path& root, const CleanupPolicy& policy, CleanupResult& result) {
  const auto noteError = [&result](const std::error_code& ec) {
    if (ec) {
      result.had_errors = true;
    }
  };
  Scan scan;
  const auto now = fs::file_time_type::clock::now();
  std::error_code ec;
  const fs::directory_iterator end;
  for (fs::directory_iterator it(root, ec); !ec && it != end; it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec) {
      noteError(entry_ec);
      continue;
    }
    const auto classified = layout.classify(entry.path());
    if (classified.kind == CacheLayout::Kind::kPartial) {
      const auto mtime = fs::last_write_time(entry.path(), entry_ec);
      noteError(entry_ec);
      if (!entry_ec && now - mtime >= policy.orphan_partial_age) {
        scan.stale_partials.push_back({entry.path(), classified.artifact});
      }
    } else if (classified.kind == CacheLayout::Kind::kArtifact) {
      const std::uintmax_t size = entry.file_size(entry_ec);
      if (entry_ec) {
        result.had_errors = true;
        continue;
      }
      scan.total = saturatedAdd(scan.total, size);
      auto stamp = fs::last_write_time(CacheLayout::touchOf(entry.path()), entry_ec);
      if (entry_ec) {
        // No touch sidecar (pre-stamp file or a deleted stamp): fall back to
        // the file's own mtime so it still participates in the order.
        stamp = fs::last_write_time(entry.path(), entry_ec);
        if (entry_ec) {
          stamp = fs::file_time_type::min();
        }
      }
      scan.candidates.push_back({entry.path(), size, stamp});
    }
  }
  if (ec) {
    result.target_met = false;
    result.had_errors = true;
  }
  return scan;
}

// Orphaned partials: old enough AND identity lock free. BOTH guards matter:
// the lock dies with its process (so a crashed writer's partial becomes
// collectable), while the age threshold keeps process B from ever deleting
// process A's live partial in a lock-handoff instant.
void removeStalePartials(const std::vector<StalePartial>& partials, CleanupResult& result) {
  for (const StalePartial& partial : partials) {
    std::string lock_error;
    bool contended = false;
    const auto lock = detail::FileLock::tryExclusive(CacheLayout::lockOf(partial.artifact), &lock_error, &contended);
    if (!lock.has_value()) {
      result.had_errors = result.had_errors || !contended;
      continue;  // a live materialization owns this identity
    }
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(partial.file, ec);
    const bool size_known = !ec;
    if (!fs::remove(partial.file, ec) || ec) {
      result.had_errors = true;
    } else if (size_known) {
      result.bytes_reclaimed = saturatedAdd(result.bytes_reclaimed, size);
    }
  }
}

// LRU eviction by touch-stamp order until BOTH budgets hold. Each victim's
// identity lock is taken non-blocking first — busy means a live
// materialization or a read lease: skip it.
void evictUntilUnderBudget(
    std::vector<EvictionCandidate>& candidates, std::uintmax_t total, const fs::path& root, const CleanupPolicy& policy,
    CleanupResult& result) {
  std::sort(candidates.begin(), candidates.end(), [](const EvictionCandidate& a, const EvictionCandidate& b) {
    return a.stamp < b.stamp;
  });
  // Free space is queried ONCE; evictions are credited back to the estimate.
  std::optional<std::uintmax_t> available;
  if (policy.min_free_bytes != 0) {
    std::error_code space_ec;
    const fs::space_info space = fs::space(root, space_ec);
    if (space_ec) {
      result.target_met = false;
      result.had_errors = true;
      return;
    }
    available = space.available;
  }
  const auto bytesOverTarget = [&]() {
    const std::uintmax_t size_excess =
        total > policy.max_total_bytes ? total - policy.max_total_bytes : std::uintmax_t{0};
    const std::uintmax_t free_deficit = available.has_value() && *available < policy.min_free_bytes
                                            ? policy.min_free_bytes - *available
                                            : std::uintmax_t{0};
    return std::max(size_excess, free_deficit);
  };
  for (const EvictionCandidate& victim : candidates) {
    if (bytesOverTarget() == 0) {
      break;
    }
    std::string lock_error;
    bool contended = false;
    const auto lock = detail::FileLock::tryExclusive(CacheLayout::lockOf(victim.file), &lock_error, &contended);
    if (!lock.has_value()) {
      result.had_errors = result.had_errors || !contended;
      continue;  // leased or re-materializing: never evict under a holder
    }
    std::error_code ec;
    if (!fs::remove(victim.file, ec) || ec) {
      result.had_errors = true;
      continue;
    }
    total -= victim.size;
    if (available.has_value()) {
      *available = saturatedAdd(*available, victim.size);
    }
    result.bytes_reclaimed = saturatedAdd(result.bytes_reclaimed, victim.size);
    fs::remove(CacheLayout::touchOf(victim.file), ec);
    result.had_errors = result.had_errors || static_cast<bool>(ec);
  }
  result.bytes_held_over_target = bytesOverTarget();
  result.target_met = result.bytes_held_over_target == 0;
}

}  // namespace

CleanupResult RequestArtifactCache::cleanup(const CleanupPolicy& policy) {
  CleanupResult result;
  if (spec_.root.empty()) {
    return result;
  }
  std::error_code ec;
  const fs::file_status root_status = fs::status(spec_.root, ec);
  if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(root_status))) {
    return result;  // first use: the first materialization creates the root later
  }
  if (ec || !fs::is_directory(root_status)) {
    const bool has_budget =
        policy.max_total_bytes != std::numeric_limits<std::uintmax_t>::max() || policy.min_free_bytes != 0;
    result.target_met = !has_budget;
    result.had_errors = true;
    return result;
  }
  const CacheLayout layout(spec_);
  Scan scan = scanRoot(layout, spec_.root, policy, result);
  if (!result.target_met) {
    return result;  // the scan itself failed
  }
  result.bytes_scanned = scan.total;
  removeStalePartials(scan.stale_partials, result);
  evictUntilUnderBudget(scan.candidates, scan.total, spec_.root, policy, result);
  return result;
}

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
