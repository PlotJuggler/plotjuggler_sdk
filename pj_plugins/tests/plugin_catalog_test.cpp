// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/plugin_catalog.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "pj_plugins/host/plugin_runtime_catalog.hpp"

namespace PJ {
namespace {

std::string pluginFileName(const std::string& stem) {
#if defined(_WIN32)
  return stem + ".dll";
#elif defined(__APPLE__)
  return stem + ".dylib";
#else
  return stem + ".so";
#endif
}

class PluginCatalogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("pj_catalog_test_" +
            std::to_string(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path copyPlugin(const std::string& source, const std::string& name) {
    const std::filesystem::path dst = dir_ / name;
    std::filesystem::copy_file(source, dst, std::filesystem::copy_options::overwrite_existing);
    return dst;
  }

  std::filesystem::path dir_;
};

TEST_F(PluginCatalogTest, MissingDirectoryReturnsError) {
  auto result = scanPluginDsos("/nonexistent/path/xyz");
  EXPECT_FALSE(result.has_value());
}

TEST_F(PluginCatalogTest, EmptyDirectoryReturnsEmptyResult) {
  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_TRUE(result->plugins.empty());
  EXPECT_TRUE(result->diagnostics.empty());
}

TEST_F(PluginCatalogTest, InspectDataSourceDsoUsesEmbeddedManifest) {
  auto descriptor = inspectPluginDso(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-data-source");
  EXPECT_EQ(descriptor->name, "Mock DataSource");
  EXPECT_EQ(descriptor->version, "1.0.0");
  EXPECT_EQ(descriptor->family, PluginFamily::kDataSource);
}

TEST_F(PluginCatalogTest, InspectMessageParserRequiresEncoding) {
  auto descriptor = inspectPluginDso(PJ_MOCK_JSON_PARSER_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-json-parser");
  EXPECT_EQ(descriptor->family, PluginFamily::kMessageParser);
  EXPECT_EQ(descriptor->encoding, std::vector<std::string>{"json"});
}

TEST_F(PluginCatalogTest, InspectToolboxDsoUsesEmbeddedManifest) {
  auto descriptor = inspectPluginDso(PJ_MOCK_TOOLBOX_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-toolbox");
  EXPECT_EQ(descriptor->family, PluginFamily::kToolbox);
}

TEST_F(PluginCatalogTest, InspectDialogDsoUsesEmbeddedManifest) {
  auto descriptor = inspectPluginDso(PJ_MOCK_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-dialog");
  EXPECT_EQ(descriptor->family, PluginFamily::kDialog);
}

TEST_F(PluginCatalogTest, InspectDialogDsoUsesStaticManifestWithoutCreate) {
  auto descriptor = inspectPluginDso(PJ_STATIC_MANIFEST_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "static-manifest-dialog");
  EXPECT_EQ(descriptor->name, "Static Manifest Dialog");
  EXPECT_EQ(descriptor->family, PluginFamily::kDialog);
}

TEST_F(PluginCatalogTest, InspectDialogDsoFallsBackWhenStaticManifestSlotIsNull) {
  auto descriptor = inspectPluginDso(PJ_LEGACY_MACRO_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "legacy-macro-dialog");
  EXPECT_EQ(descriptor->name, "Legacy Macro Dialog");
  EXPECT_EQ(descriptor->family, PluginFamily::kDialog);
}

TEST_F(PluginCatalogTest, MissingIdManifestIsRejected) {
  auto descriptor = inspectPluginDso(PJ_MISSING_ID_PLUGIN_PATH);
  ASSERT_FALSE(descriptor.has_value());
  EXPECT_NE(descriptor.error().find("id"), std::string::npos);
}

TEST_F(PluginCatalogTest, InvalidOptionalManifestFieldIsReportedAsDiagnostic) {
  copyPlugin(PJ_INVALID_OPTIONAL_PLUGIN_PATH, pluginFileName("invalid_optional"));

  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_TRUE(result->plugins.empty());
  ASSERT_EQ(result->diagnostics.size(), 1U);
  EXPECT_NE(result->diagnostics[0].message.find("description"), std::string::npos);
  EXPECT_NE(result->diagnostics[0].message.find("invalid_optional"), std::string::npos);
}

TEST_F(PluginCatalogTest, MissingRequiredVtableSlotIsReportedAsDiagnostic) {
  copyPlugin(PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH, pluginFileName("missing_required_slots"));

  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_TRUE(result->plugins.empty());
  ASSERT_EQ(result->diagnostics.size(), 1U);
  EXPECT_NE(result->diagnostics[0].message.find("missing required slot"), std::string::npos);
  EXPECT_NE(result->diagnostics[0].message.find("missing_required_slots"), std::string::npos);
}

TEST_F(PluginCatalogTest, MissingRequiredDialogVtableSlotIsReportedAsDiagnostic) {
  copyPlugin(PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH, pluginFileName("missing_dialog_slot"));

  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_TRUE(result->plugins.empty());
  ASSERT_EQ(result->diagnostics.size(), 1U);
  EXPECT_NE(
      result->diagnostics[0].message.find("Dialog vtable missing required slot: get_ui_content"), std::string::npos);
  EXPECT_NE(result->diagnostics[0].message.find("missing_dialog_slot"), std::string::npos);
}

TEST_F(PluginCatalogTest, ScanContinuesAfterBrokenDso) {
  copyPlugin(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, pluginFileName("valid"));
  std::ofstream(dir_ / pluginFileName("broken")) << "not a shared library";
  std::ofstream(dir_ / "notes.txt") << "not a candidate";

  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  ASSERT_EQ(result->plugins.size(), 1U);
  EXPECT_EQ(result->plugins[0].id, "mock-data-source");
  ASSERT_EQ(result->diagnostics.size(), 1U);
  EXPECT_EQ(result->diagnostics[0].path.filename(), pluginFileName("broken"));
}

TEST_F(PluginCatalogTest, ResultIsSortedByPath) {
  copyPlugin(PJ_MOCK_TOOLBOX_PLUGIN_PATH, pluginFileName("zz_plugin"));
  copyPlugin(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, pluginFileName("aa_plugin"));

  auto result = scanPluginDsos(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  ASSERT_EQ(result->plugins.size(), 2U);
  EXPECT_EQ(result->plugins[0].dso_path.filename(), pluginFileName("aa_plugin"));
  EXPECT_EQ(result->plugins[1].dso_path.filename(), pluginFileName("zz_plugin"));
}

TEST_F(PluginCatalogTest, FamilyToStringRoundTrip) {
  EXPECT_EQ(toString(PluginFamily::kDataSource), "data_source");
  EXPECT_EQ(toString(PluginFamily::kMessageParser), "message_parser");
  EXPECT_EQ(toString(PluginFamily::kToolbox), "toolbox");
  EXPECT_EQ(toString(PluginFamily::kDialog), "dialog");
  EXPECT_EQ(toString(PluginFamily::kUnknown), "unknown");
}

TEST_F(PluginCatalogTest, RuntimeCatalogDedupsDuplicateIdFirstFolderWins) {
  // The same data-source DSO (manifest id "mock-data-source") placed in two
  // folders: setPluginDirs scans them in order and must load it exactly once,
  // from the first (higher-priority) folder.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_b / pluginFileName("ds"));

  PluginRuntimeCatalog catalog;
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].id, "mock-data-source");
  // The winner must come from the first folder. Compare at the filesystem level
  // (std::filesystem::equivalent) so it holds regardless of how the stored path
  // is spelled — Windows back/forward slashes, drive-letter case, canonicalisation.
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_a))
      << "winner " << winner << " is not in the first folder " << dir_a;
}

TEST_F(PluginCatalogTest, RuntimeCatalogSkipsDuplicateScanFolders) {
  // The same folder listed twice in setPluginDirs is scanned once: the second pass is
  // skipped, so no plugin is ever compared against itself — which would otherwise emit a
  // confusing "ignoring duplicate id … already loaded from <same path>" diagnostic.
  const std::filesystem::path dir_a = dir_ / "a";
  std::filesystem::create_directories(dir_a);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));

  std::vector<std::string> messages;
  PluginRuntimeCatalog catalog({}, [&](const Diagnostic& d) { messages.push_back(d.message); });
  catalog.setPluginDirs({dir_a, dir_a});  // same folder twice
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  for (const std::string& message : messages) {
    EXPECT_EQ(message.find("ignoring duplicate"), std::string::npos)
        << "a folder listed twice must not trigger a self-dedup diagnostic: " << message;
  }
}

TEST_F(PluginCatalogTest, RuntimeCatalogPrefersHigherVersionOverFolderPriority) {
  // Same plugin id in two folders, but the LOWER-priority folder ships a newer
  // version (v2.0.0) than the higher-priority one (v1.0.0). Version wins over
  // folder priority, so the v2 DSO from the second folder is the one loaded.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));     // v1.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));  // v2.0.0

  PluginRuntimeCatalog catalog;
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].id, "mock-data-source");
  EXPECT_EQ(catalog.dataSources()[0].version, "2.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_b))
      << "winner " << winner << " should be the newer v2 in " << dir_b;
}

TEST_F(PluginCatalogTest, RuntimeCatalogKeepsHigherPriorityWhenNewerVersionIsHigherPriority) {
  // Mirror of the above: the newer version now sits in the HIGHER-priority folder.
  // The winner is still v2.0.0, confirming the tie-break never demotes it.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_a / pluginFileName("ds"));  // v2.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_b / pluginFileName("ds"));     // v1.0.0

  PluginRuntimeCatalog catalog;
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "2.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_a));
}

TEST_F(PluginCatalogTest, RuntimeCatalogPrefersCompatibleOverHigherIncompatibleVersion) {
  // Higher-priority folder ships an incompatible v3.0.0 (needs PlotJuggler 5.0.0);
  // lower-priority folder ships a compatible v2.0.0. With a 4.0.0 host, the
  // compatible build wins even though it is both lower version and lower priority.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(
      PJ_MOCK_DATA_SOURCE_INCOMPATIBLE_PLUGIN_PATH, dir_a / pluginFileName("ds"));               // v3, min 5.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));  // v2, no min

