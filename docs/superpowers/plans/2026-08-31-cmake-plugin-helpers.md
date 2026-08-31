# CMake Plugin Helpers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the plugin-authoring CMake helpers (`pj_configure_plugin`, `pj_embed_file`, `pj_harden_plugin_exports`) with the SDK's `plugin_sdk` component so no plugin repo carries its own copy, releasing as 0.25.0.

**Architecture:** One CMake module `cmake/PjPlugin.cmake` (replacing `cmake/PjPluginManifest.cmake`) plus a script-mode checker `cmake/PjCheckElfPluginExports.cmake`, both installed under `lib/cmake/plotjuggler_sdk/` and auto-included by the package config, the Conan build modules, and the in-tree root `CMakeLists.txt`. Correctness is proven through the real ABI: an in-tree fixture plugin configured by the helpers is loaded with the host loaders and its embedded manifest / UI / sidecar / exported symbols are compared byte-for-byte; CTest also runs the ELF gate in script mode against good and bad DSOs.

**Tech Stack:** CMake ≥ 3.22 (`file(CONFIGURE)`, `string(JSON)`, `CMAKE_CURRENT_FUNCTION_LIST_DIR`), GNU ld version scripts, `nm -D`, GoogleTest, nlohmann/json.

Spec: `docs/superpowers/specs/2026-08-31-cmake-plugin-helpers-design.md`. Work in the worktree `.worktrees/cmake-plugin-helpers` (branch `feat/cmake-plugin-helpers`). Build/test commands run from that directory: `./build.sh --debug && ./test.sh`.

---

## File structure

| Path | Responsibility |
|---|---|
| `cmake/PjPlugin.cmake` (new) | public helpers: `pj_configure_plugin`, `pj_embed_file`, `pj_harden_plugin_exports`, deprecated `pj_emit_plugin_manifest` |
| `cmake/PjCheckElfPluginExports.cmake` (new) | script-mode ELF gate run post-build |
| `cmake/PjPluginManifest.cmake` (deleted) | superseded |
| `pj_plugins/tests/cmake_helpers_fixture/manifest.json`, `dialog.ui` (new) | fixture inputs |
| `pj_plugins/tests/cmake_helpers_fixture_plugin.cpp` (new) | DataSource + Dialog fixture using the generated headers |
| `pj_plugins/tests/plugin_cmake_helpers_test.cpp` (new) | gtest through the host loaders |
| `pj_plugins/CMakeLists.txt` | fixture targets + tests |
| `CMakeLists.txt`, `cmake/plotjuggler_sdkConfig.cmake.in`, `conanfile.py`, `recipe.yaml` | packaging |
| `examples/sdk_consumer/*` | installed-package consumer exercising every helper |
| docs (`pj_plugins/docs/dialog-plugin-guide.md`, `pj_plugins/docs/ARCHITECTURE.md`, `.claude/skills/plotjuggler-plugin/SKILL.md`, `.claude/skills/plotjuggler-plugin/references/dialog.md`, `CLAUDE.md`, `README.md`, `CHANGELOG.md`) | contract |
| `VERSION`, `.gitignore` | release + hygiene |

---

### Task 1: Fixture inputs, fixture plugin, and the failing test

**Files:**
- Create: `pj_plugins/tests/cmake_helpers_fixture/manifest.json`
- Create: `pj_plugins/tests/cmake_helpers_fixture/dialog.ui`
- Create: `pj_plugins/tests/cmake_helpers_fixture_plugin.cpp`
- Create: `pj_plugins/tests/plugin_cmake_helpers_test.cpp`
- Modify: `pj_plugins/CMakeLists.txt` (fixture targets before the `# Tests` banner at ~line 430; test targets after `add_test(NAME parser_route_resolver_test ...)` at ~line 550)

- [ ] **Step 1: Write the fixture inputs**

`pj_plugins/tests/cmake_helpers_fixture/manifest.json`:

```json
{
  "id": "cmake-helpers-fixture",
  "name": "CMake Helpers Fixture",
  "version": "1.2.3",
  "description": "Exercises pj_configure_plugin / pj_embed_file / pj_harden_plugin_exports."
}
```

`pj_plugins/tests/cmake_helpers_fixture/dialog.ui` (the non-ASCII characters are deliberate: they prove the embed is byte-exact):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>CmakeHelpersFixtureDialog</class>
 <widget class="QDialog" name="CmakeHelpersFixtureDialog">
  <property name="windowTitle">
   <string>CMake Helpers Fixture — “embedded” ✓</string>
  </property>
  <layout class="QVBoxLayout" name="root_layout">
   <item>
    <widget class="QLineEdit" name="label_input"/>
   </item>
   <item>
    <widget class="QDialogButtonBox" name="buttonBox">
     <property name="standardButtons">
      <set>QDialogButtonBox::Cancel|QDialogButtonBox::Ok</set>
     </property>
    </widget>
   </item>
  </layout>
 </widget>
</ui>
```

- [ ] **Step 2: Write the fixture plugin**

`pj_plugins/tests/cmake_helpers_fixture_plugin.cpp`:

```cpp
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Fixture for plugin_cmake_helpers_test: a DataSource plus a Dialog whose
// manifest and .ui XML come from headers generated at configure time by
// pj_configure_plugin(MANIFEST_HEADER ...) and pj_embed_file(). The same
// source is also built through the deprecated pj_emit_plugin_manifest() alias.

#include <pj_base/sdk/data_source_patterns.hpp>
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
#include <string_view>

#include "cmake_helpers_fixture_dialog_ui.hpp"  // generated: kFixtureDialogUi
#include "cmake_helpers_fixture_manifest.hpp"   // generated: kFixtureManifest

#if defined(_WIN32)
#define PJ_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PJ_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

// Vague-linkage static with default visibility: GCC emits it as STB_GNU_UNIQUE.
// Without an export allowlist it leaks into .dynsym as a 'u' symbol, which is
// exactly what the gate's negative test looks for on the un-hardened build.
template <class T>
struct PJ_FIXTURE_EXPORT UniqueHolder {
  static int value;
};
template <class T>
int UniqueHolder<T>::value = 0;

class FixtureDialog : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return kFixtureManifest;
  }
  std::string ui_content() const override {
    return kFixtureDialogUi;
  }
  std::string widget_data() override {
    PJ::WidgetData wd;
    wd.setText("label_input", label_);
    return wd.toJson();
  }
  bool onTextChanged(std::string_view widget_name, std::string_view text) override {
    if (widget_name == "label_input") {
      label_ = std::string(text);
    }
    return false;
  }

 private:
  std::string label_ = "fixture";
};

class FixtureSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }
  PJ::Status importData() override {
    return PJ::okStatus();
  }
};

}  // namespace

// Default-visibility symbol that only the version-script allowlist can
// localize; plugin_cmake_helpers_test dlsym()s it to prove the allowlist held.
extern "C" PJ_FIXTURE_EXPORT int pj_cmake_fixture_probe() {
  return ++UniqueHolder<int>::value;
}

PJ_DATA_SOURCE_PLUGIN(FixtureSource, kFixtureManifest)
PJ_DIALOG_PLUGIN(FixtureDialog, kFixtureManifest)
```

- [ ] **Step 3: Write the failing test**

`pj_plugins/tests/plugin_cmake_helpers_test.cpp`:

```cpp
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Proves the CMake plugin helpers (cmake/PjPlugin.cmake) through the real ABI:
// the plugin they configured is loaded with the host loaders and what the
// helpers embedded / emitted / localized is checked byte-for-byte.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <pj_base/plugin_data_api.h>
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
```

- [ ] **Step 4: Wire fixtures and tests in `pj_plugins/CMakeLists.txt`**

Insert immediately before the `# Tests` banner (the `# ------` line above `# Tests`, ~line 429; still inside the `if(PJ_BUILD_TESTS)` block that opened at line 297 — check that `endif()` at ~428 closes the `if(WIN32)` / not the tests block; if it closes the tests block, place the fixtures just before that `endif()`):

