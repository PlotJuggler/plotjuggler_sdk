// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/plugin_catalog.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "detail/library_loader.hpp"
#include "pj_plugins/host/data_source_library.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

std::string manifestWithMinSdk(std::string_view json_value) {
  return std::string(R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0","min_sdk_required":)") +
         std::string(json_value) + "}";
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

TEST_F(PluginCatalogTest, InspectRequiredPrefixOnlyDialogDsoDoesNotReadStaticManifestTail) {
  auto descriptor = inspectPluginDso(PJ_OLD_DIALOG_VTABLE_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "old-dialog-vtable");
  EXPECT_EQ(descriptor->name, "Old Dialog Vtable");
  EXPECT_EQ(descriptor->family, PluginFamily::kDialog);
}

TEST_F(PluginCatalogTest, EntryPointSymbolResolvedFromDependencyIsRejected) {
#if !defined(_WIN32)
  auto is_provenance_error = [](const std::string& error) {
    return error.find("resolved from dependency") != std::string::npos ||
           error.find("cannot prove provenance") != std::string::npos;
  };
  auto dependency_library = DataSourceLibrary::load(PJ_ENTRY_POINT_VIA_DEPENDENCY_PLUGIN_PATH);
  ASSERT_FALSE(dependency_library.has_value());
  EXPECT_TRUE(is_provenance_error(dependency_library.error())) << dependency_library.error();

  auto dependency_descriptor = inspectPluginDso(PJ_ENTRY_POINT_VIA_DEPENDENCY_PLUGIN_PATH);
  ASSERT_FALSE(dependency_descriptor.has_value());
  EXPECT_TRUE(is_provenance_error(dependency_descriptor.error())) << dependency_descriptor.error();
#endif

  auto library = DataSourceLibrary::load(PJ_ENTRY_POINT_WITH_OWN_EXPORTS_PLUGIN_PATH);
  ASSERT_TRUE(library.has_value()) << library.error();

  auto descriptor = inspectPluginDso(PJ_ENTRY_POINT_WITH_OWN_EXPORTS_PLUGIN_PATH);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "entry-point-candidate");
}

TEST_F(PluginCatalogTest, UnicodeExtensionPathLoadsOnWindows) {
  const std::filesystem::path unicode_dir = dir_ / std::filesystem::path(u8"插件-π");
  std::filesystem::create_directories(unicode_dir);
  const std::filesystem::path plugin_path = unicode_dir / pluginFileName("unicode_plugin");
  std::filesystem::copy_file(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH, plugin_path);

  auto descriptor = inspectPluginDso(plugin_path);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-data-source");

  auto library = DataSourceLibrary::load(plugin_path);
  ASSERT_TRUE(library.has_value()) << library.error();
  EXPECT_TRUE(library->valid());
}

TEST_F(PluginCatalogTest, AlreadyOpenHandleSupportsInspectionLoadingAndFamilyQuery) {
  const std::filesystem::path plugin_path = PJ_MOCK_DATA_SOURCE_PLUGIN_PATH;
  auto raw_handle = detail::loadLibraryHandle(plugin_path);
  ASSERT_TRUE(raw_handle.has_value()) << raw_handle.error();
  auto owner = detail::adoptLibraryHandle(*raw_handle);
  auto shared_handle = detail::adoptLibraryHandleNonOwning(owner.get());

  const auto families = detail::exportedPluginFamilies(shared_handle, plugin_path);
  EXPECT_EQ(families, std::vector<PluginFamily>{PluginFamily::kDataSource});

  auto descriptor = inspectPluginDso(shared_handle, plugin_path);
  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->id, "mock-data-source");

  auto library = DataSourceLibrary::loadFromHandle(shared_handle, plugin_path);
  ASSERT_TRUE(library.has_value()) << library.error();
  EXPECT_TRUE(library->valid());
}

