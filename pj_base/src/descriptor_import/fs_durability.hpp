// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Private chmod/mkdir/fsync primitives behind the cache's durable publish.
// The platform fsync incantations are a classic source of subtle
// cross-platform bugs, so they live in exactly one TU.
#pragma once

#include <filesystem>
#include <string>

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

/// Apply 0600 (owner read/write) to `file`. Best-effort: failures are
/// swallowed (e.g. filesystems that don't carry POSIX bits).
void chmod0600(const std::filesystem::path& file);

/// Create `dir` (and parents) and tighten it to 0700 (owner only). Best-effort.
void ensureDir0700(const std::filesystem::path& dir);

/// fsync `file`'s contents to stable storage (Windows: FlushFileBuffers).
/// False on failure, with the reason in `error` when non-null.
[[nodiscard]] bool syncFile(const std::filesystem::path& file, std::string* error);

/// Best-effort fsync of the directory entry so a just-published rename
/// survives a crash. POSIX only — Windows has no directory-fsync equivalent
/// (metadata durability rides the NTFS journal); a no-op there.
void syncDir(const std::filesystem::path& dir);

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
