#include "plugin_registry.hpp"

#include "pj_marketplace/platform_utils.hpp"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace proto {

PluginRegistry::PluginRegistry(std::string_view plugin_dir) : plugin_dir_(plugin_dir) {}

void PluginRegistry::scanDirectory() {
  namespace fs = std::filesystem;

  if (!fs::is_directory(plugin_dir_)) {
    std::cerr << "Plugin directory not found: " << plugin_dir_ << "\n";
    return;
  }

  for (const auto& entry : fs::directory_iterator(plugin_dir_)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto path = entry.path().string();
    if (entry.path().extension() != PJ::PlatformUtils::pluginExtension()) {
      continue;
    }

    // Try as DataSource
    auto ds_result = PJ::DataSourceLibrary::load(path);
    if (ds_result) {
      LoadedDataSource loaded;
      loaded.library = std::move(*ds_result);

      auto handle = loaded.library.createHandle();
      loaded.capabilities = handle.capabilities();

      auto manifest_str = handle.manifest();
      try {
        auto manifest = nlohmann::json::parse(manifest_str);
        loaded.name = manifest.value("name", entry.path().stem().string());
        if (manifest.contains("file_extensions")) {
          for (const auto& ext : manifest["file_extensions"]) {
            loaded.file_extensions.push_back(ext.get<std::string>());
          }
        }
      } catch (...) {
        loaded.name = entry.path().stem().string();
      }

      std::cerr << "Loaded DataSource: " << loaded.name << " from " << path << "\n";
      data_sources_.push_back(std::move(loaded));
      continue;
    }

    // Try as MessageParser
    auto mp_result = PJ::MessageParserLibrary::load(path);
    if (mp_result) {
      LoadedMessageParser loaded;
      loaded.library = std::move(*mp_result);

      auto handle = loaded.library.createHandle();
      auto manifest_str = handle.manifest();
      try {
        auto manifest = nlohmann::json::parse(manifest_str);
        loaded.name = manifest.value("name", entry.path().stem().string());
        // "encoding" can be a string or an array of strings
        if (manifest.contains("encoding")) {
          auto& enc = manifest["encoding"];
          if (enc.is_string()) {
            loaded.encodings.push_back(enc.get<std::string>());
          } else if (enc.is_array()) {
            for (const auto& e : enc) {
              if (e.is_string()) {
                loaded.encodings.push_back(e.get<std::string>());
              }
            }
          }
        }
      } catch (...) {
        loaded.name = entry.path().stem().string();
      }

      std::cerr << "Loaded MessageParser: " << loaded.name << " from " << path << "\n";
      message_parsers_.push_back(std::move(loaded));
      continue;
    }

    // Neither DataSource nor MessageParser — log both errors for diagnostics.
    std::cerr << "Failed to load plugin: " << path << "\n"
              << "  as DataSource: " << ds_result.error() << "\n"
              << "  as MessageParser: " << mp_result.error() << "\n";
  }
}

void PluginRegistry::reload() {
  data_sources_.clear();
  message_parsers_.clear();
  scanDirectory();
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
