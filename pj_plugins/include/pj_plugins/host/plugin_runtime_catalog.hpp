#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_library.hpp"
#include "pj_plugins/host/plugin_catalog.hpp"
#include "pj_plugins/host/toolbox_library.hpp"

namespace PJ {

// Loaded DataSource plugin plus metadata used by host UIs and sessions.
struct RuntimeDataSourcePlugin {
  DataSourceLibrary library;
  std::string path;
  std::string name;
  std::string id;
  std::string version;
  std::vector<std::string> file_extensions;
  uint64_t capabilities = 0;
  std::filesystem::file_time_type loaded_mtime;
};

// Loaded MessageParser plugin plus lookup metadata.
struct RuntimeMessageParserPlugin {
  MessageParserLibrary library;
  std::string path;
  std::string name;
  std::string id;
  std::string version;
  std::vector<std::string> encodings;
  std::filesystem::file_time_type loaded_mtime;
};

// Loaded Toolbox plugin plus metadata used by Tools menus.
struct RuntimeToolboxPlugin {
  ToolboxLibrary library;
  std::string path;
  std::string name;
  std::string id;
  std::string version;
  uint64_t capabilities = 0;
  std::filesystem::file_time_type loaded_mtime;
};

// Shared host-side runtime catalog for discovering and loading plugin DSOs.
class PluginRuntimeCatalog {
 public:
  // Creates a catalog rooted at plugin_dir and reporting through sink.
  explicit PluginRuntimeCatalog(
      std::filesystem::path plugin_dir = {}, DiagnosticSink sink = {},
      std::string diagnostic_source = "PluginRuntimeCatalog");

  // Replaces the directory scanned by scanDirectory() and reload().
  void setPluginDir(std::filesystem::path plugin_dir);

  // Replaces the optional diagnostic sink.
  void setDiagnosticSink(DiagnosticSink sink);

  // Clears current state and loads every valid plugin under plugin_dir.
  void scanDirectory();

  // Reconciles loaded state with disk and returns true if it changed.
  [[nodiscard]] bool reload();

  // Returns loaded DataSource plugins.
  [[nodiscard]] const std::vector<RuntimeDataSourcePlugin>& dataSources() const {
    return data_sources_;
  }

  // Returns loaded MessageParser plugins.
  [[nodiscard]] const std::vector<RuntimeMessageParserPlugin>& messageParsers() const {
    return message_parsers_;
  }

  // Returns loaded Toolbox plugins.
  [[nodiscard]] const std::vector<RuntimeToolboxPlugin>& toolboxes() const {
    return toolbox_plugins_;
  }

  // Returns file-import capable DataSource plugins.
  [[nodiscard]] std::vector<RuntimeDataSourcePlugin*> fileImportSources();

  // Returns file-import capable DataSource plugins.
  [[nodiscard]] std::vector<const RuntimeDataSourcePlugin*> fileImportSources() const;

  // Returns streaming-capable DataSource plugins.
  [[nodiscard]] std::vector<RuntimeDataSourcePlugin*> streamSources();

  // Returns streaming-capable DataSource plugins.
  [[nodiscard]] std::vector<const RuntimeDataSourcePlugin*> streamSources() const;

  // Finds file-import DataSources that handle ext.
  [[nodiscard]] std::vector<RuntimeDataSourcePlugin*> findSourcesForExtension(std::string_view ext);

  // Finds file-import DataSources that handle ext.
  [[nodiscard]] std::vector<const RuntimeDataSourcePlugin*> findSourcesForExtension(std::string_view ext) const;

  // Finds a MessageParser by encoding name.
  [[nodiscard]] RuntimeMessageParserPlugin* findParserByEncoding(std::string_view encoding);

  // Finds a MessageParser by encoding name.
  [[nodiscard]] const RuntimeMessageParserPlugin* findParserByEncoding(std::string_view encoding) const;

  // Builds a QFileDialog-compatible filter string.
  [[nodiscard]] std::string buildFileFilter() const;

  // Lists parser encodings as a JSON string array.
  [[nodiscard]] std::string listAvailableEncodings() const;

 private:
  // Loads a descriptor using the family-specific loader.
  bool loadAndRegister(const PluginDescriptor& descriptor);

  // Loads and records one DataSource plugin.
  bool loadAndRegisterDataSource(const PluginDescriptor& descriptor);

  // Loads and records one MessageParser plugin.
  bool loadAndRegisterMessageParser(const PluginDescriptor& descriptor);

  // Loads and records one Toolbox plugin.
  bool loadAndRegisterToolbox(const PluginDescriptor& descriptor);

  // Removes any loaded plugin whose path matches path.
  bool evictByPath(const std::string& path);

  // Returns the loaded mtime for path, or a default value.
  [[nodiscard]] std::filesystem::file_time_type loadedMtimeForPath(const std::string& path) const;

  // Emits one diagnostic through the optional sink.
  void report(DiagnosticLevel level, const std::string& id, std::string message) const;

  // Emits diagnostics produced by DSO discovery.
  void reportScanDiagnostics(const PluginScanResult& scan) const;

  std::filesystem::path plugin_dir_;
  DiagnosticSink sink_;
  std::string diagnostic_source_;
  std::vector<RuntimeDataSourcePlugin> data_sources_;
  std::vector<RuntimeMessageParserPlugin> message_parsers_;
  std::vector<RuntimeToolboxPlugin> toolbox_plugins_;
};

}  // namespace PJ