```cmake
# ---------------------------------------------------------------------------
# CMake plugin-helper fixtures (cmake/PjPlugin.cmake), consumed by
# plugin_cmake_helpers_test and the script-mode gate tests.
# ---------------------------------------------------------------------------

set(_pj_cmake_fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_helpers_fixture")

add_library(cmake_helpers_fixture_plugin SHARED tests/cmake_helpers_fixture_plugin.cpp)
target_compile_features(cmake_helpers_fixture_plugin PRIVATE cxx_std_20)
target_compile_options(cmake_helpers_fixture_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(cmake_helpers_fixture_plugin PRIVATE pj_plugin_sdk)
pj_configure_plugin(cmake_helpers_fixture_plugin
  FAMILIES        data_source dialog
  MANIFEST_FILE   "${_pj_cmake_fixture_dir}/manifest.json"
  MANIFEST_HEADER cmake_helpers_fixture_generated/cmake_helpers_fixture_manifest.hpp
  MANIFEST_VAR    kFixtureManifest
)
pj_embed_file(cmake_helpers_fixture_plugin
  FILE     "${_pj_cmake_fixture_dir}/dialog.ui"
  HEADER   cmake_helpers_fixture_generated/cmake_helpers_fixture_dialog_ui.hpp
  VAR_NAME kFixtureDialogUi
)

# The same source through the deprecated alias: no export allowlist, so the
# probe symbol stays exported and (GCC) the STB_GNU_UNIQUE static leaks —
# what the gate's negative tests need. Its deprecation notice is expected.
add_library(cmake_helpers_legacy_plugin SHARED tests/cmake_helpers_fixture_plugin.cpp)
target_compile_features(cmake_helpers_legacy_plugin PRIVATE cxx_std_20)
target_compile_options(cmake_helpers_legacy_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(cmake_helpers_legacy_plugin PRIVATE pj_plugin_sdk)
set(CMAKE_WARN_DEPRECATED OFF)
pj_emit_plugin_manifest(cmake_helpers_legacy_plugin
  FAMILY        data_source
  MANIFEST_FILE "${_pj_cmake_fixture_dir}/manifest.json"
)
unset(CMAKE_WARN_DEPRECATED)
pj_embed_file(cmake_helpers_legacy_plugin
  FILE     "${_pj_cmake_fixture_dir}/manifest.json"
  HEADER   cmake_helpers_legacy_generated/cmake_helpers_fixture_manifest.hpp
  VAR_NAME kFixtureManifest
)
pj_embed_file(cmake_helpers_legacy_plugin
  FILE     "${_pj_cmake_fixture_dir}/dialog.ui"
  HEADER   cmake_helpers_legacy_generated/cmake_helpers_fixture_dialog_ui.hpp
  VAR_NAME kFixtureDialogUi
)
```

Append after `add_test(NAME parser_route_resolver_test COMMAND parser_route_resolver_test)`:

```cmake
# CMake plugin helpers: load the helper-configured fixture through the host
# loaders and compare what the helpers embedded / emitted / localized.
add_executable(plugin_cmake_helpers_test tests/plugin_cmake_helpers_test.cpp)
add_dependencies(plugin_cmake_helpers_test cmake_helpers_fixture_plugin cmake_helpers_legacy_plugin)
target_compile_definitions(plugin_cmake_helpers_test PRIVATE
  PJ_CMAKE_HELPERS_FIXTURE_PLUGIN_PATH="$<TARGET_FILE:cmake_helpers_fixture_plugin>"
  PJ_CMAKE_HELPERS_LEGACY_PLUGIN_PATH="$<TARGET_FILE:cmake_helpers_legacy_plugin>"
  PJ_CMAKE_HELPERS_FIXTURE_MANIFEST_PATH="${_pj_cmake_fixture_dir}/manifest.json"
  PJ_CMAKE_HELPERS_FIXTURE_UI_PATH="${_pj_cmake_fixture_dir}/dialog.ui"
)
target_compile_options(plugin_cmake_helpers_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(plugin_cmake_helpers_test PRIVATE
  pj_data_source_host pj_dialog_library pj_dialog_host pj_base
  nlohmann_json::nlohmann_json GTest::gtest_main ${CMAKE_DL_LIBS}
)
add_test(NAME plugin_cmake_helpers_test COMMAND plugin_cmake_helpers_test)

# The ELF gate in script mode: accepts the hardened fixture, rejects a missing
# entry point, and (GCC) rejects the un-hardened build's STB_GNU_UNIQUE leak.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_pj_gate "${plotjuggler_sdk_SOURCE_DIR}/cmake/PjCheckElfPluginExports.cmake")
  add_test(NAME cmake_helpers_gate_accepts_fixture
    COMMAND ${CMAKE_COMMAND}
      -DPLUGIN_SO=$<TARGET_FILE:cmake_helpers_fixture_plugin>
      -DREQUIRED_EXPORTS=pj_plugin_abi_version,PJ_get_data_source_vtable,PJ_get_dialog_vtable
      -DNM_TOOL=${CMAKE_NM}
      -P "${_pj_gate}")
  add_test(NAME cmake_helpers_gate_rejects_missing_export
    COMMAND ${CMAKE_COMMAND}
      -DPLUGIN_SO=$<TARGET_FILE:cmake_helpers_fixture_plugin>
      -DREQUIRED_EXPORTS=PJ_get_toolbox_vtable
      -DNM_TOOL=${CMAKE_NM}
      -P "${_pj_gate}")
  set_tests_properties(cmake_helpers_gate_rejects_missing_export PROPERTIES
    PASS_REGULAR_EXPRESSION "required export \"PJ_get_toolbox_vtable\" is missing")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_test(NAME cmake_helpers_gate_rejects_unique_symbol
      COMMAND ${CMAKE_COMMAND}
        -DPLUGIN_SO=$<TARGET_FILE:cmake_helpers_legacy_plugin>
        -DREQUIRED_EXPORTS=pj_plugin_abi_version
        -DNM_TOOL=${CMAKE_NM}
        -P "${_pj_gate}")
    set_tests_properties(cmake_helpers_gate_rejects_unique_symbol PROPERTIES
      PASS_REGULAR_EXPRESSION "STB_GNU_UNIQUE")
  endif()
endif()
```

- [ ] **Step 5: Run configure to verify it fails for the right reason**

Run: `cd .worktrees/cmake-plugin-helpers && cmake -S . -B build/debug_asan 2>&1 | grep -E 'Unknown CMake command|Error' | head -3`
Expected: `Unknown CMake command "pj_configure_plugin"`.

- [ ] **Step 6: Commit the failing test**

```bash
git add pj_plugins/tests/cmake_helpers_fixture pj_plugins/tests/cmake_helpers_fixture_plugin.cpp pj_plugins/tests/plugin_cmake_helpers_test.cpp pj_plugins/CMakeLists.txt
git commit -m "test(pj_plugins): fixture + tests for the SDK CMake plugin helpers"
```

---

### Task 2: `cmake/PjPlugin.cmake` + `cmake/PjCheckElfPluginExports.cmake`

