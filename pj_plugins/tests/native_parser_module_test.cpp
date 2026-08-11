// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/native_parser_module.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "native_parser_module_fixture.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/parser_claim_catalog.hpp"

namespace PJ {
namespace {

class NativeModuleTemporaryDirectory {
 public:
  NativeModuleTemporaryDirectory()
      : path_(
            std::filesystem::temp_directory_path() /
            ("pj_native_module_" +
             std::to_string(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())))) {
    std::filesystem::create_directories(path_);
  }

  ~NativeModuleTemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

class NativeModuleCurrentPathGuard {
 public:
  NativeModuleCurrentPathGuard() : original_(std::filesystem::current_path()) {}

  ~NativeModuleCurrentPathGuard() {
    std::error_code error;
    std::filesystem::current_path(original_, error);
  }

 private:
  std::filesystem::path original_;
};

TEST(NativeParserModule, LoadsCompleteAbiAndCopiesManifestForCatalogAdmission) {
  std::vector<Diagnostic> diagnostics;
  auto module = NativeParserModule::load(
      PJ_NATIVE_MODULE_FIXTURE_PATH, [&](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });

  ASSERT_TRUE(module.has_value()) << module.error();
  EXPECT_TRUE(module->valid());
  EXPECT_EQ(module->path(), PJ_NATIVE_MODULE_FIXTURE_PATH);
  EXPECT_NE(module->manifestJson().find("org.plotjuggler.test.native-module"), std::string_view::npos);
  EXPECT_TRUE(diagnostics.empty());

  ParserClaimCatalog catalog;
  auto manifest = catalog.ingestModuleManifest(module->manifestJson(), ParserClaimProvenance::kFolderDrop, 7);
  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  EXPECT_EQ(manifest->id, "org.plotjuggler.test.native-module");
  EXPECT_EQ(manifest->claims.size(), pj_fixture::kClaimCount);
  EXPECT_EQ(catalog.claims().size(), pj_fixture::kClaimCount);
}

TEST(NativeParserModule, NativeParserNarrowPathIsUtf8) {
  NativeModuleTemporaryDirectory temporary;
  const std::filesystem::path unicode_directory = temporary.path() / std::filesystem::path(u8"módulo-解析");
  std::filesystem::create_directories(unicode_directory);
  const std::filesystem::path module_path =
      unicode_directory / std::filesystem::path(PJ_NATIVE_MODULE_FIXTURE_PATH).filename();
  std::filesystem::copy_file(PJ_NATIVE_MODULE_FIXTURE_PATH, module_path);

  NativeModuleCurrentPathGuard current_path;
  std::filesystem::current_path(temporary.path());
  const auto utf8_path = module_path.lexically_relative(temporary.path()).u8string();
  const std::string narrow_path(utf8_path.begin(), utf8_path.end());
  auto module = NativeParserModule::load(narrow_path);
  ASSERT_TRUE(module.has_value()) << module.error();
  EXPECT_EQ(module->path(), narrow_path);
  EXPECT_NE(module->manifestJson().find("org.plotjuggler.test.native-module"), std::string_view::npos);

#if defined(_WIN32)
  const std::string invalid_utf8 = "invalid-\xff.dll";
  auto invalid = NativeParserModule::load(invalid_utf8);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_NE(invalid.error().find("valid UTF-8"), std::string::npos) << invalid.error();
#endif
}

TEST(NativeParserModule, RejectsEachLoaderFailureWithOneDiagnostic) {
  for (const std::string path : {
           PJ_NATIVE_MODULE_MISSING_EXPORT_PATH,
           PJ_NATIVE_MODULE_WRONG_ABI_PATH,
           PJ_NATIVE_MODULE_UNREADABLE_MANIFEST_PATH,
       }) {
    std::vector<Diagnostic> diagnostics;
    auto module =
        NativeParserModule::load(path, [&](const Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });

    EXPECT_FALSE(module.has_value()) << path;
    ASSERT_EQ(diagnostics.size(), 1U) << path;
    EXPECT_EQ(diagnostics.front().level, DiagnosticLevel::kError);
    EXPECT_EQ(diagnostics.front().id, path);
    EXPECT_EQ(diagnostics.front().message, module.error());
  }
}

TEST(NativeParserModule, ReportsSpecificLoaderFailureCauses) {
  auto missing = NativeParserModule::load(PJ_NATIVE_MODULE_MISSING_EXPORT_PATH);
  ASSERT_FALSE(missing.has_value());
  EXPECT_NE(missing.error().find(PJ_MODULE_FREE_EXPORT_NAME), std::string::npos);

  auto wrong_abi = NativeParserModule::load(PJ_NATIVE_MODULE_WRONG_ABI_PATH);
  ASSERT_FALSE(wrong_abi.has_value());
  EXPECT_NE(wrong_abi.error().find("ABI mismatch"), std::string::npos);

  auto unreadable = NativeParserModule::load(PJ_NATIVE_MODULE_UNREADABLE_MANIFEST_PATH);
  ASSERT_FALSE(unreadable.has_value());
  EXPECT_NE(unreadable.error().find("manifest is unreadable"), std::string::npos);
}

}  // namespace
}  // namespace PJ