  PluginRuntimeCatalog catalog;
  catalog.setHostVersion("4.0.0");
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "2.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_b))
      << "winner " << winner << " should be the compatible v2 in " << dir_b;
}

TEST_F(PluginCatalogTest, RuntimeCatalogLoadsLoneIncompatiblePlugin) {
  // A single incompatible plugin (needs PlotJuggler 5.0.0) still loads on a 4.0.0
  // host: min_plotjuggler_version only breaks ties, it never excludes.
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_INCOMPATIBLE_PLUGIN_PATH, dir_ / pluginFileName("ds"));

  PluginRuntimeCatalog catalog;
  catalog.setHostVersion("4.0.0");
  catalog.setPluginDir(dir_);
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "3.0.0");
}

TEST_F(PluginCatalogTest, RuntimeCatalogPrefersTheOnlyCompatibleAmongMixedCandidates) {
  // Three folders, same id, mixed compatibility: incompatible v3.0.0 (min 5.0.0) in
  // the highest-priority folder, compatible v2.0.0 in the middle, incompatible v1.5.0
  // (min 5.0.0) in the lowest. With a 4.0.0 host the compatible v2.0.0 wins — it is
  // neither the highest version nor the highest-priority folder.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  const std::filesystem::path dir_c = dir_ / "c";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::create_directories(dir_c);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_INCOMPATIBLE_PLUGIN_PATH, dir_a / pluginFileName("ds"));  // v3, min 5
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));            // v2, compat
  std::filesystem::copy_file(
      PJ_MOCK_DATA_SOURCE_INCOMPATIBLE_LOW_PLUGIN_PATH, dir_c / pluginFileName("ds"));  // v1.5, min 5

  PluginRuntimeCatalog catalog;
  catalog.setHostVersion("4.0.0");
  catalog.setPluginDirs({dir_a, dir_b, dir_c});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "2.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_b))
      << "winner " << winner << " should be the only compatible candidate in " << dir_b;
}

