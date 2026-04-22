#pragma once

/**
 * @file plugin_catalog.hpp
 * @brief Pre-dlopen plugin discovery via `.pjmanifest.json` sidecars.
 *
 * Each v4 plugin DSO ships with a sidecar JSON file written next to it by
 * CMake's pj_emit_plugin_manifest helper. The host scans a directory for
 * these sidecars at startup, building a catalog of what's available —
 * WITHOUT dlopen'ing any DSO. dlopen happens only when the user actually
 * activates a plugin.
 *
 * This matters at scale: at 20-50 plugins the cold-start cost of dlopen'ing
 * every candidate (for file-extension filters, parser encodings, toolbox
 * menus, etc.) becomes noticeable and noisy. The sidecar scan keeps
 * startup proportional to the number of JSON files, not the number of
 * shared libraries.
 *
 * On activation, the host dlopens the DSO, calls get_plugin_manifest(),
 * and verifies the runtime manifest matches the sidecar. Mismatch is a
 * warning, not a fatal error — DSO truth wins.
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ {

/// Plugin family as advertised by the sidecar's "family" key.
enum class PluginFamily : uint32_t {
  kUnknown = 0,
  kDataSource = 1,
  kMessageParser = 2,
  kToolbox = 3,
  kDialog = 4,
};

/// Plugin descriptor parsed from a single `.pjmanifest.json` sidecar.
/// All fields except `dso_path`, `abi_major`, `family`, `name`, and
/// `version` are optional and may be empty.
struct PluginDescriptor {
  std::filesystem::path sidecar_path;
  std::filesystem::path dso_path;  // inferred as sidecar_path minus ".pjmanifest.json" plus platform DSO suffix

  uint32_t abi_major = 0;
  PluginFamily family = PluginFamily::kUnknown;

  std::string name;
  std::string version;
  std::string description;
  std::string category;
  std::string encoding;                      ///< for message parsers
  std::vector<std::string> file_extensions;  ///< for data sources
  std::vector<std::string> capabilities;     ///< optional capability tags
};

/// Scan a directory (non-recursive) for `*.pjmanifest.json` sidecars and
/// return the parsed descriptors. Invalid sidecars are skipped silently.
/// Returns an error only for filesystem-level problems (missing/unreadable
/// directory).
///
/// Does NOT dlopen anything.
[[nodiscard]] Expected<std::vector<PluginDescriptor>> scanPluginSidecars(const std::filesystem::path& directory);

/// Human-readable name for a family. Inverse of the string used in the sidecar.
[[nodiscard]] std::string_view toString(PluginFamily family) noexcept;

}  // namespace PJ
