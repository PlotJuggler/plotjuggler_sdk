// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Private RAII advisory file lock over a dedicated sidecar lock file (POSIX
// flock / Windows LockFileEx), non-blocking try-acquire only. EXCLUSIVE for
// materialization/eviction/cleanup mutual exclusion, SHARED for read leases.
// Shared holders stack; any shared holder blocks an exclusive try and vice
// versa — across processes AND between handles of one process (flock is
// per-open-file-description, LockFileEx per-handle), which is what lets a
// provider's own leases block its own cleanup. fcntl/POSIX record locks would
// NOT give that (they are per-process), so this must never be swapped for
// them without a redesign.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

class FileLock {
 public:
  /// Non-blocking exclusive try-acquire, creating the lock file 0600 if
  /// absent. nullopt with `*error` set when held elsewhere (`*contended` =
  /// true: retry-able) or on an OS failure (`*contended` = false). The lock
  /// file is never deleted: unlinking a path another process may be about to
  /// open would hand out two "exclusive" locks on different inodes.
  [[nodiscard]] static std::optional<FileLock> tryExclusive(
      const std::filesystem::path& path, std::string* error, bool* contended = nullptr);

  /// Non-blocking shared try-acquire, creating the lock file 0600 if absent.
  [[nodiscard]] static std::optional<FileLock> tryShared(const std::filesystem::path& path, std::string* error);

  /// Convert a HELD EXCLUSIVE lock to SHARED without closing the handle.
  /// Neither flock(2) nor LockFileEx guarantees an atomic conversion: a
  /// concurrent non-blocking exclusive try may win the window, in which case
  /// the lock is RELEASED and false returned — the caller re-acquires shared
  /// and revalidates whatever the lock protected.
  [[nodiscard]] bool downgradeToShared(std::string* error);

  FileLock(FileLock&& other) noexcept;
  FileLock& operator=(FileLock&& other) noexcept;
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  ~FileLock();

 private:
  explicit FileLock(std::intptr_t handle) : handle_(handle) {}
  void release();

  // POSIX fd / Windows HANDLE. -1 = released/moved-from (INVALID_HANDLE_VALUE
  // is (HANDLE)-1, so one sentinel serves both).
  std::intptr_t handle_ = -1;
};

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
