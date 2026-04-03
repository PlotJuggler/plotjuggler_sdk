#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_library.hpp"

namespace proto {

struct LoadedDataSource {
  PJ::DataSourceLibrary library;
  std::string path;
  std::string name;
  std::vector<std::string> file_extensions;
  uint64_t capabilities = 0;
  std::filesystem::file_time_type loaded_mtime;
};

struct LoadedMessageParser {
  PJ::MessageParserLibrary library;
  std::string path;
  std::string name;
  std::vector<std::string> encodings;
  std::filesystem::file_time_type loaded_mtime;
};

class PluginRegistry {
 public:
  explicit PluginRegistry(std::string_view plugin_dir);

  void scanDirectory();
  void reload();

  [[nodiscard]] std::vector<LoadedDataSource*> fileImportSources();
  [[nodiscard]] std::vector<LoadedDataSource*> streamSources();
  [[nodiscard]] std::string buildFileFilter() const;
  [[nodiscard]] std::vector<LoadedDataSource*> findSourcesForExtension(std::string_view ext);

  /// Find a parser library by encoding name (e.g. "cdr", "protobuf", "json").
  [[nodiscard]] LoadedMessageParser* findParserByEncoding(std::string_view encoding);

  /// Get all loaded message parsers.
  [[nodiscard]] const std::vector<LoadedMessageParser>& allMessageParsers() const { return message_parsers_; }

  /// List all unique encodings from loaded parsers as a JSON array string.
  /// Returns e.g. ["json","cbor","protobuf"]. Returns "[]" if no parsers loaded.
  [[nodiscard]] std::string listAvailableEncodings() const;

 private:
  /// Try to load a DataSource plugin and register it. Returns true on success.
  bool loadAndRegisterDataSource(const std::filesystem::path& so_path);

  /// Try to load a MessageParser plugin and register it. Returns true on success.
  bool loadAndRegisterMessageParser(const std::filesystem::path& so_path);

  std::string plugin_dir_;
  std::vector<LoadedDataSource> data_sources_;
  std::vector<LoadedMessageParser> message_parsers_;
};

}  // namespace proto
