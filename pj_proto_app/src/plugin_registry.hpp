#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <QMap>
#include <QString>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_marketplace/installed_extension.hpp"
#include "pj_plugins/host/plugin_runtime_catalog.hpp"

namespace proto {

using LoadedDataSource = PJ::RuntimeDataSourcePlugin;
using LoadedMessageParser = PJ::RuntimeMessageParserPlugin;
using LoadedToolbox = PJ::RuntimeToolboxPlugin;

// Proto-app compatibility wrapper over PJ::PluginRuntimeCatalog.
class PluginRegistry {
 public:
  // Creates a registry rooted at plugin_dir with optional diagnostics.
  explicit PluginRegistry(std::string_view plugin_dir, PJ::DiagnosticSink sink = {});

  // Clears current state and loads every valid plugin.
  void scanDirectory();

  // Reconciles loaded state with current files on disk.
  void reload();

  // Returns file-import capable DataSource plugins.
  [[nodiscard]] std::vector<LoadedDataSource*> fileImportSources();

  // Returns streaming-capable DataSource plugins.
  [[nodiscard]] std::vector<LoadedDataSource*> streamSources();

  // Builds a QFileDialog-compatible filter string.
  [[nodiscard]] std::string buildFileFilter() const;

  // Finds file-import DataSources that handle ext.
  [[nodiscard]] std::vector<LoadedDataSource*> findSourcesForExtension(std::string_view ext);

  // Finds a parser library by encoding name.
  [[nodiscard]] LoadedMessageParser* findParserByEncoding(std::string_view encoding);

  // Returns all loaded message parsers.
  [[nodiscard]] const std::vector<LoadedMessageParser>& allMessageParsers() const { return catalog_.messageParsers(); }

  // Lists parser encodings as a JSON string array.
  [[nodiscard]] std::string listAvailableEncodings() const;

  // Returns all loaded toolbox plugins.
  [[nodiscard]] const std::vector<LoadedToolbox>& allToolboxes() const { return catalog_.toolboxes(); }

  // Builds a marketplace-style installed snapshot from loaded manifests.
  [[nodiscard]] QMap<QString, PJ::InstalledExtension> loadedExtensionsSnapshot() const;

 private:
  PJ::PluginRuntimeCatalog catalog_;
};

}  // namespace proto