TEST_F(PluginCatalogTest, DependencySearchExcludesCwdAndPath) {
  const std::filesystem::path candidate_dir = dir_ / "candidate";
  const std::filesystem::path cwd_decoy_dir = dir_ / "cwd-decoy";
  const std::filesystem::path path_decoy_dir = dir_ / "path-decoy";
  std::filesystem::create_directories(candidate_dir);
  std::filesystem::create_directories(cwd_decoy_dir);
  std::filesystem::create_directories(path_decoy_dir);

  const std::filesystem::path candidate_source = PJ_DEPENDENCY_SEARCH_CANDIDATE_PATH;
  const std::filesystem::path real_source = PJ_DEPENDENCY_SEARCH_REAL_PATH;
  const std::filesystem::path decoy_source = PJ_DEPENDENCY_SEARCH_DECOY_PATH;
  const std::filesystem::path candidate = candidate_dir / candidate_source.filename();
  const std::filesystem::path sibling = candidate_dir / real_source.filename();
  std::filesystem::copy_file(candidate_source, candidate);
  std::filesystem::copy_file(real_source, sibling);
  std::filesystem::copy_file(decoy_source, cwd_decoy_dir / real_source.filename());
  std::filesystem::copy_file(decoy_source, path_decoy_dir / real_source.filename());

  const std::filesystem::path original_cwd = std::filesystem::current_path();
#if defined(_WIN32)
  std::optional<std::wstring> old_path;
  const DWORD old_path_size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (old_path_size > 0) {
    std::wstring value(old_path_size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"PATH", value.data(), old_path_size);
    ASSERT_GT(copied, 0U);
    value.resize(copied);
    old_path = std::move(value);
  }
  ASSERT_NE(SetEnvironmentVariableW(L"PATH", path_decoy_dir.c_str()), 0);
#endif
  std::filesystem::current_path(cwd_decoy_dir);

  auto sibling_result = inspectPluginDso(candidate);
  std::filesystem::remove(sibling);
  auto decoy_only_result = DataSourceLibrary::load(candidate);

  std::filesystem::current_path(original_cwd);
#if defined(_WIN32)
  ASSERT_NE(SetEnvironmentVariableW(L"PATH", old_path.has_value() ? old_path->c_str() : nullptr), 0);
#endif

  ASSERT_TRUE(sibling_result.has_value()) << sibling_result.error();
  EXPECT_EQ(sibling_result->id, "dependency-search-real");
  EXPECT_FALSE(decoy_only_result.has_value());
}

TEST_F(PluginCatalogTest, MissingIdManifestIsRejected) {
  auto descriptor = inspectPluginDso(PJ_MISSING_ID_PLUGIN_PATH);
  ASSERT_FALSE(descriptor.has_value());
  EXPECT_NE(descriptor.error().find("id"), std::string::npos);
}

TEST_F(PluginCatalogTest, MinSdkRequiredDefaultsToEmptyWhenAbsent) {
  auto descriptor = decodeManifest(
      "static:sdk-test", PluginFamily::kDataSource, R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0"})");

  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_TRUE(descriptor->min_sdk_required.empty());
}

TEST_F(PluginCatalogTest, MinSdkRequiredAcceptsEmptyString) {
  auto descriptor = decodeManifest(
      "static:sdk-test", PluginFamily::kDataSource,
      R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0","min_sdk_required":""})");

  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_TRUE(descriptor->min_sdk_required.empty());
}

TEST_F(PluginCatalogTest, MinSdkRequiredAcceptsConcreteSemVer) {
  auto descriptor = decodeManifest(
      "static:sdk-test", PluginFamily::kDataSource,
      R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0","min_sdk_required":"0.21.0"})");

  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->min_sdk_required, "0.21.0");
}

TEST_F(PluginCatalogTest, MinSdkRequiredAcceptsPrerelease) {
  auto descriptor = decodeManifest(
      "static:sdk-test", PluginFamily::kDataSource,
      R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0","min_sdk_required":"0.21.0-rc.1+build.7"})");

  ASSERT_TRUE(descriptor.has_value()) << descriptor.error();
  EXPECT_EQ(descriptor->min_sdk_required, "0.21.0-rc.1+build.7");
}

TEST_F(PluginCatalogTest, MinSdkRequiredRejectsNonString) {
  auto descriptor = decodeManifest(
      "static:sdk-test", PluginFamily::kDataSource,
      R"({"id":"sdk-test","name":"SDK Test","version":"1.0.0","min_sdk_required":21})");

  ASSERT_FALSE(descriptor.has_value());
  EXPECT_NE(descriptor.error().find("min_sdk_required"), std::string::npos);
  EXPECT_NE(descriptor.error().find("must be a string"), std::string::npos);
}

TEST_F(PluginCatalogTest, MinSdkRequiredRejectsMalformedVersions) {
  for (const std::string_view value : {"1.0", "garbage", ">=0.21.0", "^0.21.0", "0.21.x"}) {
    auto descriptor = decodeManifest(
        "static:sdk-test", PluginFamily::kDataSource, manifestWithMinSdk("\"" + std::string(value) + "\""));

    ASSERT_FALSE(descriptor.has_value()) << value;
    EXPECT_NE(descriptor.error().find("min_sdk_required"), std::string::npos) << value;
    EXPECT_NE(descriptor.error().find(value), std::string::npos) << value;
  }
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

}  // namespace
}  // namespace PJ
