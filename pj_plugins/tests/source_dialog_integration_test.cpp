// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "detail/library_loader.hpp"
#include "pj_plugins/host/config_envelope.hpp"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/dialog_handle.hpp"

#ifndef PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH
#error "PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH must be defined"
#endif

#ifndef PJ_MOCK_DATA_SOURCE_PLUGIN_PATH
#error "PJ_MOCK_DATA_SOURCE_PLUGIN_PATH must be defined"
#endif

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory()
      : path_(
            std::filesystem::temp_directory_path() /
            ("pj_dialog_loader_" +
             std::to_string(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())))) {
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

class CurrentPathGuard {
 public:
  CurrentPathGuard() : original_(std::filesystem::current_path()) {}

  ~CurrentPathGuard() {
    std::error_code error;
    std::filesystem::current_path(original_, error);
  }

  const std::filesystem::path& original() const noexcept {
    return original_;
  }

 private:
  std::filesystem::path original_;
};

// --- Test 1: Load combined .so ---

TEST(SourceDialogIntegration, LoadCombinedPlugin) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  EXPECT_TRUE(lib->valid());
}

// --- Test 2: Capability check ---

TEST(SourceDialogIntegration, HasDialogCapability) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  auto handle = lib->createHandle();
  EXPECT_TRUE(handle.valid());
  EXPECT_NE(handle.capabilities() & PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG, 0u);
  EXPECT_NE(handle.capabilities() & PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM, 0u);
  EXPECT_NE(handle.capabilities() & PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST, 0u);
}

// --- Test 3: Resolve dialog vtable ---

TEST(SourceDialogIntegration, ResolveDialogVtable) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  auto dialog_vt = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vt) << dialog_vt.error();
  EXPECT_EQ((*dialog_vt)->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
}

TEST(SourceDialogIntegration, DialogVtableSurvivesCandidateFileDeletion) {
  TemporaryDirectory temporary;
  const std::filesystem::path plugin_filename =
      std::filesystem::path(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH).filename();
  const std::filesystem::path candidate = temporary.path() / plugin_filename;
  std::filesystem::copy_file(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH, candidate);

  {
    auto lib = PJ::DataSourceLibrary::load(candidate);
    ASSERT_TRUE(lib) << lib.error();
#if defined(_WIN32)
    // A mapped image cannot be deleted on Windows, but it can be renamed away;
    // deferred resolution must not depend on the original path either way.
    std::filesystem::rename(candidate, candidate.parent_path() / "moved-away.dll");
#else
    ASSERT_TRUE(std::filesystem::remove(candidate));
#endif

    auto dialog_vtable = lib->resolveDialogVtable();
    ASSERT_TRUE(dialog_vtable) << dialog_vtable.error();
    EXPECT_EQ((*dialog_vtable)->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
  }

#if !defined(_WIN32)
  const std::filesystem::path real_directory = temporary.path() / "real";
  const std::filesystem::path symlink_directory = temporary.path() / "symlink";
  std::filesystem::create_directories(real_directory);
  std::filesystem::create_directory_symlink(real_directory, symlink_directory);
  const std::filesystem::path real_candidate = real_directory / plugin_filename;
  const std::filesystem::path symlink_candidate = symlink_directory / plugin_filename;
  std::filesystem::copy_file(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH, real_candidate);

  // Preload the real spelling so glibc reuses a link-map whose dli_fname is the
  // canonical path when the library API subsequently loads through the symlink.
  // This reproduces dyld's realpath reporting on Linux.
  auto preloaded_handle = PJ::detail::loadLibraryHandle(real_candidate);
  ASSERT_TRUE(preloaded_handle) << preloaded_handle.error();
  auto preloaded_owner = PJ::detail::adoptLibraryHandle(*preloaded_handle);

  auto symlink_lib = PJ::DataSourceLibrary::load(symlink_candidate);
  ASSERT_TRUE(symlink_lib) << symlink_lib.error();
  ASSERT_TRUE(std::filesystem::remove(real_candidate));

  auto symlink_dialog_vtable = symlink_lib->resolveDialogVtable();
  ASSERT_TRUE(symlink_dialog_vtable) << symlink_dialog_vtable.error();
  EXPECT_EQ((*symlink_dialog_vtable)->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
#endif
}

TEST(SourceDialogIntegration, DialogVtableSurvivesCwdChangeAfterRelativeLoad) {
  TemporaryDirectory temporary;
  const std::filesystem::path candidate =
      temporary.path() / std::filesystem::path(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH).filename();
  std::filesystem::copy_file(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH, candidate);

  CurrentPathGuard current_path;
  std::filesystem::current_path(temporary.path());
  auto lib = PJ::DataSourceLibrary::load(candidate.filename());
  std::filesystem::current_path(current_path.original());

  ASSERT_TRUE(lib) << lib.error();
  auto dialog_vtable = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vtable) << dialog_vtable.error();
  EXPECT_EQ((*dialog_vtable)->protocol_version, PJ_DIALOG_PROTOCOL_VERSION);
}

// --- Test 4: Borrowed dialog context ---

TEST(SourceDialogIntegration, BorrowedDialogContext) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();
  auto source = lib->createHandle();
  ASSERT_TRUE(source.valid());

  void* dialog_ctx = source.getDialog().ctx;
  EXPECT_NE(dialog_ctx, nullptr);
}

