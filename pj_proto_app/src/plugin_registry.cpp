#include "plugin_registry.hpp"

#include <algorithm>

#include "pj_marketplace/platform_utils.hpp"
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace proto {

PluginRegistry::PluginRegistry(std::string_view plugin_dir) : plugin_dir_(plugin_dir) {}

std::optional<LoadedDataSource> PluginRegistry::tryLoadDataSource(const std::filesystem::path& so_path) {
  auto result = PJ::DataSourceLibrary::load(so_path.string());
  if (!result) {
    return std::nullopt;
  }
  LoadedDataSource loaded;
  loaded.library = std::move(*result);
  loaded.path = so_path.string();
  loaded.loaded_mtime = std::filesystem::last_write_time(so_path);

  auto handle = loaded.library.createHandle();
  loaded.capabilities = handle.capabilities();
  try {
    auto manifest = nlohmann::json::parse(handle.manifest());
    loaded.name = manifest.value("name", so_path.stem().string());
    if (manifest.contains("file_extensions")) {
      for (const auto& ext : manifest["file_extensions"]) {
        loaded.file_extensions.push_back(ext.get<std::string>());
      }
    }
  } catch (...) {
    loaded.name = so_path.stem().string();
  }
  std::cerr << "Loaded DataSource: " << loaded.name << " from " << loaded.path << "\n";
  return loaded;
}

std::optional<LoadedMessageParser> PluginRegistry::tryLoadMessageParser(const std::filesystem::path& so_path) {
  auto result = PJ::MessageParserLibrary::load(so_path.string());
  if (!result) {
    return std::nullopt;
  }
  LoadedMessageParser loaded;
  loaded.library = std::move(*result);
  loaded.path = so_path.string();
  loaded.loaded_mtime = std::filesystem::last_write_time(so_path);

  auto handle = loaded.library.createHandle();
  try {
    auto manifest = nlohmann::json::parse(handle.manifest());
    loaded.name = manifest.value("name", so_path.stem().string());
    // Helper to push encoding(s) from a JSON value (string or array of strings)
    auto push_encodings = [&](const nlohmann::json& enc) {
      if (enc.is_string()) {
        loaded.encodings.push_back(enc.get<std::string>());
      } else if (enc.is_array()) {
        for (const auto& e : enc) {
          if (e.is_string()) {
            loaded.encodings.push_back(e.get<std::string>());
          }
        }
      }
    };
    // Primary encoding field
    if (manifest.contains("encoding")) {
      push_encodings(manifest["encoding"]);
    }
    // Additional encoding aliases (e.g., "ros1"/"ros2" for ROS parser)
    if (manifest.contains("additional_encodings")) {
      push_encodings(manifest["additional_encodings"]);
    }
  } catch (...) {
    loaded.name = so_path.stem().string();
  }
  std::cerr << "Loaded MessageParser: " << loaded.name << " from " << loaded.path << "\n";
  return loaded;
}

void PluginRegistry::scanDirectory() {
  namespace fs = std::filesystem;

  if (!fs::is_directory(plugin_dir_)) {
    std::cerr << "Plugin directory not found: " << plugin_dir_ << "\n";
    return;
  }

  for (const auto& entry : fs::recursive_directory_iterator(plugin_dir_)) {
    if (!entry.is_regular_file() ||
        entry.path().extension() != PJ::PlatformUtils::pluginExtension()) {
      continue;
    }
    if (auto ds = tryLoadDataSource(entry.path())) {
      data_sources_.push_back(std::move(*ds));
    } else if (auto mp = tryLoadMessageParser(entry.path())) {
      message_parsers_.push_back(std::move(*mp));
    } else {
      std::cerr << "Failed to load plugin: " << entry.path() << "\n";
    }
  }
}

void PluginRegistry::reload() {
  namespace fs = std::filesystem;

  if (!fs::is_directory(plugin_dir_)) {
    std::cerr << "Plugin directory not found: " << plugin_dir_ << "\n";
    return;
  }

  // Collect all plugin files currently on disk
  std::vector<fs::path> on_disk;
  for (const auto& entry : fs::recursive_directory_iterator(plugin_dir_)) {
    if (entry.is_regular_file() &&
        entry.path().extension() == PJ::PlatformUtils::pluginExtension()) {
      on_disk.push_back(entry.path());
    }
  }

  // Remove entries whose .so no longer exists on disk
  auto is_gone = [&](const std::string& p) {
    return std::none_of(on_disk.begin(), on_disk.end(),
                        [&](const fs::path& dp) { return dp.string() == p; });
  };
  std::erase_if(data_sources_, [&](const LoadedDataSource& ds) {
    if (is_gone(ds.path)) {
      std::cerr << "Unloaded DataSource (removed): " << ds.path << "\n";
      return true;
    }
    return false;
  });
  std::erase_if(message_parsers_, [&](const LoadedMessageParser& mp) {
    if (is_gone(mp.path)) {
      std::cerr << "Unloaded MessageParser (removed): " << mp.path << "\n";
      return true;
    }
    return false;
  });

  // Load new .so files; reload modified ones
  for (const auto& so_path : on_disk) {
    const std::string path_str = so_path.string();
    const auto disk_mtime = fs::last_write_time(so_path);

    auto ds_it = std::find_if(data_sources_.begin(), data_sources_.end(),
                              [&](const LoadedDataSource& ds) { return ds.path == path_str; });
    if (ds_it != data_sources_.end()) {
      if (disk_mtime <= ds_it->loaded_mtime) {
        continue;
      }
      std::cerr << "Reloading updated DataSource: " << path_str << "\n";
      data_sources_.erase(ds_it);
    } else {
      auto mp_it = std::find_if(message_parsers_.begin(), message_parsers_.end(),
                                [&](const LoadedMessageParser& mp) { return mp.path == path_str; });
      if (mp_it != message_parsers_.end()) {
        if (disk_mtime <= mp_it->loaded_mtime) {
          continue;
        }
        std::cerr << "Reloading updated MessageParser: " << path_str << "\n";
        message_parsers_.erase(mp_it);
      }
    }

    if (auto ds = tryLoadDataSource(so_path)) {
      data_sources_.push_back(std::move(*ds));
    } else if (auto mp = tryLoadMessageParser(so_path)) {
      message_parsers_.push_back(std::move(*mp));
    } else {
      std::cerr << "Failed to load plugin: " << path_str << "\n";
    }
  }
}
std::vector<LoadedDataSource*> PluginRegistry::fileImportSources() {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT) {
      result.push_back(&ds);
    }
  }
  return result;
}

