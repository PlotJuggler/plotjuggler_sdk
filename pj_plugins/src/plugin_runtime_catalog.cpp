// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/plugin_runtime_catalog.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

#include "pj_base/data_source_protocol.h"
#include "pj_base/toolbox_protocol.h"

namespace PJ {

namespace {

std::string canonicalPath(const std::filesystem::path& path) {
  std::error_code ec;
  auto canon = std::filesystem::weakly_canonical(path, ec);
  return (ec ? path : canon).string();
}

std::filesystem::file_time_type safeMtime(const std::filesystem::path& path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  return ec ? std::filesystem::file_time_type{} : mtime;
}

std::string normalizeExtension(std::string ext) {
  if (!ext.empty() && ext.front() != '.') {
    ext.insert(ext.begin(), '.');
  }
  return ext;
}

template <typename PluginT>
std::vector<PluginT*> mutablePtrs(std::vector<PluginT>& plugins, uint64_t capability) {
  std::vector<PluginT*> out;
  for (auto& plugin : plugins) {
    if ((plugin.capabilities & capability) != 0) {
      out.push_back(&plugin);
    }
  }
  return out;
}

template <typename PluginT>
std::vector<const PluginT*> constPtrs(const std::vector<PluginT>& plugins, uint64_t capability) {
  std::vector<const PluginT*> out;
  for (const auto& plugin : plugins) {
    if ((plugin.capabilities & capability) != 0) {
      out.push_back(&plugin);
    }
  }
  return out;
}

}  // namespace

PluginRuntimeCatalog::PluginRuntimeCatalog(
    std::filesystem::path plugin_dir, DiagnosticSink sink, std::string diagnostic_source)
    : plugin_dir_(std::move(plugin_dir)), sink_(std::move(sink)), diagnostic_source_(std::move(diagnostic_source)) {}

void PluginRuntimeCatalog::setPluginDir(std::filesystem::path plugin_dir) {
  plugin_dir_ = std::move(plugin_dir);
}

void PluginRuntimeCatalog::setDiagnosticSink(DiagnosticSink sink) {
  sink_ = std::move(sink);
}

void PluginRuntimeCatalog::scanDirectory() {
  data_sources_.clear();
  message_parsers_.clear();
  toolbox_plugins_.clear();

  auto scan = scanPluginDsos(plugin_dir_);
  if (!scan) {
    report(DiagnosticLevel::kError, {}, scan.error());
    return;
  }
  reportScanDiagnostics(*scan);

  for (const PluginDescriptor& descriptor : scan->plugins) {
    if (!loadAndRegister(descriptor)) {
      report(
          DiagnosticLevel::kError, descriptor.id,
          descriptor.dso_path.string() + ": failed to load " + std::string(toString(descriptor.family)) + " plugin");
    }
  }
}

bool PluginRuntimeCatalog::reload() {
  auto scan = scanPluginDsos(plugin_dir_);
  if (!scan) {
    report(DiagnosticLevel::kError, {}, scan.error());
    return false;
  }
  reportScanDiagnostics(*scan);

  std::vector<std::string> on_disk;
  on_disk.reserve(scan->plugins.size());
  for (const PluginDescriptor& descriptor : scan->plugins) {
    on_disk.push_back(canonicalPath(descriptor.dso_path));
  }

  bool changed = false;
  auto drop_missing = [&](auto& vec, std::string_view family) {
    const auto before = vec.size();
    std::erase_if(vec, [&](const auto& plugin) {
      const bool gone = std::find(on_disk.begin(), on_disk.end(), plugin.path) == on_disk.end();
      if (gone) {
        report(DiagnosticLevel::kInfo, plugin.id, "Unloaded " + std::string(family) + ": " + plugin.path);
      }
      return gone;
    });
    changed = changed || vec.size() != before;
  };
  drop_missing(data_sources_, "DataSource");
  drop_missing(message_parsers_, "MessageParser");
  drop_missing(toolbox_plugins_, "Toolbox");

  for (const PluginDescriptor& descriptor : scan->plugins) {
    const std::string path = canonicalPath(descriptor.dso_path);
    const auto disk_mtime = safeMtime(descriptor.dso_path);
    if (disk_mtime == std::filesystem::file_time_type{}) {
      report(DiagnosticLevel::kWarning, descriptor.id, descriptor.dso_path.string() + ": could not read mtime");
      continue;
    }

    const auto prior_mtime = loadedMtimeForPath(path);
    const bool already_loaded = prior_mtime != std::filesystem::file_time_type{};
    if (already_loaded && disk_mtime <= prior_mtime) {
      continue;
    }
    if (already_loaded) {
      evictByPath(path);
      changed = true;
    }
    if (loadAndRegister(descriptor)) {
      changed = true;
    } else {
      report(
          DiagnosticLevel::kError, descriptor.id,
          descriptor.dso_path.string() + ": failed to load " + std::string(toString(descriptor.family)) + " plugin");
    }
  }

  return changed;
}

bool PluginRuntimeCatalog::loadAndRegister(const PluginDescriptor& descriptor) {
  switch (descriptor.family) {
    case PluginFamily::kDataSource:
      return loadAndRegisterDataSource(descriptor);
    case PluginFamily::kMessageParser:
      return loadAndRegisterMessageParser(descriptor);
    case PluginFamily::kToolbox:
      return loadAndRegisterToolbox(descriptor);
    case PluginFamily::kDialog:
      report(
          DiagnosticLevel::kWarning, descriptor.id,
          descriptor.dso_path.string() + ": standalone dialog plugin \"" + descriptor.name +
              "\" discovered; dialogs are loaded through owning plugins");
      return false;
    case PluginFamily::kUnknown:
      break;
  }
  return false;
}

bool PluginRuntimeCatalog::loadAndRegisterDataSource(const PluginDescriptor& descriptor) {
  auto result = DataSourceLibrary::load(descriptor.dso_path.string());
  if (!result) {
    report(DiagnosticLevel::kError, descriptor.id, descriptor.dso_path.string() + ": " + result.error());
    return false;
  }

  RuntimeDataSourcePlugin loaded;
  loaded.library = std::move(*result);
  loaded.path = canonicalPath(descriptor.dso_path);
  loaded.loaded_mtime = safeMtime(descriptor.dso_path);
  loaded.id = descriptor.id;
  loaded.name = descriptor.name;
  loaded.version = descriptor.version;
  loaded.capabilities = loaded.library.createHandle().capabilities();

  // Fail-fast on plugins that lie about kCapabilityHasDialog: a misbuilt
  // plugin that advertises the bit but doesn't export the dialog vtable
  // would otherwise reach the host's dialog flow and silently degrade to
  // "no dialog", confusing the user. Block it here so the broken plugin
  // never enters the loaded set.
  if ((loaded.capabilities & PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG) != 0) {
    auto vt = loaded.library.resolveDialogVtable();
    if (!vt) {
      report(
          DiagnosticLevel::kError, descriptor.id,
          descriptor.dso_path.string() + ": advertises kCapabilityHasDialog but " + vt.error());
      return false;
    }
  }

  loaded.file_extensions.reserve(descriptor.file_extensions.size());
  for (const auto& ext : descriptor.file_extensions) {
    loaded.file_extensions.push_back(normalizeExtension(ext));
  }

  report(DiagnosticLevel::kInfo, loaded.id, "Loaded DataSource " + loaded.name + " from " + loaded.path);
  data_sources_.push_back(std::move(loaded));
  return true;
}

bool PluginRuntimeCatalog::loadAndRegisterMessageParser(const PluginDescriptor& descriptor) {
  auto result = MessageParserLibrary::load(descriptor.dso_path.string());
  if (!result) {
    report(DiagnosticLevel::kError, descriptor.id, descriptor.dso_path.string() + ": " + result.error());
    return false;
  }

  RuntimeMessageParserPlugin loaded;
  loaded.library = std::move(*result);
  loaded.path = canonicalPath(descriptor.dso_path);
  loaded.loaded_mtime = safeMtime(descriptor.dso_path);
  loaded.id = descriptor.id;
  loaded.name = descriptor.name;
  loaded.version = descriptor.version;
  loaded.encodings.insert(loaded.encodings.end(), descriptor.encoding.begin(), descriptor.encoding.end());

  report(DiagnosticLevel::kInfo, loaded.id, "Loaded MessageParser " + loaded.name + " from " + loaded.path);
  message_parsers_.push_back(std::move(loaded));
  return true;
}

bool PluginRuntimeCatalog::loadAndRegisterToolbox(const PluginDescriptor& descriptor) {
  auto result = ToolboxLibrary::load(descriptor.dso_path.string());
  if (!result) {
    report(DiagnosticLevel::kError, descriptor.id, descriptor.dso_path.string() + ": " + result.error());
    return false;
  }

  RuntimeToolboxPlugin loaded;
  loaded.library = std::move(*result);
  loaded.path = canonicalPath(descriptor.dso_path);
  loaded.loaded_mtime = safeMtime(descriptor.dso_path);
  loaded.id = descriptor.id;
  loaded.name = descriptor.name;
  loaded.version = descriptor.version;
  loaded.capabilities = loaded.library.createHandle().capabilities();

  // Same fail-fast contract as DataSource above: kToolboxCapabilityHasDialog
  // requires an exported dialog vtable.
  if ((loaded.capabilities & PJ_TOOLBOX_CAPABILITY_HAS_DIALOG) != 0) {
    auto vt = loaded.library.resolveDialogVtable();
    if (!vt) {
      report(
          DiagnosticLevel::kError, descriptor.id,
          descriptor.dso_path.string() + ": advertises kToolboxCapabilityHasDialog but " + vt.error());
      return false;
    }
  }

  report(DiagnosticLevel::kInfo, loaded.id, "Loaded Toolbox " + loaded.name + " from " + loaded.path);
  toolbox_plugins_.push_back(std::move(loaded));
  return true;
}

bool PluginRuntimeCatalog::evictByPath(const std::string& path) {
  bool removed = false;
  auto erase_path = [&](auto& vec) {
    const auto before = vec.size();
    std::erase_if(vec, [&](const auto& plugin) { return plugin.path == path; });
    removed = removed || vec.size() != before;
  };
  erase_path(data_sources_);
  erase_path(message_parsers_);
  erase_path(toolbox_plugins_);
  return removed;
}

std::filesystem::file_time_type PluginRuntimeCatalog::loadedMtimeForPath(const std::string& path) const {
  auto find_mtime = [&](const auto& vec) {
    auto it = std::find_if(vec.begin(), vec.end(), [&](const auto& plugin) { return plugin.path == path; });
    return it == vec.end() ? std::filesystem::file_time_type{} : it->loaded_mtime;
  };
  if (auto mtime = find_mtime(data_sources_); mtime != std::filesystem::file_time_type{}) {
    return mtime;
  }
  if (auto mtime = find_mtime(message_parsers_); mtime != std::filesystem::file_time_type{}) {
    return mtime;
  }
  return find_mtime(toolbox_plugins_);
}

std::vector<RuntimeDataSourcePlugin*> PluginRuntimeCatalog::fileImportSources() {
  return mutablePtrs(data_sources_, PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT);
}

std::vector<const RuntimeDataSourcePlugin*> PluginRuntimeCatalog::fileImportSources() const {
  return constPtrs(data_sources_, PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT);
}

std::vector<RuntimeDataSourcePlugin*> PluginRuntimeCatalog::streamSources() {
  return mutablePtrs(data_sources_, PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM);
}

std::vector<const RuntimeDataSourcePlugin*> PluginRuntimeCatalog::streamSources() const {
  return constPtrs(data_sources_, PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM);
}

std::vector<RuntimeDataSourcePlugin*> PluginRuntimeCatalog::findSourcesForExtension(std::string_view ext) {
  std::vector<RuntimeDataSourcePlugin*> out;
  for (auto& source : data_sources_) {
    if ((source.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT) == 0) {
      continue;
    }
    if (std::find(source.file_extensions.begin(), source.file_extensions.end(), ext) != source.file_extensions.end()) {
      out.push_back(&source);
    }
  }
  return out;
}

std::vector<const RuntimeDataSourcePlugin*> PluginRuntimeCatalog::findSourcesForExtension(std::string_view ext) const {
  std::vector<const RuntimeDataSourcePlugin*> out;
  for (const auto& source : data_sources_) {
    if ((source.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT) == 0) {
      continue;
    }
    if (std::find(source.file_extensions.begin(), source.file_extensions.end(), ext) != source.file_extensions.end()) {
      out.push_back(&source);
    }
  }
  return out;
}

RuntimeMessageParserPlugin* PluginRuntimeCatalog::findParserByEncoding(std::string_view encoding) {
  for (auto& parser : message_parsers_) {
    if (std::find(parser.encodings.begin(), parser.encodings.end(), encoding) != parser.encodings.end()) {
      return &parser;
    }
  }
  return nullptr;
}

const RuntimeMessageParserPlugin* PluginRuntimeCatalog::findParserByEncoding(std::string_view encoding) const {
  for (const auto& parser : message_parsers_) {
    if (std::find(parser.encodings.begin(), parser.encodings.end(), encoding) != parser.encodings.end()) {
      return &parser;
    }
  }
  return nullptr;
}

std::string PluginRuntimeCatalog::buildFileFilter() const {
  std::string all_exts;
  std::string per_plugin;
  for (const auto& source : data_sources_) {
    if ((source.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT) == 0 || source.file_extensions.empty()) {
      continue;
    }

    if (!per_plugin.empty()) {
      per_plugin += ";;";
    }
    per_plugin += source.name + " (";
    for (size_t i = 0; i < source.file_extensions.size(); ++i) {
      if (i > 0) {
        per_plugin += " ";
      }
      per_plugin += "*" + source.file_extensions[i];
      if (!all_exts.empty()) {
        all_exts += " ";
      }
      all_exts += "*" + source.file_extensions[i];
    }
    per_plugin += ")";
  }

  std::string filter;
  if (!all_exts.empty()) {
    filter = "All supported files (" + all_exts + ")";
    if (!per_plugin.empty()) {
      filter += ";;" + per_plugin;
    }
  } else {
    filter = per_plugin;
  }
  if (!filter.empty()) {
    filter += ";;";
  }
  filter += "All files (*)";
  return filter;
}

std::string PluginRuntimeCatalog::listAvailableEncodings() const {
  std::vector<std::string> unique_encodings;
  for (const auto& parser : message_parsers_) {
    for (const auto& encoding : parser.encodings) {
      if (std::find(unique_encodings.begin(), unique_encodings.end(), encoding) == unique_encodings.end()) {
        unique_encodings.push_back(encoding);
      }
    }
  }

  return nlohmann::json(unique_encodings).dump();
}

void PluginRuntimeCatalog::report(DiagnosticLevel level, const std::string& id, std::string message) const {
  if (!sink_) {
    return;
  }
  sink_(Diagnostic{level, diagnostic_source_, id, std::move(message), std::chrono::system_clock::now()});
}

void PluginRuntimeCatalog::reportScanDiagnostics(const PluginScanResult& scan) const {
  for (const auto& diagnostic : scan.diagnostics) {
    report(DiagnosticLevel::kError, {}, diagnostic.path.string() + ": " + diagnostic.message);
  }
}

}  // namespace PJ
