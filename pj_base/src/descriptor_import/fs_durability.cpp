// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "descriptor_import/fs_durability.hpp"

#include <cerrno>
#include <system_error>

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
#include <unistd.h>
#endif

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

namespace fs = std::filesystem;

void chmod0600(const fs::path& file) {
  std::error_code ec;
  fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
}

void ensureDir0700(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
}

bool syncFile(const fs::path& file, std::string* error) {
#if defined(_WIN32)
  const HANDLE handle = ::CreateFileW(
      file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (error != nullptr) {
      *error = "could not reopen for flush: " + file.string();
    }
    return false;
  }
  const bool ok = ::FlushFileBuffers(handle) != 0;
  ::CloseHandle(handle);
  if (!ok && error != nullptr) {
    *error = "FlushFileBuffers failed: " + file.string();
  }
  return ok;
#else
  const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "could not reopen for fsync: " + file.string() + ": " +
               std::error_code(errno, std::generic_category()).message();
    }
    return false;
  }
  const bool ok = ::fsync(fd) == 0;
  const int fsync_errno = errno;
  ::close(fd);
  if (!ok && error != nullptr) {
    *error = "fsync failed: " + file.string() + ": " + std::error_code(fsync_errno, std::generic_category()).message();
  }
  return ok;
#endif
}

void syncDir(const fs::path& dir) {
#if !defined(_WIN32)
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  ::fsync(fd);
  ::close(fd);
#else
  (void)dir;
#endif
}

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
