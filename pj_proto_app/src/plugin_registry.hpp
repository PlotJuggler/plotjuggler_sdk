#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_library.hpp"

namespace proto {

struct LoadedDataSource {
  PJ::DataSourceLibrary library;
  std::string name;
  std::vector<std::string> file_extensions;
  uint64_t capabilities = 0;
};

struct LoadedMessageParser {
  PJ::MessageParserLibrary library;
  std::string name;
  std::vector<std::string> encodings;
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

 private:
  std::string plugin_dir_;
  std::vector<LoadedDataSource> data_sources_;
  std::vector<LoadedMessageParser> message_parsers_;
};

}  // namespace proto