TEST_F(PluginCatalogTest, RuntimeCatalogAuthoritativeFolderOverridesHigherVersion) {
  // An authoritative (user-explicit) folder is a hard override: its v1.0.0 wins over
  // a higher v2.0.0 in a lower-priority managed folder. Without the authoritative
  // mark, version would pick v2.0.0 — this isolates the override.
  const std::filesystem::path dir_a = dir_ / "a";  // authoritative
  const std::filesystem::path dir_b = dir_ / "b";  // managed
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));     // v1.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));  // v2.0.0

  PluginRuntimeCatalog catalog;
  catalog.setAuthoritativeFolders({dir_a});
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "1.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_a));
}

TEST_F(PluginCatalogTest, RuntimeCatalogAuthoritativeFolderWinsEvenWhenIncompatible) {
  // The authoritative copy wins even if it is incompatible (min 5.0.0 > host 4.0.0)
  // and the managed alternative is compatible: an explicit choice overrides compat.
  const std::filesystem::path dir_a = dir_ / "a";  // authoritative, incompatible v3.0.0
  const std::filesystem::path dir_b = dir_ / "b";  // managed, compatible v2.0.0
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_INCOMPATIBLE_PLUGIN_PATH, dir_a / pluginFileName("ds"));  // v3, min 5
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));            // v2, compat

  PluginRuntimeCatalog catalog;
  catalog.setHostVersion("4.0.0");
  catalog.setAuthoritativeFolders({dir_a});
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "3.0.0");
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_a));
}

