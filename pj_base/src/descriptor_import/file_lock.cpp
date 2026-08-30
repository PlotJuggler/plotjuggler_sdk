// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "descriptor_import/file_lock.hpp"

#include <system_error>
#include <utility>

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
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

namespace {

// Shared try-acquire body for both modes: open/create the 0600 lock file,
// then take the advisory lock non-blocking. `exclusive` selects LOCK_EX vs
// LOCK_SH (POSIX) / the LOCKFILE_EXCLUSIVE_LOCK flag (Windows) — both modes
// contend on the same first byte, so shared holders stack and block an
// exclusive try (and vice versa).
std::optional<std::intptr_t> tryAcquireHandle(
    const std::filesystem::path& path, bool exclusive, std::string* error, bool* contended) {
  if (contended != nullptr) {
    *contended = false;
  }
#if defined(_WIN32)
  const HANDLE handle = ::CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (error != nullptr) {
      *error = "could not open lock file " + path.string() + " (error " + std::to_string(::GetLastError()) + ")";
    }
    return std::nullopt;
  }
  OVERLAPPED overlapped{};
  const DWORD flags = LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0);
  if (::LockFileEx(handle, flags, 0, 1, 0, &overlapped) == 0) {
    ::CloseHandle(handle);
    if (contended != nullptr) {
      *contended = true;  // FAIL_IMMEDIATELY: the region is held elsewhere
    }
    if (error != nullptr) {
      *error = "lock is held elsewhere: " + path.string();
    }
    return std::nullopt;
  }
  return reinterpret_cast<std::intptr_t>(handle);
#else
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "could not open lock file " + path.string() + ": " +
               std::error_code(errno, std::generic_category()).message();
    }
    return std::nullopt;
  }
  if (::flock(fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
    const int flock_errno = errno;
    ::close(fd);
    const bool held_elsewhere = (flock_errno == EWOULDBLOCK || flock_errno == EAGAIN);
    if (contended != nullptr) {
      *contended = held_elsewhere;
    }
    if (error != nullptr) {
      *error = held_elsewhere ? "lock is held elsewhere: " + path.string()
                              : "flock failed on " + path.string() + ": " +
                                    std::error_code(flock_errno, std::generic_category()).message();
    }
    return std::nullopt;
  }
  return static_cast<std::intptr_t>(fd);
#endif
}

}  // namespace

std::optional<FileLock> FileLock::tryExclusive(const std::filesystem::path& path, std::string* error, bool* contended) {
  auto handle = tryAcquireHandle(path, /*exclusive=*/true, error, contended);
  if (!handle.has_value()) {
    return std::nullopt;
  }
  return FileLock(*handle);
}

std::optional<FileLock> FileLock::tryShared(const std::filesystem::path& path, std::string* error) {
  auto handle = tryAcquireHandle(path, /*exclusive=*/false, error, nullptr);
  if (!handle.has_value()) {
    return std::nullopt;
  }
  return FileLock(*handle);
}

FileLock::FileLock(FileLock&& other) noexcept : handle_(std::exchange(other.handle_, -1)) {}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
  if (this != &other) {
    release();
    handle_ = std::exchange(other.handle_, -1);
  }
  return *this;
}

FileLock::~FileLock() {
  release();
}

bool FileLock::downgradeToShared(std::string* error) {
  if (handle_ == -1) {
    if (error != nullptr) {
      *error = "downgrade on a released lock";
    }
    return false;
  }
#if defined(_WIN32)
  // No in-place conversion on Windows: unlock, then re-lock shared on the
  // SAME handle (FAIL_IMMEDIATELY). The window between the two calls is the
  // documented non-atomicity; on failure the handle is closed (released).
  const HANDLE handle = reinterpret_cast<HANDLE>(handle_);
  OVERLAPPED overlapped{};
  ::UnlockFileEx(handle, 0, 1, 0, &overlapped);
  OVERLAPPED relock{};
  if (::LockFileEx(handle, LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &relock) == 0) {
    ::CloseHandle(handle);
    handle_ = -1;
    if (error != nullptr) {
      *error = "shared re-lock lost the conversion race";
    }
    return false;
  }
  return true;
#else
  // flock(2): "Converting a lock ... is not guaranteed to be atomic: the
  // existing lock is first removed, and then a new lock is established" —
  // with LOCK_NB a concurrent exclusive try can win that window, failing
  // this call; the lock state is then unknown, so release outright.
  if (::flock(static_cast<int>(handle_), LOCK_SH | LOCK_NB) != 0) {
    const int flock_errno = errno;
    release();
    if (error != nullptr) {
      *error = "shared downgrade lost the conversion race: " +
               std::error_code(flock_errno, std::generic_category()).message();
    }
    return false;
  }
  return true;
#endif
}

void FileLock::release() {
  if (handle_ == -1) {
    return;
  }
#if defined(_WIN32)
  const HANDLE handle = reinterpret_cast<HANDLE>(handle_);
  OVERLAPPED overlapped{};
  ::UnlockFileEx(handle, 0, 1, 0, &overlapped);
  ::CloseHandle(handle);
#else
  ::close(static_cast<int>(handle_));  // close(2) drops the flock
#endif
  handle_ = -1;
}

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
