#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ::detail {

/// One descriptor blob recovered from a DSO's plugin-descriptor section.
struct EmbeddedDescriptor {
  /// PJ_ABI_VERSION the plugin was compiled against. Reported rather than
  /// enforced here: the caller decides whether a mismatch rejects the plugin.
  uint32_t abi_version = 0;
  /// A PJ::detail::PluginDescriptorFamily value.
  uint32_t family = 0;
  std::string manifest_json;
};

/// Extracts every descriptor blob from a plugin DSO on disk without loading it,
/// so no plugin code runs and the process never maps the image.
///
/// Only the container headers and the descriptor section are read — inspecting
/// a 50 MB plugin costs a few KB, not a full slurp.
///
/// An empty vector means the image parsed cleanly but carries no descriptor
/// section: a plugin built before the section existed, or one with a
/// hand-written vtable. Callers treat that as "fall back to dlopen", which is
/// why it is a value and not an error. An error means the container itself
/// could not be read (missing or truncated file, unsupported format).
[[nodiscard]] Expected<std::vector<EmbeddedDescriptor>> readEmbeddedDescriptors(const std::filesystem::path& dso_path);

/// Same, over an image already in memory. Used by the format tests, which build
/// synthetic ELF/PE/Mach-O containers rather than shipping binary fixtures.
[[nodiscard]] Expected<std::vector<EmbeddedDescriptor>> readEmbeddedDescriptors(std::span<const std::byte> image);

}  // namespace PJ::detail
