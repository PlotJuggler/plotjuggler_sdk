#include "plugin_registry.hpp"

#include <QFileInfo>

#include <utility>

namespace proto {

PluginRegistry::PluginRegistry(std::string_view plugin_dir, PJ::DiagnosticSink sink)
    : catalog_(std::filesystem::path(std::string(plugin_dir)), std::move(sink), "PluginRegistry") {}

void PluginRegistry::scanDirectory() {
  catalog_.scanDirectory();
}

void PluginRegistry::reload() {
  (void)catalog_.reload();
}

std::vector<LoadedDataSource*> PluginRegistry::fileImportSources() {
  return catalog_.fileImportSources();
}

std::vector<LoadedDataSource*> PluginRegistry::streamSources() {
  return catalog_.streamSources();
}

std::string PluginRegistry::buildFileFilter() const {
  return catalog_.buildFileFilter();
}

std::vector<LoadedDataSource*> PluginRegistry::findSourcesForExtension(std::string_view ext) {
  return catalog_.findSourcesForExtension(ext);
}

LoadedMessageParser* PluginRegistry::findParserByEncoding(std::string_view encoding) {
  return catalog_.findParserByEncoding(encoding);
}

std::string PluginRegistry::listAvailableEncodings() const {
  return catalog_.listAvailableEncodings();
}

QMap<QString, PJ::InstalledExtension> PluginRegistry::loadedExtensionsSnapshot() const {
  QMap<QString, PJ::InstalledExtension> snapshot;

  auto add_loaded = [&](const std::string& id, const std::string& version, const std::string& path) {
    if (id.empty()) {
      return;
    }
    const QString qid = QString::fromStdString(id);
    if (snapshot.contains(qid)) {
      return;
    }

    PJ::InstalledExtension record;
    record.id = qid;
    record.version = QString::fromStdString(version);
    record.path = QString::fromStdString(path);
    record.install_date = QFileInfo(record.path).lastModified();
    record.enabled = true;
    snapshot.insert(qid, record);
  };

  for (const auto& source : catalog_.dataSources()) {
    add_loaded(source.id, source.version, source.path);
  }
  for (const auto& parser : catalog_.messageParsers()) {
    add_loaded(parser.id, parser.version, parser.path);
  }
  for (const auto& toolbox : catalog_.toolboxes()) {
    add_loaded(toolbox.id, toolbox.version, toolbox.path);
  }

  return snapshot;
}

}  // namespace proto
