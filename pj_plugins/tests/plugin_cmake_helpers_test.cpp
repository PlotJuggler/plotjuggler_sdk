// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Proves the CMake plugin helpers (cmake/PjPlugin.cmake) through the real ABI:
// the plugin they configured is loaded with the host loaders and what the
// helpers embedded / emitted / localized is checked byte-for-byte.

#include <gtest/gtest.h>
#include <pj_base/plugin_data_api.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <pj_plugins/host/data_source_library.hpp>
#include <pj_plugins/host/dialog_library.hpp>
#include <string>

#if defined(__linux__)
#include <dlfcn.h>
#endif

#ifndef PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH
#error "PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH
#error "PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH must be defined"
#endif
#ifndef PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH
#error "PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH must be defined"
#endif
#ifndef PJ_CMAKE_HELPERS_FIXTURE_UI_PATH
#error "PJ_CMAKE_HELPERS_FIXTURE_UI_PATH must be defined"
#endif

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << path;
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path sidecarFor(const std::filesystem::path& dso, const std::string& target) {
  return dso.parent_path() / (target + ".pjmanifest.json");
}

#if defined(__linux__)
bool exportsSymbol(const char* dso_path, const char* symbol) {
  void* handle = dlopen(dso_path, RTLD_NOW | RTLD_LOCAL);
  EXPECT_NE(handle, nullptr) << dlerror();
  if (handle == nullptr) {
    return false;
  }
  const bool found = dlsym(handle, symbol) != nullptr;
  dlclose(handle);
  return found;
}
#endif

}  // namespace

TEST(PluginCmakeHelpers, EmbeddedManifestMatchesSourceFile) {
  auto library = PJ::DataSourceLibrary::load(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_EQ(library->createHandle().manifest(), readFile(PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH));
}

TEST(PluginCmakeHelpers, EmbeddedUiMatchesSourceFile) {
  auto library = PJ::DialogLibrary::load(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  auto handle = library->createHandle();
  EXPECT_EQ(handle.ui_content(), readFile(PJ_CMAKE_HELPERS_FIXTURE_UI_PATH));
  EXPECT_EQ(handle.manifest(), readFile(PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH));
}

TEST(PluginCmakeHelpers, SidecarCarriesPrimaryFamilyAndAbiMajor) {
  const auto sidecar = sidecarFor(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH, "cmake_helpers_fixture_plugin");
  ASSERT_TRUE(std::filesystem::exists(sidecar)) << sidecar;
  const auto json = nlohmann::json::parse(readFile(sidecar));
  const auto source = nlohmann::json::parse(readFile(PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH));
  EXPECT_EQ(json.at("family"), "data_source");  // first of FAMILIES data_source dialog
  EXPECT_EQ(json.at("abi_major"), PJ_ABI_VERSION);
  EXPECT_EQ(json.at("id"), source.at("id"));
  EXPECT_EQ(json.at("version"), source.at("version"));
}

TEST(PluginCmakeHelpers, LegacyAliasStillConfiguresALoadablePlugin) {
  auto library = PJ::DataSourceLibrary::load(PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH);
  ASSERT_TRUE(library) << library.error();
  EXPECT_EQ(library->createHandle().manifest(), readFile(PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH));
  const auto sidecar = sidecarFor(PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH, "cmake_helpers_legacy_plugin");
  ASSERT_TRUE(std::filesystem::exists(sidecar)) << sidecar;
  EXPECT_EQ(nlohmann::json::parse(readFile(sidecar)).at("family"), "data_source");
}

#if defined(__linux__)
TEST(PluginCmakeHelpers, ExportAllowlistLocalizesEverythingButEntryPoints) {
  // pj_configure_plugin applied the version script: the probe is gone ...
  EXPECT_FALSE(exportsSymbol(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH, "pj_cmake_fixture_probe"));
  // ... while the entry points the host needs survived.
  EXPECT_TRUE(exportsSymbol(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH, "pj_plugin_abi_version"));
  EXPECT_TRUE(exportsSymbol(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH, "PJ_get_data_source_vtable"));
  EXPECT_TRUE(exportsSymbol(PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH, "PJ_get_dialog_vtable"));
  // The deprecated alias never hardened, so the same probe is still exported there.
  EXPECT_TRUE(exportsSymbol(PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH, "pj_cmake_fixture_probe"));
}
#endif
