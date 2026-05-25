// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/plugin_catalog.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

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

}  // namespace
}  // namespace PJ