**Files:**
- Create: `cmake/PjPlugin.cmake`
- Create: `cmake/PjCheckElfPluginExports.cmake`
- Delete: `cmake/PjPluginManifest.cmake`
- Modify: `CMakeLists.txt:16-18` (`include(PjPlugin)`), `CMakeLists.txt:159-165` (`install(FILES ...)`)

- [ ] **Step 1: Write the checker script**

`cmake/PjCheckElfPluginExports.cmake`:

```cmake
# PjCheckElfPluginExports.cmake — post-build gate for ELF plugin DSOs.
#
# Run in CMake script mode by pj_harden_plugin_exports() (cmake/PjPlugin.cmake):
#
#   cmake -DPLUGIN_SO=<path/to/plugin.so>
#         -DREQUIRED_EXPORTS=<sym1,sym2,...>
#         [-DNM_TOOL=<toolchain nm>]
#         -P PjCheckElfPluginExports.cmake
#
# Fails when:
#   1. the DSO exports any STB_GNU_UNIQUE symbol ('u' in `nm -D`). Exported
#      unique symbols are process-global despite RTLD_LOCAL: glibc pins the
#      first DSO providing such a name (dlclose stops unmapping it) and a
#      second copy of the plugin loaded from another path binds into the first
#      copy's statics — skipped constructors and cross-build state mixing.
#   2. any of REQUIRED_EXPORTS (comma-separated) is missing from the dynamic
#      symbol table — catches an over-aggressive export allowlist that would
#      make the host reject the plugin at the ABI handshake.

if(NOT PLUGIN_SO OR NOT REQUIRED_EXPORTS)
  message(FATAL_ERROR "PjCheckElfPluginExports: PLUGIN_SO and REQUIRED_EXPORTS are required")
endif()

# NM_TOOL lets the caller pass the toolchain's nm (CMAKE_NM) so cross-builds
# do not inspect target ELF files with an incompatible host nm.
if(NOT NM_TOOL)
  set(NM_TOOL nm)
endif()

execute_process(
  COMMAND "${NM_TOOL}" -D "${PLUGIN_SO}"
  OUTPUT_VARIABLE _dynsym
  ERROR_VARIABLE _nm_err
  RESULT_VARIABLE _nm_rc
)
if(NOT _nm_rc EQUAL 0)
  message(FATAL_ERROR
    "PjCheckElfPluginExports: ${NM_TOOL} -D failed on ${PLUGIN_SO} (rc=${_nm_rc}): ${_nm_err}")
endif()

string(REGEX MATCHALL "[^\n]*[ \t]u[ \t][^\n]*" _unique_syms "${_dynsym}")
list(LENGTH _unique_syms _unique_count)
if(_unique_count GREATER 0)
  list(SUBLIST _unique_syms 0 5 _unique_sample)
  list(JOIN _unique_sample "\n  " _unique_sample_text)
  message(FATAL_ERROR
    "PjCheckElfPluginExports: ${PLUGIN_SO} exports ${_unique_count} STB_GNU_UNIQUE "
    "symbol(s); they must all be localized (pj_configure_plugin / "
    "pj_harden_plugin_exports apply the version script that does). First few:\n"
    "  ${_unique_sample_text}")
endif()

# T/W: code, D/B/V: data (initialized / bss / weak), R: read-only data — the
# boot symbol pj_plugin_abi_version is a const object in some plugins.
string(REPLACE "," ";" _required "${REQUIRED_EXPORTS}")
foreach(_symbol IN LISTS _required)
  if(NOT _dynsym MATCHES "[ \t][TWVDBR][ \t]+${_symbol}(\n|$)")
    message(FATAL_ERROR
      "PjCheckElfPluginExports: required export \"${_symbol}\" is missing from "
      "${PLUGIN_SO} — the host would reject the plugin at the ABI handshake. "
      "Check the allowlist's REQUIRED_EXPORTS / FAMILIES.")
  endif()
endforeach()

message(STATUS "PjCheckElfPluginExports: ${PLUGIN_SO} — 0 unique symbols, all required exports present")
```

- [ ] **Step 2: Write the module**

`cmake/PjPlugin.cmake`:

