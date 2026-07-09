#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file plugin_runtime_catalog.hpp
 * @brief Shared host-side catalog that discovers, loads, and indexes plugin
 *        DSOs across all four families.
 *
 * Wraps scanPluginDsos() discovery plus the per-family loaders; exposes the
 * loaded DataSource/MessageParser/Toolbox sets and lookup helpers (by file
 * extension, by parser encoding). reload() reconciles loaded state with disk.
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_library.hpp"
#include "pj_plugins/host/plugin_catalog.hpp"
#include "pj_plugins/host/toolbox_library.hpp"

namespace PJ {

namespace detail {
// Compares two dotted numeric version strings ("4.1.0" vs "4.0.2"). Overflow-safe:
// components are compared as decimals, never converted to an integer. Only leading
// numeric components count; pre-release/build suffixes are ignored; a missing
// component is 0. Exposed here so the version ordering can be unit-tested directly.
// Returns <0, 0, or >0 like strcmp.
int compareSemver(std::string_view lhs, std::string_view rhs);
}  // namespace detail

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

// Host-side catalog of loaded plugin DSOs for one plugin directory. Owns the
// loaded libraries; the Runtime*Plugin structs above are non-owning views into
// the loaded set plus the metadata host UIs/sessions need. Not thread-safe.
class PluginRuntimeCatalog {
 public:
  // Creates a catalog rooted at plugin_dir and reporting through sink.
  explicit PluginRuntimeCatalog(
      std::filesystem::path plugin_dir = {}, DiagnosticSink sink = {},
      std::string diagnostic_source = "PluginRuntimeCatalog");

  // Replaces the directory scanned by scanDirectory() and reload().
  void setPluginDir(std::filesystem::path plugin_dir);

  // Replaces the ordered list of directories scanned by scanDirectory() and
  // reload(). Directories are scanned in descending priority order and
  // de-duplicated by manifest id: when the same plugin id appears in more than
  // one directory, the winner is chosen by compatibility first (see
  // setHostVersion), then by higher version, then by directory priority; the
  // losers are skipped with an info diagnostic. Empty entries are ignored.
  void setPluginDirs(std::vector<std::filesystem::path> plugin_dirs);

  // Replaces the optional diagnostic sink.
  void setDiagnosticSink(DiagnosticSink sink);

  // Sets the host ("PlotJuggler") version used to gauge plugin compatibility
  // against each plugin's manifest `min_plotjuggler_version`. Used only to break
  // ties between duplicate ids: a compatible build is preferred over an
  // incompatible one (min > host) regardless of version. It never excludes a
  // plugin — a lone incompatible plugin still loads (with no warning), and if
  // every candidate for an id is incompatible the highest version still wins.
  // Empty (the default) disables the check entirely: every plugin is treated as
  // compatible, so incompatible builds load silently — set a host version to make
  // the compatibility tie-break take effect.
  //
  // Call before the first scanDirectory()/reload(): the value is read on the
  // scan thread and this class is not thread-safe, so it must not change
  // concurrently with a scan.
  void setHostVersion(std::string host_version);

  // Marks a subset of the scan folders (see setPluginDirs) as *authoritative* — a
  // hard override for duplicate-id resolution. When a plugin id is present in an
  // authoritative folder, the highest-priority authoritative copy wins outright,
  // ignoring version and compatibility of any lower-priority folder. The host uses
  // this for user-explicit tiers (custom folders + --plugin-dir); marketplace and
  // bundled folders are left non-authoritative so version/compatibility decide
  // among them. Folders not also in setPluginDirs have no effect.
  //
  // Relative paths are resolved to absolute against the current working directory
  // *at this call*, so a later CWD change cannot silently repoint an entry. A
  // scan folder is matched to an authoritative one by filesystem identity
  // (std::filesystem::equivalent) when both exist, so symlinks, '.'/'..', and
  // case-insensitive filesystems all match correctly; a normalized string compare
  // is the fallback for an authoritative folder not present on disk. Call before
  // the first scanDirectory()/reload().
  void setAuthoritativeFolders(std::vector<std::filesystem::path> folders);

  // Clears current state and loads every valid plugin under plugin_dir.
  void scanDirectory();

  // Reconciles loaded state with disk and returns true if it changed.
  [[nodiscard]] bool reload();

  // --- Static plugin registration (WASM/static builds; no dlopen) -------------
  // Register a statically-linked plugin by its vtable. The vtable must outlive
  // this catalog (static storage duration); the manifest is read from
  // vtable->manifest_json. Returns false (and reports a diagnostic) on failure.
  bool registerStaticDataSource(const PJ_data_source_vtable_t* vtable);
  bool registerStaticMessageParser(const PJ_message_parser_vtable_t* vtable);
  bool registerStaticToolbox(const PJ_toolbox_vtable_t* vtable);

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
  // Scans every directory in plugin_dirs_ (in priority order) and returns the
  // loadable descriptors de-duplicated by manifest id, choosing each id's winner
  // by compatibility, then version, then directory priority (see setPluginDirs /
  // setHostVersion). Reports scan diagnostics and one info diagnostic per skipped
  // duplicate.
  [[nodiscard]] std::vector<PluginDescriptor> collectDeduplicatedPlugins() const;

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

  // True if dir refers to one of the authoritative folders (see
  // setAuthoritativeFolders). Matches by filesystem identity when both paths
  // exist, falling back to a normalized string compare otherwise — never by a
  // single canonicalized-string equality, which is not stable across the
  // set/scan boundary (symlinks, CWD, case-insensitive filesystems).
  [[nodiscard]] bool isAuthoritativeDir(const std::filesystem::path& dir) const;

  std::vector<std::filesystem::path> plugin_dirs_;
  DiagnosticSink sink_;
  std::string diagnostic_source_;
  std::string host_version_;  ///< host version for compatibility ties; "" disables the check
  std::vector<std::filesystem::path> authoritative_folders_;  ///< absolute paths of hard-override folders
  std::vector<RuntimeDataSourcePlugin> data_sources_;
  std::vector<RuntimeMessageParserPlugin> message_parsers_;
  std::vector<RuntimeToolboxPlugin> toolbox_plugins_;
};

}  // namespace PJ
