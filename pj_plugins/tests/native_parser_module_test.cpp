// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/native_parser_module.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "native_parser_module_fixture.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/parser_claim_catalog.hpp"

namespace PJ {
namespace {

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