```cmake
# PjPlugin.cmake — CMake helpers for authoring PlotJuggler plugin shared libraries.
#
# Shipped with the plotjuggler_sdk::plugin_sdk component: find_package(plotjuggler_sdk
# COMPONENTS plugin_sdk) and the in-tree add_subdirectory() build both include it, so plugin
# CMakeLists never copy these helpers.
#
#   pj_configure_plugin(<target> FAMILIES <family>... [options])
#     Turn a SHARED/MODULE target into a correct PlotJuggler plugin: symbol isolation + rpath,
#     manifest validation + sidecar, optional manifest header embed, export allowlist + gate.
#   pj_embed_file(<target> FILE <path> HEADER <path> VAR_NAME <identifier>)
#     Embed any file (Qt Designer .ui XML, manifest.json, ...) as a constexpr char array.
#   pj_harden_plugin_exports(<target> [FAMILIES ...] [REQUIRED_EXPORTS ...] [EXTRA_EXPORTS ...])
#     Restrict a plugin DSO's dynamic exports to the ABI entry points (Linux/ELF only).
#   pj_emit_plugin_manifest(<target> FAMILY <family> [MANIFEST_FILE <path>] [ABI_MAJOR <n>])
#     Deprecated (0.25.0) alias of pj_configure_plugin(... NO_EXPORT_HARDENING).

include_guard(GLOBAL)
include(GNUInstallDirs)  # CMAKE_INSTALL_LIBDIR — the sidecar install rule needs it

function(_pj_plugin_check_family CONTEXT FAMILY)
  set(_families data_source message_parser toolbox dialog)
  if(NOT FAMILY IN_LIST _families)
    message(FATAL_ERROR "${CONTEXT}: family \"${FAMILY}\" is invalid. Must be one of: ${_families}")
  endif()
endfunction()

function(_pj_plugin_check_target CONTEXT TARGET)
  if(NOT TARGET "${TARGET}")
    message(FATAL_ERROR "${CONTEXT}: \"${TARGET}\" is not a target — call add_library() first")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# pj_embed_file
# ---------------------------------------------------------------------------
# pj_embed_file(<target>
#   FILE     <path>          # input, relative to CMAKE_CURRENT_SOURCE_DIR
#   HEADER   <path>          # generated header, relative to CMAKE_CURRENT_BINARY_DIR
#   VAR_NAME <identifier>)   # name of the generated `inline constexpr char <VAR_NAME>[]`
#
# Generates, at configure time, a header holding the file's bytes plus a NUL terminator:
#   inline constexpr char kMyDialogUi[] = { 0x3c, 0x3f, ..., 0x00 };
# and adds the header's directory to the target's PRIVATE include path, so the plugin does
#   #include "my_dialog_ui.hpp"
#   std::string ui_content() const override { return kMyDialogUi; }
#
# Bytes rather than a string literal: MSVC caps one literal at 16380 characters (C2026) and
# a raw string would need a delimiter that cannot appear in the file. The input is tracked as
# a configure dependency (editing it re-runs CMake); the header is rewritten only when its
# content changes, so a reconfigure does not recompile every translation unit including it.
function(pj_embed_file TARGET)
  set(_options)
  set(_oneValueArgs FILE HEADER VAR_NAME)
  set(_multiValueArgs)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})
  set(_ctx "pj_embed_file(${TARGET})")
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${_ctx}: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  _pj_plugin_check_target("${_ctx}" "${TARGET}")
  foreach(_required FILE HEADER VAR_NAME)
    if(NOT ARG_${_required})
      message(FATAL_ERROR "${_ctx}: ${_required} is required")
    endif()
  endforeach()
  if(NOT ARG_VAR_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "${_ctx}: VAR_NAME \"${ARG_VAR_NAME}\" is not a C++ identifier")
  endif()
  get_filename_component(_file "${ARG_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(_header "${ARG_HEADER}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "${_ctx}: FILE not found: ${_file}")
  endif()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")

  file(READ "${_file}" _hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
  get_filename_component(_file_name "${_file}" NAME)
  file(CONFIGURE OUTPUT "${_header}" CONTENT
"#pragma once
// Generated by pj_embed_file() from ${_file_name} — edit that file, not this header.
inline constexpr char ${ARG_VAR_NAME}[] = {
${_bytes}0x00};
" @ONLY)

  get_filename_component(_header_dir "${_header}" DIRECTORY)
  target_include_directories(${TARGET} PRIVATE "${_header_dir}")
  target_sources(${TARGET} PRIVATE "${_header}")
endfunction()

# ---------------------------------------------------------------------------
# pj_harden_plugin_exports
# ---------------------------------------------------------------------------
# pj_harden_plugin_exports(<target>
#   [FAMILIES <family>...]           # entry points derived as PJ_get_<family>_vtable
#   [REQUIRED_EXPORTS <symbol>...]   # further symbols that must be exported (kept + checked)
#   [EXTRA_EXPORTS <symbol>...])     # symbols to keep exported without checking presence
#
# Linux/ELF only (a silent no-op elsewhere): links the DSO with a version script whose
# `global:` list is the plugin ABI boot symbol (pj_plugin_abi_version), every family entry
# point, and the caller's REQUIRED/EXTRA exports, with `local: *` for everything else. A
# post-build step (PjCheckElfPluginExports.cmake, run in CMake script mode with the
# toolchain's nm) then fails the build if any STB_GNU_UNIQUE symbol is still exported or a
# required entry point is missing.
#
# Why: exported STB_GNU_UNIQUE symbols (vague-linkage statics from headers, statically linked
# gRPC/Abseil/Arrow/protobuf, libstdc++ instantiations) are process-global despite RTLD_LOCAL.
# glibc pins the first DSO providing such a name (dlclose stops unmapping it), a second copy
# of the plugin loaded from another path binds into the first copy's statics with init guards
# already set, and two different plugins embedding the same dependency silently share one
# copy's state. A version script — rather than --exclude-libs,ALL — because
# pj_plugin_abi_version is a weak header-emitted definition that static-archive members on
# the link line also carry: hiding the archive copy would demote every copy through ELF
# visibility merging, whereas the version script selects by final symbol name after
# resolution.
#
# All four family entry points are always kept exported (naming an absent symbol is a
# no-op), so an embedded dialog's PJ_get_dialog_vtable is never localized by mistake;
# FAMILIES decides which of them the gate insists on. PE exports nothing without dllexport
# and Mach-O has no unique binding, so the problem does not exist there.
function(pj_harden_plugin_exports TARGET)
  set(_options)
  set(_oneValueArgs)
  set(_multiValueArgs FAMILIES REQUIRED_EXPORTS EXTRA_EXPORTS)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})
  set(_ctx "pj_harden_plugin_exports(${TARGET})")
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${_ctx}: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  _pj_plugin_check_target("${_ctx}" "${TARGET}")
  if(NOT ARG_FAMILIES AND NOT ARG_REQUIRED_EXPORTS)
    message(FATAL_ERROR "${_ctx}: FAMILIES or REQUIRED_EXPORTS is required")
  endif()

  set(_required pj_plugin_abi_version)
  foreach(_family IN LISTS ARG_FAMILIES)
    _pj_plugin_check_family("${_ctx}" "${_family}")
    list(APPEND _required "PJ_get_${_family}_vtable")
  endforeach()
  list(APPEND _required ${ARG_REQUIRED_EXPORTS})
  list(REMOVE_DUPLICATES _required)

  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
  endif()

  set(_global
    pj_plugin_abi_version
    PJ_get_data_source_vtable
    PJ_get_message_parser_vtable
    PJ_get_toolbox_vtable
    PJ_get_dialog_vtable
    ${ARG_REQUIRED_EXPORTS}
    ${ARG_EXTRA_EXPORTS})
  list(REMOVE_DUPLICATES _global)
  set(_global_lines "")
  foreach(_symbol IN LISTS _global)
    string(APPEND _global_lines "    ${_symbol};\n")
  endforeach()
  set(_map "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_exports.map")
  file(CONFIGURE OUTPUT "${_map}" CONTENT
"/* Generated by pj_harden_plugin_exports(${TARGET}) — edit the CMake call, not this file. */
{
  global:
${_global_lines}  local:
    *;
};
" @ONLY)

  set(_checker "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PjCheckElfPluginExports.cmake")
  target_link_options(${TARGET} PRIVATE "LINKER:--version-script=${_map}")
  # Re-link (and re-gate) when the generated map or the checker changes.
  set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_map}" "${_checker}")

  list(JOIN _required "," _required_csv)
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DPLUGIN_SO=$<TARGET_FILE:${TARGET}>
      -DREQUIRED_EXPORTS=${_required_csv}
      -DNM_TOOL=${CMAKE_NM}
      -P "${_checker}"
    COMMENT "${TARGET}: verifying export allowlist (no STB_GNU_UNIQUE leaks, entry points present)"
    VERBATIM)
endfunction()

# ---------------------------------------------------------------------------
# pj_configure_plugin
# ---------------------------------------------------------------------------
# pj_configure_plugin(<target>
#   FAMILIES <family>...              # data_source | message_parser | toolbox | dialog;
#                                     # the first is the primary family recorded in the sidecar
#   [MANIFEST_FILE <path>]            # default: ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
#   [MANIFEST_HEADER <path>]          # also embed MANIFEST_FILE as a constexpr header (pj_embed_file)
#   [MANIFEST_VAR <identifier>]       # its variable name; default kPluginManifest
#   [ABI_MAJOR <n>]                   # sidecar hint; default PJ_ABI_VERSION
#   [EXTRA_EXPORTS <symbol>...]       # extra symbols to keep exported (see pj_harden_plugin_exports)
#   [NO_EXPORT_HARDENING])            # skip the export allowlist + gate
#
# The one call that turns `add_library(<target> SHARED ...)` into a correct PlotJuggler
# plugin. Typical use:
#
#   add_library(my_plugin SHARED my_plugin.cpp)
#   target_link_libraries(my_plugin PRIVATE plotjuggler_sdk::plugin_sdk)
#   pj_configure_plugin(my_plugin
#     FAMILIES        data_source dialog
#     MANIFEST_HEADER generated/my_manifest.hpp   # -> #include "my_manifest.hpp"
#     MANIFEST_VAR    kMyManifest)                # -> PJ_DATA_SOURCE_PLUGIN(MyPlugin, kMyManifest)
#
# What it does, in order:
#  1. Validates MANIFEST_FILE: it must exist and hold non-empty string "id", "name", "version".
#  2. Symbol isolation + rpath (see the comment in the body).
#  3. Writes <target>.pjmanifest.json — the source manifest plus "abi_major" and "family" —
#     next to the DSO and installs it alongside. The sidecar is for inspection, packaging
#     diagnostics and developer tooling only: runtime discovery reads the manifest embedded in
#     the DSO, so keep the JSON you pass to PJ_*_PLUGIN() identical to MANIFEST_FILE — easiest
#     with MANIFEST_HEADER, which makes manifest.json the single source.
#  4. MANIFEST_HEADER: pj_embed_file(<target> FILE <manifest> HEADER <h> VAR_NAME <v>).
#  5. Unless NO_EXPORT_HARDENING: pj_harden_plugin_exports(<target> FAMILIES ... EXTRA_EXPORTS ...).
#     Use NO_EXPORT_HARDENING (and call pj_harden_plugin_exports yourself) only when the DSO's
#     entry points are not the standard PJ_get_<family>_vtable set.
function(pj_configure_plugin TARGET)
  set(_options NO_EXPORT_HARDENING)
  set(_oneValueArgs MANIFEST_FILE MANIFEST_HEADER MANIFEST_VAR ABI_MAJOR)
  set(_multiValueArgs FAMILIES EXTRA_EXPORTS)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})
  set(_ctx "pj_configure_plugin(${TARGET})")
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${_ctx}: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  _pj_plugin_check_target("${_ctx}" "${TARGET}")
  if(NOT ARG_FAMILIES)
    message(FATAL_ERROR "${_ctx}: FAMILIES is required")
  endif()
  foreach(_family IN LISTS ARG_FAMILIES)
    _pj_plugin_check_family("${_ctx}" "${_family}")
  endforeach()
  list(GET ARG_FAMILIES 0 _primary_family)
  if(ARG_MANIFEST_VAR AND NOT ARG_MANIFEST_HEADER)
    message(FATAL_ERROR "${_ctx}: MANIFEST_VAR requires MANIFEST_HEADER")
  endif()
  if(NOT ARG_MANIFEST_FILE)
    set(ARG_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json")
  endif()
  get_filename_component(_manifest "${ARG_MANIFEST_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_manifest}")
    message(FATAL_ERROR "${_ctx}: MANIFEST_FILE not found: ${_manifest}")
  endif()
  if(NOT ARG_ABI_MAJOR)
    # Matches PJ_ABI_VERSION in pj_base/plugin_data_api.h. Bump in lockstep.
    set(ARG_ABI_MAJOR 5)
  endif()

  # --- 1. Manifest validation ------------------------------------------------
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")
  file(READ "${_manifest}" _src_json)
  foreach(_key IN ITEMS id name version)
    string(JSON _key_type ERROR_VARIABLE _err TYPE "${_src_json}" "${_key}")
    if(_err OR NOT _key_type STREQUAL "STRING")
      message(FATAL_ERROR "${_ctx}: ${_manifest}: missing required string \"${_key}\" key")
    endif()
    string(JSON _key_value GET "${_src_json}" "${_key}")
    if(_key_value STREQUAL "")
      message(FATAL_ERROR "${_ctx}: ${_manifest}: required string \"${_key}\" key must not be empty")
    endif()
  endforeach()

  # --- 2. Symbol isolation + rpath ------------------------------------------
  # Functional replacement for RTLD_DEEPBIND. Two complementary mechanisms:
  #
  # a. -fvisibility=hidden (compile-time): hides symbols DEFINED in the plugin's
  #    own source files, so the host cannot interpose them.
  # b. -Wl,-Bsymbolic-functions (link-time): makes function calls WITHIN the .so
  #    resolve to the definitions inside it, bypassing the PLT. Critical for
  #    statically bundled deps (e.g. libssl.a from Conan) compiled WITHOUT
  #    -fvisibility=hidden: their symbols enter the .so with DEFAULT visibility,
  #    and without -Bsymbolic-functions their calls would go through the PLT →
  #    the host's namespace first → crash.
  #
  # Together, every function call inside the plugin uses the embedded static
  # copies. The boot-level exports (pj_plugin_abi_version + PJ_get_<family>_vtable)
  # keep visibility("default") via the PJ_*_PLUGIN macros. malloc / pthread /
  # system calls are NOT defined in the plugin, so they still resolve to the
  # host — ASAN malloc interposition works. The version-script allowlist applied
  # in step 5 is the third leg: it also localizes default-visibility symbols
  # that come from static archives. -Bsymbolic-functions is ELF-specific; macOS
  # uses a two-level namespace by default (equivalent behavior).
  set_target_properties(${TARGET} PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    C_VISIBILITY_PRESET       hidden
    VISIBILITY_INLINES_HIDDEN ON
  )
  if(APPLE)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "@loader_path"
      MACOSX_RPATH  ON
    )
  elseif(UNIX)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "$ORIGIN"
    )
  endif()
  target_link_options(${TARGET} PRIVATE
    $<$<PLATFORM_ID:Linux>:-Wl,-Bsymbolic-functions>
  )

  # --- 3. Sidecar ------------------------------------------------------------
  set(_sidecar_json "${_src_json}")
  string(JSON _sidecar_json SET "${_sidecar_json}" "abi_major" "${ARG_ABI_MAJOR}")
  string(JSON _sidecar_json SET "${_sidecar_json}" "family"    "\"${_primary_family}\"")
  set(_sidecar_path "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pjmanifest.json")
  file(CONFIGURE OUTPUT "${_sidecar_path}" CONTENT "${_sidecar_json}\n" @ONLY)
  add_custom_command(
    TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_sidecar_path}"
      "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.pjmanifest.json"
    COMMENT "Copying human-readable ${TARGET}.pjmanifest.json next to DSO"
    VERBATIM
  )
  get_target_property(_type ${TARGET} TYPE)
  if(_type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
    install(FILES "${_sidecar_path}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  endif()

  # --- 4. Manifest header ----------------------------------------------------
  if(ARG_MANIFEST_HEADER)
    if(NOT ARG_MANIFEST_VAR)
      set(ARG_MANIFEST_VAR kPluginManifest)
    endif()
    pj_embed_file(${TARGET}
      FILE     "${_manifest}"
      HEADER   "${ARG_MANIFEST_HEADER}"
      VAR_NAME "${ARG_MANIFEST_VAR}")
  endif()

  # --- 5. Export allowlist + gate -------------------------------------------
  if(NOT ARG_NO_EXPORT_HARDENING)
    pj_harden_plugin_exports(${TARGET}
      FAMILIES      ${ARG_FAMILIES}
      EXTRA_EXPORTS ${ARG_EXTRA_EXPORTS})
  endif()
endfunction()

# ---------------------------------------------------------------------------
# pj_emit_plugin_manifest — deprecated since 0.25.0
# ---------------------------------------------------------------------------
# pj_emit_plugin_manifest(<target> FAMILY <family> [MANIFEST_FILE <path>] [ABI_MAJOR <n>])
#
# The pre-0.25 name. Behaves exactly as it always did — symbol isolation, rpath, manifest
# validation, sidecar; no export allowlist — by forwarding to
# pj_configure_plugin(<target> FAMILIES <family> ... NO_EXPORT_HARDENING). Kept so plugins
# written against older SDKs configure unchanged; new code calls pj_configure_plugin.
function(pj_emit_plugin_manifest TARGET)
  get_property(_warned GLOBAL PROPERTY _PJ_EMIT_PLUGIN_MANIFEST_WARNED)
  if(NOT _warned)
    set_property(GLOBAL PROPERTY _PJ_EMIT_PLUGIN_MANIFEST_WARNED TRUE)
    message(DEPRECATION
      "pj_emit_plugin_manifest() is deprecated since plotjuggler_sdk 0.25.0: call "
      "pj_configure_plugin(<target> FAMILIES <family> ...) instead, which also applies the "
      "export allowlist (see cmake/PjPlugin.cmake).")
  endif()
  set(_options)
  set(_oneValueArgs FAMILY MANIFEST_FILE ABI_MAJOR)
  set(_multiValueArgs)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "pj_emit_plugin_manifest(${TARGET}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT ARG_FAMILY)
    message(FATAL_ERROR "pj_emit_plugin_manifest(${TARGET}): FAMILY is required")
  endif()
  set(_forward FAMILIES "${ARG_FAMILY}" NO_EXPORT_HARDENING)
  if(ARG_MANIFEST_FILE)
    list(APPEND _forward MANIFEST_FILE "${ARG_MANIFEST_FILE}")
  endif()
  if(ARG_ABI_MAJOR)
    list(APPEND _forward ABI_MAJOR "${ARG_ABI_MAJOR}")
  endif()
  pj_configure_plugin(${TARGET} ${_forward})
endfunction()
```

