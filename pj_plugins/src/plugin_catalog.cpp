#include "pj_plugins/host/plugin_catalog.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <system_error>

namespace PJ {

namespace {

constexpr std::string_view kSidecarSuffix = ".pjmanifest.json";

#if defined(_WIN32)
constexpr std::string_view kDsoSuffix = ".dll";
#elif defined(__APPLE__)
constexpr std::string_view kDsoSuffix = ".dylib";
#else
constexpr std::string_view kDsoSuffix = ".so";
#endif

PluginFamily parseFamily(std::string_view s) noexcept {
  if (s == "data_source") {
    return PluginFamily::kDataSource;
  }
  if (s == "message_parser") {
    return PluginFamily::kMessageParser;
  }
  if (s == "toolbox") {
    return PluginFamily::kToolbox;
  }
  if (s == "dialog") {
    return PluginFamily::kDialog;
  }
  return PluginFamily::kUnknown;
}

/// Best-effort decode of a single sidecar file. Returns empty optional on
/// anything malformed (missing required keys, JSON parse error, etc.).
std::optional<PluginDescriptor> decodeSidecar(const std::filesystem::path& sidecar_path) {
  std::ifstream in(sidecar_path);
  if (!in) {
    return std::nullopt;
  }
  nlohmann::json j;
  try {
    in >> j;
  } catch (const nlohmann::json::parse_error&) {
    return std::nullopt;
  }
  if (!j.is_object()) {
    return std::nullopt;
  }

  PluginDescriptor d;
  d.sidecar_path = sidecar_path;

  // Required keys. Reject sidecars that are missing any of these.
  if (!j.contains("name") || !j["name"].is_string()) {
    return std::nullopt;
  }
  if (!j.contains("version") || !j["version"].is_string()) {
    return std::nullopt;
  }
  if (!j.contains("abi_major") || !j["abi_major"].is_number_integer()) {
    return std::nullopt;
  }
  if (!j.contains("family") || !j["family"].is_string()) {
    return std::nullopt;
  }

  d.name = j["name"].get<std::string>();
  d.version = j["version"].get<std::string>();
  d.abi_major = j["abi_major"].get<uint32_t>();
  d.family = parseFamily(j["family"].get<std::string>());
  if (d.family == PluginFamily::kUnknown) {
    return std::nullopt;
  }

  // Optional fields.
  if (j.contains("description") && j["description"].is_string()) {
    d.description = j["description"].get<std::string>();
  }
  if (j.contains("category") && j["category"].is_string()) {
    d.category = j["category"].get<std::string>();
  }
  if (j.contains("encoding") && j["encoding"].is_string()) {
    d.encoding = j["encoding"].get<std::string>();
  }
  if (j.contains("file_extensions") && j["file_extensions"].is_array()) {
    for (const auto& e : j["file_extensions"]) {
      if (e.is_string()) {
        d.file_extensions.push_back(e.get<std::string>());
      }
    }
  }
  if (j.contains("capabilities") && j["capabilities"].is_array()) {
    for (const auto& c : j["capabilities"]) {
      if (c.is_string()) {
        d.capabilities.push_back(c.get<std::string>());
      }
    }
  }

  // Infer the DSO path: sidecar is "<stem>.pjmanifest.json"; DSO is
  // "<stem><platform_suffix>". On Linux, plugin DSOs built by us are
  // usually "lib<stem>.so", but our CMake put them in the same directory
  // without the "lib" prefix handling. Try both.
  const auto stem_wo_ext = sidecar_path.stem().stem();  // drop ".pjmanifest" then ".json"
  auto parent = sidecar_path.parent_path();
  std::filesystem::path candidate = parent / (stem_wo_ext.string() + std::string(kDsoSuffix));
  if (std::filesystem::exists(candidate)) {
    d.dso_path = candidate;
  } else {
    candidate = parent / (std::string("lib") + stem_wo_ext.string() + std::string(kDsoSuffix));
    if (std::filesystem::exists(candidate)) {
      d.dso_path = candidate;
    } else {
      // Leave dso_path empty — host will note "DSO not found for sidecar".
      d.dso_path = parent / (stem_wo_ext.string() + std::string(kDsoSuffix));
    }
  }

  return d;
}

}  // namespace

std::string_view toString(PluginFamily family) noexcept {
  switch (family) {
    case PluginFamily::kDataSource:
      return "data_source";
    case PluginFamily::kMessageParser:
      return "message_parser";
    case PluginFamily::kToolbox:
      return "toolbox";
    case PluginFamily::kDialog:
      return "dialog";
    case PluginFamily::kUnknown:
      return "unknown";
  }
  return "unknown";
}

Expected<std::vector<PluginDescriptor>> scanPluginSidecars(const std::filesystem::path& directory) {
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) {
    return unexpected(std::string("plugin directory does not exist: ") + directory.string());
  }
  if (!std::filesystem::is_directory(directory, ec)) {
    return unexpected(std::string("plugin path is not a directory: ") + directory.string());
  }

  std::vector<PluginDescriptor> result;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto& path = entry.path();
    const auto name = path.filename().string();
    if (name.size() < kSidecarSuffix.size()) {
      continue;
    }
    if (!std::equal(kSidecarSuffix.rbegin(), kSidecarSuffix.rend(), name.rbegin())) {
      continue;
    }
    if (auto d = decodeSidecar(path); d.has_value()) {
      result.push_back(std::move(*d));
    }
  }

  // Deterministic order for reproducible catalogs.
  std::sort(result.begin(), result.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    return a.sidecar_path < b.sidecar_path;
  });

  return result;
}

}  // namespace PJ