// --- Test 5: Borrowed DialogHandle works ---

TEST(SourceDialogIntegration, BorrowedDialogHandleWorks) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto dialog_vt = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vt) << dialog_vt.error();

  auto source = lib->createHandle();
  ASSERT_TRUE(source.valid());

  void* dialog_ctx = source.getDialog().ctx;
  ASSERT_NE(dialog_ctx, nullptr);

  auto dialog = PJ::DialogHandle::borrowed(*dialog_vt, dialog_ctx);

  // widget_data should return valid JSON
  std::string wd = dialog.widget_data();
  EXPECT_FALSE(wd.empty());
  auto j = nlohmann::json::parse(wd, nullptr, false);
  EXPECT_FALSE(j.is_discarded());

  // sendEvent should work
  bool refresh = dialog.sendEvent("host_input", R"({"text": "192.0.2.1"})");
  EXPECT_TRUE(refresh);

  // save_config should return valid JSON
  std::string cfg = dialog.save_config();
  EXPECT_FALSE(cfg.empty());
}

TEST(SourceDialogIntegration, BorrowedDialogWorksAfterLibraryObjectDiesWhileSourceLives) {
  std::unique_ptr<PJ::DataSourceHandle> source;
  std::unique_ptr<PJ::DialogHandle> dialog;

  {
    auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
    ASSERT_TRUE(lib) << lib.error();

    source = std::make_unique<PJ::DataSourceHandle>(lib->createHandle());
    ASSERT_TRUE(source->valid());

    auto borrowed = source->getDialog();
    ASSERT_NE(borrowed.ctx, nullptr);
    ASSERT_NE(borrowed.vtable, nullptr);
    dialog = std::make_unique<PJ::DialogHandle>(PJ::DialogHandle::fromBorrowed(borrowed));
  }

  std::string wd = dialog->widget_data();
  EXPECT_FALSE(wd.empty());
  auto j = nlohmann::json::parse(wd, nullptr, false);
  EXPECT_FALSE(j.is_discarded());

  dialog.reset();
  source.reset();
}

// --- Test 6: Shared state ---

TEST(SourceDialogIntegration, SharedStateBetweenDialogAndSource) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto dialog_vt = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vt) << dialog_vt.error();

  auto source = lib->createHandle();
  void* dialog_ctx = source.getDialog().ctx;
  ASSERT_NE(dialog_ctx, nullptr);

  auto dialog = PJ::DialogHandle::borrowed(*dialog_vt, dialog_ctx);

  // Change dialog state
  (void)dialog.sendEvent("host_input", R"({"text": "shared-host"})");

  // Dialog's save_config should reflect the change
  auto dialog_cfg = nlohmann::json::parse(dialog.save_config());
  EXPECT_EQ(dialog_cfg["host"], "shared-host");

  // Source's saveConfig should match (same underlying state)
  std::string source_saved;
  ASSERT_TRUE(source.saveConfig(source_saved));
  auto source_cfg = nlohmann::json::parse(source_saved);
  EXPECT_EQ(source_cfg["host"], "shared-host");
}

// --- Test 7: Headless dialog round-trip (via vtable tick) ---