std::vector<LoadedDataSource*> PluginRegistry::streamSources() {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM) {
      result.push_back(&ds);
    }
  }
  return result;
}

std::string PluginRegistry::buildFileFilter() const {
  // Collect all extensions and per-plugin filters
  std::string all_exts;
  std::string per_plugin;
  for (const auto& ds : data_sources_) {
    if (!(ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT)) {
      continue;
    }
    if (ds.file_extensions.empty()) {
      continue;
    }

    if (!per_plugin.empty()) {
      per_plugin += ";;";
    }
    per_plugin += ds.name + " (";
    for (size_t i = 0; i < ds.file_extensions.size(); ++i) {
      if (i > 0) {
        per_plugin += " ";
      }
      per_plugin += "*" + ds.file_extensions[i];
      // Collect for "All supported" entry
      if (!all_exts.empty()) {
        all_exts += " ";
      }
      all_exts += "*" + ds.file_extensions[i];
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

std::vector<LoadedDataSource*> PluginRegistry::findSourcesForExtension(std::string_view ext) {
  std::vector<LoadedDataSource*> result;
  for (auto& ds : data_sources_) {
    if (!(ds.capabilities & PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT)) {
      continue;
    }
    for (const auto& supported_ext : ds.file_extensions) {
      if (supported_ext == ext) {
        result.push_back(&ds);
        break;
      }
    }
  }
  return result;
}

LoadedMessageParser* PluginRegistry::findParserByEncoding(std::string_view encoding) {
  for (auto& mp : message_parsers_) {
    for (const auto& enc : mp.encodings) {
      if (enc == encoding) {
        return &mp;
      }
    }
  }
  return nullptr;
}

}  // namespace proto