- [ ] **Step 3: Delete the old module and switch the root build to the new one**

```bash
git rm -q cmake/PjPluginManifest.cmake
```

`CMakeLists.txt` lines 16–18 become:

```cmake
include(GNUInstallDirs)  # CMAKE_INSTALL_LIBDIR, etc. used by PjPlugin
include(PjPlugin)
include(PjParserModule)
```

`CMakeLists.txt` `install(FILES ...)` (~line 159) becomes:

```cmake
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfigVersion.cmake"
    cmake/PjPlugin.cmake
    cmake/PjCheckElfPluginExports.cmake
    cmake/PjParserModule.cmake
    DESTINATION ${PJ_PACKAGE_CMAKE_DIR}
  )
```

- [ ] **Step 4: Build and run the new tests**

Run: `cd .worktrees/cmake-plugin-helpers && ./build.sh --debug 2>&1 | tail -5 && (cd build/debug_asan && ctest --output-on-failure -R 'cmake_helpers|plugin_cmake_helpers')`
Expected: build prints `cmake_helpers_fixture_plugin: verifying export allowlist ...` then `PjCheckElfPluginExports: ... 0 unique symbols, all required exports present`; ctest reports `100% tests passed` for the 4 (GCC) / 3 (Clang) tests.

If `cmake_helpers_gate_rejects_unique_symbol` fails because GCC did not emit a `u` symbol, inspect with `nm -D build/debug_asan/pj_plugins/libcmake_helpers_legacy_plugin.so | grep ' u '`; if empty, replace `UniqueHolder` in the fixture with an inline function-local static (`inline int& fixtureCounter() { static int c = 0; return c; }` marked `PJ_FIXTURE_EXPORT`) whose guard variable GCC always emits as unique.

