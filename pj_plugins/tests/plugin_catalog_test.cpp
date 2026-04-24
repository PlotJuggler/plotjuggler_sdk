/**
 * @file plugin_catalog_test.cpp
 * @brief Tests for the sidecar-based plugin discovery scanner (Phase 1d).
 *
 * The scanner is pure filesystem + JSON — no dlopen. We write synthetic
 * sidecars into a temp directory and verify the descriptors round-trip.
 */
#include "pj_plugins/host/plugin_catalog.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace PJ {
namespace {

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

  void writeSidecar(const std::string& stem, const std::string& json) {
    std::ofstream out(dir_ / (stem + ".pjmanifest.json"));
    out << json;
  }

  std::filesystem::path dir_;
};

TEST_F(PluginCatalogTest, MissingDirectoryReturnsError) {
  auto result = scanPluginSidecars("/nonexistent/path/xyz");
  EXPECT_FALSE(result.has_value());
}

TEST_F(PluginCatalogTest, EmptyDirectoryReturnsEmptyVector) {
  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_TRUE(result->empty());
}

TEST_F(PluginCatalogTest, ValidSidecarDecodes) {
  writeSidecar("my_plugin", R"({
    "name": "My Plugin",
    "version": "1.2.3",
    "abi_major": 4,
    "family": "data_source",
    "description": "A test plugin",
    "category": "File",
    "file_extensions": [".csv", ".tsv"]
  })");

  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value()) << result.error();
  ASSERT_EQ(result->size(), 1U);
  const auto& d = (*result)[0];

  EXPECT_EQ(d.name, "My Plugin");
  EXPECT_EQ(d.version, "1.2.3");
  EXPECT_EQ(d.abi_major, 4U);
  EXPECT_EQ(d.family, PluginFamily::kDataSource);
  EXPECT_EQ(d.description, "A test plugin");
  EXPECT_EQ(d.category, "File");
  ASSERT_EQ(d.file_extensions.size(), 2U);
  EXPECT_EQ(d.file_extensions[0], ".csv");
  EXPECT_EQ(d.file_extensions[1], ".tsv");
  EXPECT_EQ(d.sidecar_path.filename(), "my_plugin.pjmanifest.json");
}

TEST_F(PluginCatalogTest, MalformedJsonIsSkipped) {
  writeSidecar("broken", "{ this is not valid json");
  writeSidecar("good", R"({"name":"G","version":"1","abi_major":4,"family":"toolbox"})");

  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1U);
  EXPECT_EQ((*result)[0].name, "G");
}

TEST_F(PluginCatalogTest, MissingRequiredKeyIsSkipped) {
  writeSidecar("no_version", R"({"name":"X","abi_major":4,"family":"dialog"})");
  writeSidecar("no_family", R"({"name":"Y","version":"1","abi_major":4})");
  writeSidecar("complete", R"({"name":"Z","version":"1","abi_major":4,"family":"message_parser"})");

  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1U);
  EXPECT_EQ((*result)[0].name, "Z");
  EXPECT_EQ((*result)[0].family, PluginFamily::kMessageParser);
}

TEST_F(PluginCatalogTest, UnknownFamilyIsSkipped) {
  writeSidecar("bogus", R"({"name":"B","version":"1","abi_major":4,"family":"something_else"})");
  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST_F(PluginCatalogTest, NonSidecarFilesAreIgnored) {
  writeSidecar("p1", R"({"name":"P1","version":"1","abi_major":4,"family":"data_source"})");
  // Write a non-sidecar file
  std::ofstream(dir_ / "random.txt") << "hello";
  std::ofstream(dir_ / "libp1.so") << "fake binary";

  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1U);
  EXPECT_EQ((*result)[0].name, "P1");
}

TEST_F(PluginCatalogTest, ResultIsSortedByPath) {
  writeSidecar("zz_plugin", R"({"name":"Z","version":"1","abi_major":4,"family":"toolbox"})");
  writeSidecar("aa_plugin", R"({"name":"A","version":"1","abi_major":4,"family":"toolbox"})");
  writeSidecar("mm_plugin", R"({"name":"M","version":"1","abi_major":4,"family":"toolbox"})");

  auto result = scanPluginSidecars(dir_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3U);
  EXPECT_EQ((*result)[0].name, "A");
  EXPECT_EQ((*result)[1].name, "M");
  EXPECT_EQ((*result)[2].name, "Z");
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

// ---------------------------------------------------------------------------
// Integration test: scan the actual build-tree plugin directory if present.
// Lets us verify that the pj_emit_plugin_manifest CMake helper produces
// sidecars that scanPluginSidecars actually consumes correctly.
// ---------------------------------------------------------------------------

#ifdef PJ_PORTED_PLUGINS_BIN_DIR
TEST(PluginCatalogIntegration, ScansPortedPluginsBinDir) {
  const std::filesystem::path bin_dir = PJ_PORTED_PLUGINS_BIN_DIR;
  if (!std::filesystem::exists(bin_dir)) {
    GTEST_SKIP() << "ported plugins bin dir not present: " << bin_dir;
  }

  auto result = PJ::scanPluginSidecars(bin_dir);
  ASSERT_TRUE(result.has_value()) << result.error();

  // Every entry must parse cleanly and have abi_major == 4.
  EXPECT_FALSE(result->empty()) << "no sidecars found in " << bin_dir;
  for (const auto& d : *result) {
    EXPECT_EQ(d.abi_major, 4U) << "sidecar " << d.sidecar_path << " has abi_major != 4";
    EXPECT_NE(d.family, PJ::PluginFamily::kUnknown);
    EXPECT_FALSE(d.name.empty());
    EXPECT_FALSE(d.version.empty());
  }
}
#endif
