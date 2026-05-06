#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

/**
 * @file plugin_catalog.hpp
 * @brief Plugin discovery from each DSO's embedded manifest.
 *
 * Plugin DSOs export a family-specific vtable whose `manifest_json` field is
 * the source of truth for local plugin metadata. Dialogs compiled against the
 * original v4.0 protocol may lack that tail slot; the scanner falls back to
 * their legacy `create()` + `get_manifest()` path. The scanner walks plugin
 * files, inspects those exports, parses the embedded manifest, and reports both
 * valid descriptors and diagnostics for candidates that could not be used.
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ {

/// Plugin family inferred from the DSO's exported protocol vtable.
enum class PluginFamily : uint32_t {
  kUnknown = 0,
  kDataSource = 1,
  kMessageParser = 2,
  kToolbox = 3,
  kDialog = 4,
};

/// Plugin descriptor parsed from a DSO's embedded manifest.
/// All fields except `dso_path`, `family`, `id`, `name`, and `version` are
/// optional and may be empty.
struct PluginDescriptor {
  std::filesystem::path dso_path;

  uint32_t abi_major = 0;
  PluginFamily family = PluginFamily::kUnknown;

  std::string id;
  std::string name;
  std::string version;
  std::string description;
  std::string category;
  std::vector<std::string> encoding;         ///< for message parsers (one or more)
  std::vector<std::string> file_extensions;  ///< for data sources
  std::vector<std::string> capabilities;     ///< optional capability tags
};

/// Diagnostic for a candidate DSO that could not produce a valid descriptor.
struct PluginDiagnostic {
  std::filesystem::path path;
  std::string message;
};

/// Result of a directory scan: valid descriptors plus per-DSO diagnostics.
struct PluginScanResult {
  std::vector<PluginDescriptor> plugins;
  std::vector<PluginDiagnostic> diagnostics;
};

/// Inspect one DSO and return its embedded plugin descriptor.
[[nodiscard]] Expected<PluginDescriptor> inspectPluginDso(const std::filesystem::path& dso_path);

/// Recursively scan a directory for platform plugin DSOs. Invalid candidates are
/// reported in diagnostics while discovery continues.
[[nodiscard]] Expected<PluginScanResult> scanPluginDsos(const std::filesystem::path& directory);

/// Human-readable name for a plugin family.
[[nodiscard]] std::string_view toString(PluginFamily family) noexcept;

}  // namespace PJ