- [ ] **Step 5: Run the full suite**

Run: `cd .worktrees/cmake-plugin-helpers && ./test.sh 2>&1 | grep -E 'tests passed|Failed'`
Expected: `100% tests passed, 0 tests failed out of 83` (79 + `plugin_cmake_helpers_test` + 3 gate tests on GCC/Linux).

- [ ] **Step 6: Commit**

```bash
git add cmake/PjPlugin.cmake cmake/PjCheckElfPluginExports.cmake CMakeLists.txt
git commit -m "feat(cmake): ship pj_configure_plugin / pj_embed_file / pj_harden_plugin_exports

PjPlugin.cmake replaces PjPluginManifest.cmake. pj_emit_plugin_manifest stays
as a deprecated alias with identical behavior."
```

---

### Task 3: Package config, Conan build modules, conda recipe

**Files:**
- Modify: `cmake/plotjuggler_sdkConfig.cmake.in:24-28`
- Modify: `conanfile.py:142-151`
- Modify: `recipe.yaml:59-61`

- [ ] **Step 1: Package config**

Replace the `plugin_sdk` branch in `cmake/plotjuggler_sdkConfig.cmake.in`:

```cmake
  elseif(_comp STREQUAL "plugin_sdk")
    find_dependency(nlohmann_json)
    # Plugin-authoring CMake helpers (pj_configure_plugin, pj_embed_file,
    # pj_harden_plugin_exports) ship with the component so plugin authors never
    # copy them into their tree.
    include("${CMAKE_CURRENT_LIST_DIR}/PjPlugin.cmake")
    set(plotjuggler_sdk_plugin_sdk_FOUND TRUE)
```

- [ ] **Step 2: Conan build modules**

In `conanfile.py` replace the `cmake_build_modules` block (keep the explanatory comment, update the file name):

```python
        # Conan 2's CMakeDeps only aggregates cmake_build_modules declared at
        # the package level (self.cpp_info), not at component level — declaring
        # it on the `sdk` component below silently produced an empty
        # plotjuggler_sdk_BUILD_MODULES_PATHS_RELEASE in the generated data
        # file. Ship the plugin-authoring helpers (PjPlugin.cmake) and the
        # parser-module helper from the package root so CMakeDeps actually
        # include()s them after find_package() returns.
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join("lib", "cmake", "plotjuggler_sdk", "PjPlugin.cmake"),
            os.path.join("lib", "cmake", "plotjuggler_sdk", "PjParserModule.cmake"),
        ])
```

- [ ] **Step 3: Conda recipe package-contents check**

In `recipe.yaml` replace the `PjPluginManifest.cmake` line with:

```yaml
        - ${{ "lib" if unix else "Library/lib" }}/cmake/plotjuggler_sdk/PjPlugin.cmake
        - ${{ "lib" if unix else "Library/lib" }}/cmake/plotjuggler_sdk/PjCheckElfPluginExports.cmake
        - ${{ "lib" if unix else "Library/lib" }}/cmake/plotjuggler_sdk/PjParserModule.cmake
```

and update its script comment `runs pj_emit_plugin_manifest()` → `runs pj_configure_plugin() + pj_embed_file()`.

- [ ] **Step 4: Verify the installed layout**

Run:
```bash
cd .worktrees/cmake-plugin-helpers && S=/tmp/claude-1000/-home-davide-ws-plotjuggler-plotjuggler-sdk/bad00227-b274-47bf-8ae8-50a021920276/scratchpad
cmake -S . -B $S/build_install -DPJ_INSTALL_SDK=ON -DPJ_BUILD_TESTS=OFF -DPJ_BUILD_PORTED_PLUGINS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(ls build/*/generators/conan_toolchain.cmake | head -1) >/dev/null && cmake --build $S/build_install -j >/dev/null && cmake --install $S/build_install --prefix $S/prefix >/dev/null && ls $S/prefix/lib/cmake/plotjuggler_sdk/
```
Expected listing includes `PjPlugin.cmake PjCheckElfPluginExports.cmake PjParserModule.cmake plotjuggler_sdkConfig.cmake` and **no** `PjPluginManifest.cmake`. (If the conan toolchain path differs, look under `build/debug_asan/generators/` or configure without a toolchain if nlohmann_json is discoverable.)

- [ ] **Step 5: Commit**

```bash
git add cmake/plotjuggler_sdkConfig.cmake.in conanfile.py recipe.yaml
git commit -m "build: install and export PjPlugin.cmake with the plugin_sdk component"
```

---

### Task 4: `examples/sdk_consumer` uses every helper

**Files:**
- Modify: `examples/sdk_consumer/CMakeLists.txt`
- Modify: `examples/sdk_consumer/minimal_data_source.cpp:26`
- Modify: `examples/sdk_consumer/dialog_controls.cpp` (lines 20–~330: the two constants)
- Create: `examples/sdk_consumer/dialog_controls.ui`