TEST(SourceDialogIntegration, HeadlessDialogTicksWork) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto dialog_vt = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vt) << dialog_vt.error();

  auto source = lib->createHandle();
  auto dialog = PJ::DialogHandle::borrowed(*dialog_vt, source.getDialog().ctx);

  // Connect first
  (void)dialog.sendEvent("connect_btn", R"({"clicked": true})");

  // Pump ticks until topics appear
  bool refresh = false;
  for (int i = 0; i < 5; ++i) {
    refresh = dialog.tick();
    if (refresh) {
      break;
    }
  }
  EXPECT_TRUE(refresh);

  // widget_data should now contain topics
  std::string wd = dialog.widget_data();
  auto j = nlohmann::json::parse(wd);
  EXPECT_TRUE(j.contains("topic_list"));
}

// --- Test 8: Config persistence ---

TEST(SourceDialogIntegration, ConfigPersistence) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto source = lib->createHandle();

  // Set some config via dialog
  auto dialog_vt = lib->resolveDialogVtable();
  ASSERT_TRUE(dialog_vt) << dialog_vt.error();
  auto dialog = PJ::DialogHandle::borrowed(*dialog_vt, source.getDialog().ctx);
  (void)dialog.sendEvent("host_input", R"({"text": "persist-host"})");
  (void)dialog.sendEvent("port_input", R"({"value": 7777})");

  // Save and reload
  std::string saved;
  ASSERT_TRUE(source.saveConfig(saved));
  auto source2 = lib->createHandle();
  EXPECT_TRUE(source2.loadConfig(saved));

  // Verify round-trip
  std::string reloaded;
  ASSERT_TRUE(source2.saveConfig(reloaded));
  auto j1 = nlohmann::json::parse(saved);
  auto j2 = nlohmann::json::parse(reloaded);
  EXPECT_EQ(j1["host"], j2["host"]);
  EXPECT_EQ(j1["port"], j2["port"]);
}

// --- Test 9: No-dialog plugin ---

TEST(SourceDialogIntegration, NoDialogPluginReturnsNull) {
  auto lib = PJ::DataSourceLibrary::load(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  ASSERT_TRUE(lib) << lib.error();

  auto source = lib->createHandle();
  EXPECT_TRUE(source.valid());

  // No kCapabilityHasDialog
  EXPECT_EQ(source.capabilities() & PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG, 0u);

  // dialogContext should return null
  EXPECT_EQ(source.getDialog().ctx, nullptr);

  // resolveDialogVtable should fail (no dialog vtable exported)
  auto dialog_vt = lib->resolveDialogVtable();
  EXPECT_FALSE(dialog_vt);
}

// --- Test 10: Config envelope ---

TEST(SourceDialogIntegration, ConfigEnvelopeRoundTrip) {
  std::string source_cfg = R"({"host":"localhost","port":9090})";
  std::string parser_binding = R"({"parser":"json"})";

  std::string packed = PJ::ConfigEnvelope::pack(source_cfg, parser_binding);
  auto unpacked = PJ::ConfigEnvelope::unpack(packed);
  ASSERT_TRUE(unpacked) << unpacked.error();
  EXPECT_EQ(unpacked->source_config, source_cfg);
  EXPECT_EQ(unpacked->parser_binding, parser_binding);
}

TEST(SourceDialogIntegration, ConfigEnvelopeDefaultParserBinding) {
  std::string source_cfg = R"({"host":"example.com"})";
  std::string packed = PJ::ConfigEnvelope::pack(source_cfg);
  auto unpacked = PJ::ConfigEnvelope::unpack(packed);
  ASSERT_TRUE(unpacked) << unpacked.error();
  EXPECT_EQ(unpacked->source_config, source_cfg);
  EXPECT_EQ(unpacked->parser_binding, "{}");
}

TEST(SourceDialogIntegration, ConfigEnvelopeRejectsInvalid) {
  EXPECT_FALSE(PJ::ConfigEnvelope::unpack("not json"));
  EXPECT_FALSE(PJ::ConfigEnvelope::unpack(R"({"version":99})"));
  EXPECT_FALSE(PJ::ConfigEnvelope::unpack(R"({"version":1})"));
}

}  // namespace
