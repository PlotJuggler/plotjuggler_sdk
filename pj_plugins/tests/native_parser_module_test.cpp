// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/native_parser_module.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "native_parser_module_fixture.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/parser_claim_catalog.hpp"
#include "pj_plugins/host/parser_module_runtime.hpp"
#include "pj_plugins/host/parser_module_session_budget.hpp"

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

TEST(NativeParserModule, EnforcesAggregateBudgetsAtLoadAndCreate) {
  const uint64_t artifact_size = std::filesystem::file_size(PJ_NATIVE_MODULE_FIXTURE_PATH);
  const auto make_budget = [&](ParserModuleSessionBudgetLimits limits) {
    return std::make_shared<ParserModuleSessionBudgetTracker>(limits);
  };
  ParserModuleSessionBudgetLimits limits;
  limits.maximum_modules = 0;
  auto module_budget = make_budget(limits);
  auto module_decline = NativeParserModule::load(PJ_NATIVE_MODULE_FIXTURE_PATH, module_budget);
  ASSERT_FALSE(module_decline.has_value());
  EXPECT_NE(module_decline.error().find("module_count"), std::string::npos);
  EXPECT_EQ(module_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_artifact_bytes = artifact_size - 1;
  auto artifact_budget = make_budget(limits);
  auto artifact = NativeParserModule::load(PJ_NATIVE_MODULE_FIXTURE_PATH, artifact_budget);
  ASSERT_FALSE(artifact.has_value());
  EXPECT_NE(artifact.error().find("artifact_file_size"), std::string::npos);
  EXPECT_EQ(artifact_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_claims = pj_fixture::kClaimCount - 1;
  auto claim_budget = make_budget(limits);
  auto claims = NativeParserModule::load(PJ_NATIVE_MODULE_FIXTURE_PATH, claim_budget);
  ASSERT_FALSE(claims.has_value());
  EXPECT_NE(claims.error().find("total_claims"), std::string::npos);
  EXPECT_EQ(claim_budget->usage().modules, 0U);

  limits = {};
  limits.maximum_active_instances = 0;
  auto instance_budget = make_budget(limits);
  auto module = NativeParserModule::load(PJ_NATIVE_MODULE_FIXTURE_PATH, instance_budget);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = NativeParserModuleInstance::create(*module, 0);
  ASSERT_FALSE(instance.has_value());
  EXPECT_NE(instance.error().find("active_instances"), std::string::npos);
  EXPECT_EQ(instance_budget->usage().active_instances, 0U);
}

}  // namespace
}  // namespace PJ