- [ ] **Step 1: Move the dialog XML to a `.ui` file**

Copy the exact XML between `R"(` on line 31 and the closing `)"` of `kUiContent` into `examples/sdk_consumer/dialog_controls.ui` (starting with `<?xml version="1.0" encoding="UTF-8"?>`, ending with `</ui>` and a trailing newline). Then delete the `kManifestJson` and `kUiContent` constants (and their comment) from `dialog_controls.cpp` and add, after the standard includes:

```cpp
#include "dialog_controls_manifest.hpp"  // generated by pj_configure_plugin(MANIFEST_HEADER): kManifestJson
#include "dialog_controls_ui.hpp"        // generated by pj_embed_file(): kUiContent
```

The class body (`return kManifestJson;`, `return kUiContent;`, the `PJ_DIALOG_PLUGIN(DialogControlsExample, kManifestJson)` line) stays unchanged. `dialog_manifest.json` already has the same content as the literal.

- [ ] **Step 2: Minimal data source from `manifest.json`**

`examples/sdk_consumer/minimal_data_source.cpp`: add `#include "minimal_data_source_manifest.hpp"  // generated: kMinimalManifest` after the SDK includes and replace the last line with:

```cpp
PJ_DATA_SOURCE_PLUGIN(MinimalDataSource, kMinimalManifest)
```

- [ ] **Step 3: CMake**

`examples/sdk_consumer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(sdk_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk descriptor_import_support)

# A plugin is: add_library(SHARED) + link plugin_sdk + pj_configure_plugin().
# The helper (shipped with plugin_sdk) applies symbol isolation, the export
# allowlist + post-build gate, validates manifest.json, emits the sidecar and
# — with MANIFEST_HEADER — embeds manifest.json so it is the single source.
add_library(minimal_data_source SHARED minimal_data_source.cpp)
target_link_libraries(minimal_data_source PRIVATE plotjuggler_sdk::plugin_sdk)
pj_configure_plugin(minimal_data_source
  FAMILIES        data_source
  MANIFEST_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
  MANIFEST_HEADER generated/minimal_data_source_manifest.hpp
  MANIFEST_VAR    kMinimalManifest
)

add_library(dialog_controls_example SHARED dialog_controls.cpp)
target_link_libraries(dialog_controls_example PRIVATE plotjuggler_sdk::plugin_sdk)
pj_configure_plugin(dialog_controls_example
  FAMILIES        dialog
  MANIFEST_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/dialog_manifest.json
  MANIFEST_HEADER generated/dialog_controls_manifest.hpp
  MANIFEST_VAR    kManifestJson
)
# Keep the .ui editable in Qt Designer; embed it at configure time.
pj_embed_file(dialog_controls_example
  FILE     ${CMAKE_CURRENT_SOURCE_DIR}/dialog_controls.ui
  HEADER   generated/dialog_controls_ui.hpp
  VAR_NAME kUiContent
)

# Probe for the descriptor_import_support component: links the exported
# compiled target and touches one symbol per public header.
add_executable(descriptor_import_probe descriptor_import_probe.cpp)
target_link_libraries(descriptor_import_probe PRIVATE plotjuggler_sdk::descriptor_import_support)
```

- [ ] **Step 4: Build the example against the installed prefix from Task 3**

Run:
```bash
S=/tmp/claude-1000/-home-davide-ws-plotjuggler-plotjuggler-sdk/bad00227-b274-47bf-8ae8-50a021920276/scratchpad
cd .worktrees/cmake-plugin-helpers && cmake -S examples/sdk_consumer -B $S/build_consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$S/prefix -DCMAKE_TOOLCHAIN_FILE=$(ls build/*/generators/conan_toolchain.cmake | head -1) 2>&1 | tail -3 && cmake --build $S/build_consumer -j 2>&1 | grep -E 'PjCheckElfPluginExports|error|Built target' && nm -D $S/build_consumer/libminimal_data_source.so | grep -cE ' (T|V|W|D|R) '
```
Expected: two `PjCheckElfPluginExports: ... 0 unique symbols, all required exports present` lines, all targets built, and the `nm` count is small (only the boot symbol + entry point, no leaked C++ symbols).

- [ ] **Step 5: Commit**

```bash
git add examples/sdk_consumer
git commit -m "examples(sdk_consumer): use pj_configure_plugin + pj_embed_file"
```

---

### Task 5: Documentation

**Files:**
- Modify: `pj_plugins/docs/dialog-plugin-guide.md:243-265`
- Modify: `.claude/skills/plotjuggler-plugin/SKILL.md:57-61, 118-158`
- Modify: `.claude/skills/plotjuggler-plugin/references/dialog.md:166-167`
- Modify: `pj_plugins/docs/ARCHITECTURE.md:215-217`
- Modify: `CLAUDE.md` (Modules list), `README.md` (consumer section)

- [ ] **Step 1: Dialog guide**

Replace the "EmbedUi — external `.ui` files" section with:

````markdown
#### `pj_embed_file` — external `.ui` files

For larger dialogs, keeping the XML inline becomes unwieldy (and MSVC rejects a
single string literal above 16380 characters, C2026). `pj_embed_file`, shipped
with the `plotjuggler_sdk::plugin_sdk` component, converts any file into a
generated header holding it as a `constexpr char` array:

```cmake
pj_embed_file(my_plugin
  FILE     ${CMAKE_CURRENT_SOURCE_DIR}/ui/my_dialog.ui
  HEADER   generated/my_dialog_ui.hpp
  VAR_NAME kMyDialogUi
)
```

Then in your plugin:

```cpp
#include "my_dialog_ui.hpp"  // generated

std::string ui_content() const override { return kMyDialogUi; }
```

The `.ui` file is tracked as a CMake configure dependency — editing it
triggers header regeneration. The same helper (through
`pj_configure_plugin(... MANIFEST_HEADER ...)`) embeds `manifest.json`.
````

- [ ] **Step 2: Skill**

`SKILL.md` Step 0, third bullet: replace "`pj_emit_plugin_manifest` becomes available too" with "`pj_configure_plugin` / `pj_embed_file` are available too". Replace the CMake snippet and the manifest section with:

````markdown
```cmake
cmake_minimum_required(VERSION 3.22)
project(my_plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan / pixi / installed package:
find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)
# Submodule / vendored source instead:  add_subdirectory(plotjuggler_sdk)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE plotjuggler_sdk::plugin_sdk)  # same in all worlds

pj_configure_plugin(my_plugin
  FAMILIES        data_source                 # data_source | message_parser | toolbox | dialog
                                              # (list several: `data_source dialog` for an embedded dialog)
  MANIFEST_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
  MANIFEST_HEADER generated/my_manifest.hpp   # -> #include "my_manifest.hpp"
  MANIFEST_VAR    kMyManifest                 # -> PJ_DATA_SOURCE_PLUGIN(MyPlugin, kMyManifest)
)
# Dialog UI kept as a real Qt Designer file:
# pj_embed_file(my_plugin FILE ui/my_dialog.ui HEADER generated/my_dialog_ui.hpp VAR_NAME kMyDialogUi)
```

The single `plotjuggler_sdk::plugin_sdk` component is the whole author surface —
base + parser SDK + dialog SDK + these CMake helpers. You do **not** link a separate
dialog target downstream (`pj_dialog_sdk` is an in-tree name).

