#include "pj_plugins/host/plugin_catalog.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "detail/library_loader.hpp"
#include "pj_base/data_source_protocol.h"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/toolbox_protocol.h"
#include "pj_plugins/dialog_protocol.h"

namespace PJ {

namespace {

#if defined(_WIN32)
constexpr std::string_view kDsoSuffix = ".dll";
#elif defined(__APPLE__)
constexpr std::string_view kDsoSuffix = ".dylib";
#else
constexpr std::string_view kDsoSuffix = ".so";
#endif

struct ManifestCandidate {
  PluginFamily family = PluginFamily::kUnknown;
  std::string manifest_json;
};

struct LibraryHandleCloser {
  void operator()(void* handle) const {
    detail::closeLibraryHandle(handle);
  }
};

bool hasDsoSuffix(const std::filesystem::path& path) {
  return path.extension().string() == kDsoSuffix;
}

// Direct-vtable families share the exact same probe sequence: resolve symbol,
// call entry, check protocol_version, check struct_size, read manifest_json.
// Only the family-specific types and constants vary.
template <typename Vtable, typename EntryFn>
Expected<ManifestCandidate> probeDirectVtable(
    void* handle, const char* symbol, const char* family_name, uint32_t expected_protocol, size_t min_vtable_size,
    PluginFamily family) {
  auto sym = detail::resolveSymbol(handle, symbol);
  if (!sym) {
    return unexpected(sym.error());
  }
  const Vtable* vt = reinterpret_cast<EntryFn>(*sym)();
  if (vt == nullptr) {
    return unexpected(std::string(symbol) + " returned null");
  }
  if (vt->protocol_version != expected_protocol) {
    return unexpected(std::string(family_name) + " protocol version mismatch");
  }
  if (vt->struct_size < min_vtable_size) {
    return unexpected(std::string(family_name) + " vtable smaller than v4.0 baseline");
  }
  return ManifestCandidate{family, vt->manifest_json == nullptr ? "" : vt->manifest_json};
}

Expected<ManifestCandidate> tryDataSource(void* handle) {
  return probeDirectVtable<PJ_data_source_vtable_t, PJ_get_data_source_vtable_fn>(
      handle, "PJ_get_data_source_vtable", "DataSource", PJ_DATA_SOURCE_PROTOCOL_VERSION,
      PJ_DATA_SOURCE_MIN_VTABLE_SIZE, PluginFamily::kDataSource);
}

Expected<ManifestCandidate> tryMessageParser(void* handle) {
  return probeDirectVtable<PJ_message_parser_vtable_t, PJ_get_message_parser_vtable_fn>(
      handle, "PJ_get_message_parser_vtable", "MessageParser", PJ_MESSAGE_PARSER_PROTOCOL_VERSION,
      PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE, PluginFamily::kMessageParser);
}

Expected<ManifestCandidate> tryToolbox(void* handle) {
  return probeDirectVtable<PJ_toolbox_vtable_t, PJ_get_toolbox_vtable_fn>(
      handle, "PJ_get_toolbox_vtable", "Toolbox", PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION, PJ_TOOLBOX_MIN_VTABLE_SIZE,
      PluginFamily::kToolbox);
}

Expected<ManifestCandidate> tryDialog(void* handle) {
  auto sym = detail::resolveSymbol(handle, "PJ_get_dialog_vtable");
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_dialog_vtable_fn>(*sym);
  const PJ_dialog_vtable_t* vt = entry();
  if (vt == nullptr) {
    return unexpected(std::string("PJ_get_dialog_vtable returned null"));
  }
  if (vt->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
    return unexpected(std::string("Dialog protocol version mismatch"));
  }
  if (vt->struct_size < sizeof(PJ_dialog_vtable_t)) {
    return unexpected(std::string("Dialog vtable smaller than v4.0 baseline"));
  }
  if (vt->create == nullptr || vt->destroy == nullptr || vt->get_manifest == nullptr) {
    return unexpected(std::string("Dialog vtable missing required lifecycle slots"));
  }

  void* ctx = vt->create();
  if (ctx == nullptr) {
    return unexpected(std::string("PJ_dialog_vtable_t::create returned null"));
  }
  const char* manifest = vt->get_manifest(ctx);
  std::string manifest_json = manifest == nullptr ? "" : manifest;
  vt->destroy(ctx);
  return ManifestCandidate{PluginFamily::kDialog, std::move(manifest_json)};
}

Expected<ManifestCandidate> findEmbeddedManifest(void* handle) {
  std::vector<std::string> errors;

  if (auto candidate = tryDataSource(handle)) {
    return *candidate;
  } else {
    errors.push_back("data_source: " + candidate.error());
  }

  if (auto candidate = tryMessageParser(handle)) {
    return *candidate;
  } else {
    errors.push_back("message_parser: " + candidate.error());
  }

  if (auto candidate = tryToolbox(handle)) {
    return *candidate;
  } else {
    errors.push_back("toolbox: " + candidate.error());
  }

  if (auto candidate = tryDialog(handle)) {
    return *candidate;
  } else {
    errors.push_back("dialog: " + candidate.error());
  }

  std::ostringstream out;
  out << "no supported plugin vtable found";
  for (const auto& error : errors) {
    out << "; " << error;
  }
  return unexpected(out.str());
}

Expected<std::vector<std::string>> readStringArray(const nlohmann::json& j, std::string_view key) {
  std::vector<std::string> values;
  const auto it = j.find(std::string(key));
  if (it == j.end()) {
    return values;
  }
  if (!it->is_array()) {
    return unexpected(std::string("plugin embedded manifest key must be an array of strings: ") + std::string(key));
  }
  for (const auto& value : *it) {
    if (!value.is_string()) {
      return unexpected(std::string("plugin embedded manifest key contains a non-string value: ") + std::string(key));
    }
    values.push_back(value.get<std::string>());
  }
  return values;
}

Expected<PluginDescriptor> decodeManifest(
    const std::filesystem::path& dso_path, PluginFamily family, std::string_view manifest_json) {
  if (manifest_json.empty()) {
    return unexpected(std::string("plugin embedded manifest is empty"));
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(manifest_json);
  } catch (const nlohmann::json::exception& e) {
    return unexpected(std::string("plugin embedded manifest is invalid JSON: ") + e.what());
  }

  if (!j.is_object()) {
    return unexpected(std::string("plugin embedded manifest must be a JSON object"));
  }

  auto requiredString = [&](std::string_view key) -> Expected<std::string> {
    const auto it = j.find(std::string(key));
    if (it == j.end() || !it->is_string() || it->get<std::string>().empty()) {
      return unexpected(std::string("plugin embedded manifest missing required string key: ") + std::string(key));
    }
    return it->get<std::string>();
  };
  auto optionalString = [&](std::string_view key) -> Expected<std::string> {
    const auto it = j.find(std::string(key));
    if (it == j.end()) {
      return std::string{};
    }
    if (!it->is_string()) {
      return unexpected(std::string("plugin embedded manifest key must be a string: ") + std::string(key));
    }
    return it->get<std::string>();
  };

  PluginDescriptor d;
  d.dso_path = dso_path;
  d.abi_major = PJ_ABI_VERSION;
  d.family = family;

  auto id = requiredString("id");
  if (!id) {
    return unexpected(id.error());
  }
  auto name = requiredString("name");
  if (!name) {
    return unexpected(name.error());
  }
  auto version = requiredString("version");
  if (!version) {
    return unexpected(version.error());
  }

  d.id = *id;
  d.name = *name;
  d.version = *version;

  auto description = optionalString("description");
  if (!description) {
    return unexpected(description.error());
  }
  auto category = optionalString("category");
  if (!category) {
    return unexpected(category.error());
  }
  auto file_extensions = readStringArray(j, "file_extensions");
  if (!file_extensions) {
    return unexpected(file_extensions.error());
  }
  auto capabilities = readStringArray(j, "capabilities");
  if (!capabilities) {
    return unexpected(capabilities.error());
  }

  d.description = *description;
  d.category = *category;
  d.file_extensions = *file_extensions;
  d.capabilities = *capabilities;

  if (family == PluginFamily::kMessageParser) {
    auto encoding = requiredString("encoding");
    if (!encoding) {
      return unexpected(encoding.error());
    }
    d.encoding = *encoding;
  } else {
    auto encoding = optionalString("encoding");
    if (!encoding) {
      return unexpected(encoding.error());
    }
    d.encoding = *encoding;
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

Expected<PluginDescriptor> inspectPluginDso(const std::filesystem::path& dso_path) {
  if (!hasDsoSuffix(dso_path)) {
    return unexpected(std::string("not a platform plugin DSO: ") + dso_path.string());
  }
  auto withPath = [&](const std::string& error) { return dso_path.string() + ": " + error; };

  auto handle = detail::loadLibraryHandle(dso_path.string());
  if (!handle) {
    return unexpected(withPath(handle.error()));
  }
  std::unique_ptr<void, LibraryHandleCloser> library(*handle);

  if (auto abi = detail::checkPluginAbiVersion(library.get()); !abi) {
    return unexpected(withPath(abi.error()));
  }

  auto candidate = findEmbeddedManifest(library.get());
  if (!candidate) {
    return unexpected(withPath(candidate.error()));
  }

  auto descriptor = decodeManifest(dso_path, candidate->family, candidate->manifest_json);
  if (!descriptor) {
    return unexpected(withPath(descriptor.error()));
  }
  return *descriptor;
}

Expected<PluginScanResult> scanPluginDsos(const std::filesystem::path& directory) {
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) {
    return unexpected(std::string("plugin directory not found: ") + directory.string());
  }
  if (!std::filesystem::is_directory(directory, ec)) {
    return unexpected(std::string("plugin path is not a directory: ") + directory.string());
  }

  PluginScanResult result;
  // Use the increment-with-error-code overload so a single inaccessible subtree
  // (e.g. one extension dir owned by another user) doesn't terminate the entire
  // scan and silently hide every later extension from the marketplace.
  std::filesystem::recursive_directory_iterator it(directory, ec);
  if (ec) {
    result.diagnostics.push_back({directory, "directory iteration failed: " + ec.message()});
    return result;
  }
  const std::filesystem::recursive_directory_iterator end;
  while (it != end) {
    const auto entry = *it;
    std::error_code entry_ec;
    const bool is_file = entry.is_regular_file(entry_ec);
    if (!entry_ec && is_file && hasDsoSuffix(entry.path())) {
      auto descriptor = inspectPluginDso(entry.path());
      if (descriptor) {
        result.plugins.push_back(std::move(*descriptor));
      } else {
        result.diagnostics.push_back({entry.path(), descriptor.error()});
      }
    } else if (entry_ec) {
      result.diagnostics.push_back({entry.path(), "stat failed: " + entry_ec.message()});
    }

    std::error_code inc_ec;
    it.increment(inc_ec);
    if (inc_ec) {
      result.diagnostics.push_back({entry.path(), "directory iteration failed: " + inc_ec.message()});
      // Skip the unreadable subtree but continue with the rest of the scan.
      it.pop();
    }
  }

  std::sort(result.plugins.begin(), result.plugins.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    return a.dso_path < b.dso_path;
  });
  std::sort(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const PluginDiagnostic& a, const PluginDiagnostic& b) { return a.path < b.path; });

  return result;
}

}  // namespace PJ