TEST_F(PluginCatalogTest, RuntimeCatalogAuthoritativeFolderSurvivesSymlinkMaterializedAfterRegistration) {
  // Regression for the non-idempotent-canonicalization trap: an authoritative folder is
  // registered before it exists on disk, then materializes as a symlink. weakly_canonical
  // returns the path unresolved at registration but resolves the symlink at scan time, so a
  // string compare of the two canonicalizations diverges — the folder silently degrades to
  // managed and version picks the higher v2.0.0. Matching by filesystem identity instead keeps
  // the authoritative v1.0.0 override intact. This is the shared root cause of all three
  // divergence scenarios (symlink timing, relative-path + CWD change, case-insensitive
  // filesystems); the last is not reproducible on a case-sensitive CI filesystem.
  const std::filesystem::path real_dir = dir_ / "real";  // symlink target, authoritative v1.0.0
  const std::filesystem::path link_dir = dir_ / "link";  // authoritative spelling (a symlink)
  const std::filesystem::path dir_b = dir_ / "b";        // managed, v2.0.0

  PluginRuntimeCatalog catalog;
  // Register the authoritative folder while it does NOT yet exist on disk.
  catalog.setAuthoritativeFolders({link_dir});

  std::filesystem::create_directories(real_dir);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, real_dir / pluginFileName("ds"));  // v1.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));  // v2.0.0
  std::error_code ec;
  std::filesystem::create_directory_symlink(real_dir, link_dir, ec);
  if (ec) {
    GTEST_SKIP() << "filesystem does not support directory symlinks: " << ec.message();
  }

  catalog.setPluginDirs({link_dir, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "1.0.0")
      << "authoritative override must survive a symlink that only resolves at scan time";
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), real_dir));
}

TEST_F(PluginCatalogTest, RuntimeCatalogAuthoritativeFolderSupersedesEarlierManagedIncumbent) {
  // The authoritative folder is scanned AFTER a managed one (it sits lower in the scan
  // list), so a managed incumbent is already recorded when the authoritative copy arrives.
  // The authoritative candidate must still supersede it — the override is not order
  // dependent. Complements the existing tests, which place the authoritative folder first.
  const std::filesystem::path dir_managed = dir_ / "managed";  // higher priority, managed, v2.0.0
  const std::filesystem::path dir_auth = dir_ / "auth";        // lower priority, authoritative, v1.0.0
  std::filesystem::create_directories(dir_managed);
  std::filesystem::create_directories(dir_auth);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_managed / pluginFileName("ds"));  // v2.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_auth / pluginFileName("ds"));        // v1.0.0

  PluginRuntimeCatalog catalog;
  catalog.setAuthoritativeFolders({dir_auth});
  catalog.setPluginDirs({dir_managed, dir_auth});  // managed scanned first, authoritative second
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "1.0.0")
      << "an authoritative folder must override an already-recorded managed incumbent";
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_auth));
}