**`pj_configure_plugin` is not optional in practice.** It (1) validates
`manifest.json` (`id`, `name`, `version` required), (2) applies the symbol-isolation
settings that stop your plugin's symbols from clashing with the host's (hidden
visibility everywhere; `-Wl,-Bsymbolic-functions` on Linux/ELF), (3) writes a
human-readable `<target>.pjmanifest.json` sidecar for tooling (runtime discovery
does *not* read it), (4) with `MANIFEST_HEADER` generates the `constexpr` header you
pass to `PJ_*_PLUGIN(Class, kMyManifest)` so `manifest.json` is the single source of
truth, and (5) on Linux links a version-script allowlist that exports **only** the
ABI entry points and fails the build post-link if a `STB_GNU_UNIQUE` symbol leaks or
an entry point is missing. Family extras in the manifest: MessageParser **must**
include `"encoding": ["json", …]` (the host routes payloads by these names,
case-sensitive); DataSource may add `"file_extensions": [".csv"]`.
`pj_emit_plugin_manifest` (pre-0.25 name) still works but is deprecated.
````

`references/dialog.md` line 166–167: `(\`pj_embed_ui\` — see ...)` → `(\`pj_embed_file\` — see \`pj_plugins/docs/dialog-plugin-guide.md\`)`.

- [ ] **Step 3: ARCHITECTURE.md, CLAUDE.md, README.md**

`pj_plugins/docs/ARCHITECTURE.md` "No more RTLD_DEEPBIND" bullet, last sentence → "Plugin-local symbol isolation is the plugin's job: `pj_configure_plugin` (`cmake/PjPlugin.cmake`) applies hidden visibility, `-Wl,-Bsymbolic-functions`, and a version-script export allowlist gated post-build against `STB_GNU_UNIQUE` leaks."

Root `CLAUDE.md` Modules list: add after the `pj_plugins` bullet:

```markdown
- **cmake/** — the plugin-authoring CMake helpers shipped with `plugin_sdk`
  (`PjPlugin.cmake`: `pj_configure_plugin`, `pj_embed_file`, `pj_harden_plugin_exports`;
  `PjCheckElfPluginExports.cmake`: its post-build ELF gate) and `PjParserModule.cmake`
  (`pj_add_parser_module`). These are public API: renaming or changing their arguments follows
  the same versioning contract as headers.
```

`README.md`: find the consumer/`find_package` section and add one sentence: "`find_package(plotjuggler_sdk COMPONENTS plugin_sdk)` also provides `pj_configure_plugin()` / `pj_embed_file()` (see `cmake/PjPlugin.cmake` and `examples/sdk_consumer`)."

- [ ] **Step 4: Commit**

```bash
git add pj_plugins/docs .claude/skills CLAUDE.md README.md
git commit -m "docs: pj_configure_plugin / pj_embed_file replace the copied EmbedUi/EmbedManifest helpers"
```

---

### Task 6: Version, changelog, `.gitignore`

**Files:**
- Modify: `VERSION`, `CHANGELOG.md`, `.gitignore`

- [ ] **Step 1: Bump**

`VERSION` → `0.25.0`. Prepend to `CHANGELOG.md` after the intro:

````markdown
## [0.25.0]

### Feature: plugin-authoring CMake helpers ship with the SDK (MINOR)

`cmake/PjPlugin.cmake` (installed with the `plugin_sdk` component, auto-included by
`find_package(plotjuggler_sdk COMPONENTS plugin_sdk)`, the Conan build modules and the
in-tree build) replaces `cmake/PjPluginManifest.cmake` and absorbs the helpers every
official plugin carried in its own `cmake/` directory:

- `pj_configure_plugin(<target> FAMILIES <f>... [MANIFEST_FILE] [MANIFEST_HEADER]
  [MANIFEST_VAR] [ABI_MAJOR] [EXTRA_EXPORTS] [NO_EXPORT_HARDENING])` — the one call that
  makes a `SHARED` target a correct plugin: manifest validation, symbol isolation + rpath,
  the `.pjmanifest.json` sidecar, an optional `constexpr` header generated from
  `manifest.json` (single source for the `PJ_*_PLUGIN` literal), and on Linux a
  version-script export allowlist with a post-build gate against `STB_GNU_UNIQUE` leaks
  and missing entry points.
- `pj_embed_file(<target> FILE HEADER VAR_NAME)` — generic configure-time file →
  `constexpr char[]` header (Qt Designer `.ui`, manifests, …); rewritten only when the
  content changes.
- `pj_harden_plugin_exports(<target> [FAMILIES] [REQUIRED_EXPORTS] [EXTRA_EXPORTS])` —
  the allowlist + gate on its own, for DSOs with non-standard entry points.
- `pj_emit_plugin_manifest` is **deprecated** (one CMake deprecation notice per configure)
  and forwards to `pj_configure_plugin(... NO_EXPORT_HARDENING)` with identical behavior.

Migration from the copied helpers: `pj_embed_ui` → `pj_embed_file` (`UI_FILE` → `FILE`);
`pj_embed_manifest` + `pj_emit_plugin_manifest` + `pj_harden_plugin_exports` → one
`pj_configure_plugin(... FAMILIES <f>... MANIFEST_HEADER <h> MANIFEST_VAR <v>)`. The
allowlist no longer names the never-shipped `pj_plugin_descriptor_*`; `REQUIRED_EXPORTS`
are now kept exported automatically (no need to repeat them in `EXTRA_EXPORTS`).

Plugins that adopt these helpers pin `plotjuggler_sdk/[>=0.25.0 <1.0.0]`. No ABI change;
`abi/baseline.abi` is untouched.
````

`.gitignore`: append `.worktrees/` (git worktrees live there by policy; it showed as untracked).

- [ ] **Step 2: Commit**

```bash
git add VERSION CHANGELOG.md .gitignore
git commit -m "chore: release 0.25.0 — SDK-shipped CMake plugin helpers"
```

---

### Task 7: Final verification

- [ ] **Step 1: Full build + tests, both configurations**

Run: `cd .worktrees/cmake-plugin-helpers && ./build.sh --debug 2>&1 | tail -3 && ./build.sh 2>&1 | tail -3 && ./test.sh 2>&1 | grep -E 'tests passed|tests failed'`
Expected: both builds succeed; `100% tests passed` for both build dirs.

- [ ] **Step 2: Consumer smoke against a fresh install (repeat Task 3 step 4 + Task 4 step 4 from the final tree)**

Expected: same results as before.

- [ ] **Step 3: Reconfigure-does-not-recompile check**

Run: `cd .worktrees/cmake-plugin-helpers/build/debug_asan && cmake . >/dev/null && cmake --build . --target cmake_helpers_fixture_plugin 2>&1 | grep -cE 'Building CXX'`
Expected: `0` — a no-op reconfigure did not touch the generated headers or the map.

- [ ] **Step 4: Stale-reference grep**

Run: `cd .worktrees/cmake-plugin-helpers && grep -rnE 'PjPluginManifest|pj_embed_ui|pj_embed_manifest|EmbedUi\.cmake|EmbedManifest\.cmake' --include='*.md' --include='*.cmake*' --include='*.py' --include='*.yaml' --include='*.yml' --include=CMakeLists.txt . | grep -vE '^\./(build|\.worktrees|packaging/conan-center-index|docs/superpowers|CHANGELOG)'`
Expected: no output.

- [ ] **Step 5: pre-commit on the whole branch diff**

Run: `cd .worktrees/cmake-plugin-helpers && pre-commit run --from-ref origin/main --to-ref HEAD`
Expected: all hooks Passed/Skipped.