TEST_F(PluginCatalogTest, RuntimeCatalogAuthoritativeFolderOutsideScanListHasNoEffect) {
  // An authoritative folder that is NOT among the scan dirs has no effect (docstring
  // contract): among the two managed scan folders, version decides normally, so the
  // lower-priority v2.0.0 beats the higher-priority v1.0.0.
  const std::filesystem::path dir_a = dir_ / "a";          // managed, higher priority, v1.0.0
  const std::filesystem::path dir_b = dir_ / "b";          // managed, lower priority, v2.0.0
  const std::filesystem::path dir_other = dir_ / "other";  // authoritative, but never scanned
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::create_directories(dir_other);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));     // v1.0.0
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH, dir_b / pluginFileName("ds"));  // v2.0.0

  PluginRuntimeCatalog catalog;
  catalog.setAuthoritativeFolders({dir_other});  // not in the scan list -> must be inert
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  ASSERT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.dataSources()[0].version, "2.0.0")
      << "an authoritative folder outside the scan list must not change the managed outcome";
  const std::filesystem::path winner(catalog.dataSources()[0].path);
  EXPECT_TRUE(std::filesystem::equivalent(winner.parent_path(), dir_b));
}

TEST_F(PluginCatalogTest, RuntimeCatalogLoadsDistinctIdsFromMultipleFolders) {
  // Different plugins in different folders all load.
  const std::filesystem::path dir_a = dir_ / "a";
  const std::filesystem::path dir_b = dir_ / "b";
  std::filesystem::create_directories(dir_a);
  std::filesystem::create_directories(dir_b);
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, dir_a / pluginFileName("ds"));
  std::filesystem::copy_file(PJ_MOCK_TOOLBOX_PLUGIN_PATH, dir_b / pluginFileName("tb"));

  PluginRuntimeCatalog catalog;
  catalog.setPluginDirs({dir_a, dir_b});
  catalog.scanDirectory();

  EXPECT_EQ(catalog.dataSources().size(), 1U);
  EXPECT_EQ(catalog.toolboxes().size(), 1U);
}

// ─── compareSemver (version ordering used by the runtime catalog dedup) ──────────

TEST(CompareSemver, OrdersByNumericComponents) {
  EXPECT_LT(detail::compareSemver("4.0.2", "4.1.0"), 0);
  EXPECT_GT(detail::compareSemver("5.0.0", "4.9.9"), 0);
  EXPECT_EQ(detail::compareSemver("4.1.0", "4.1.0"), 0);
}

TEST(CompareSemver, TreatsMissingTrailingComponentsAsZero) {
  EXPECT_EQ(detail::compareSemver("4.1", "4.1.0"), 0);
  EXPECT_EQ(detail::compareSemver("4", "4.0.0"), 0);
  EXPECT_LT(detail::compareSemver("4.0", "4.0.1"), 0);
}

TEST(CompareSemver, IgnoresLeadingZerosAndSuffixes) {
  EXPECT_EQ(detail::compareSemver("4.01.0", "4.1.0"), 0);
  EXPECT_EQ(detail::compareSemver("1.0.0-rc2", "1.0.0"), 0);  // pre-release suffix ignored
  EXPECT_LT(detail::compareSemver("1.0.0", "2.0.0-beta"), 0);
}

TEST(CompareSemver, IgnoresDottedPreReleaseSuffix) {
  // The suffix is ignored in full even when it contains dots: the compare must not walk
  // past the '-' and read the suffix's own numeric parts (regression for a version that
  // stepped to the next '.' instead of stopping at the pre-release separator).
  EXPECT_EQ(detail::compareSemver("1.0.0-rc.2", "1.0.0-rc.3"), 0);
  EXPECT_EQ(detail::compareSemver("1.0-rc.2", "1.0.0"), 0);
  EXPECT_LT(detail::compareSemver("1.0.0-rc.9", "1.0.1"), 0);
}

TEST(CompareSemver, DoesNotOverflowOnHugeComponents) {
  EXPECT_GT(detail::compareSemver("999999999999999999999.0.0", "4.0.0"), 0);
  EXPECT_LT(detail::compareSemver("4.0.0", "1000000000000000000000.0.0"), 0);
  EXPECT_EQ(detail::compareSemver("100000000000000000000.0", "100000000000000000000.0"), 0);
}

}  // namespace
}  // namespace PJ
