/**
 * @file platform.hpp
 * @brief Qt-free, header-only platform helpers for plugins and core.
 *
 * Provides small cross-platform utilities that plugins would otherwise need
 * to reimplement (and sometimes do incorrectly): reading environment
 * variables without tripping MSVC's C4996 deprecation warning, and locating
 * the per-user data directory that mirrors Qt's
 * QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).
 *
 * Header-only so plugins linked against pj_base pick it up via CPM without
 * pulling additional translation units or Qt dependencies.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PJ::sdk {

/// Read an environment variable.
///
/// Returns std::nullopt if the variable is unset or empty. Wraps std::getenv
/// with a local MSVC C4996 suppression so the call compiles under /W4 /WX
/// without forcing _CRT_SECURE_NO_WARNINGS project-wide.
inline std::optional<std::string> getEnv(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string{value};
}

/// Return the per-user data directory used by PlotJuggler and its plugins.
///
/// Mirrors Qt's `QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/plotjuggler"`,
/// so plugins that need on-disk state (script libraries, cached downloads,
/// etc.) share the same root as the Qt-linked host. Falls back to the
/// system temp directory when no suitable environment variable is set, so
/// the returned path is never empty.
///
/// Platform resolution:
/// - Windows: `%LOCALAPPDATA%/plotjuggler`, then `%USERPROFILE%/AppData/Local/plotjuggler`
/// - macOS:   `$HOME/Library/Application Support/plotjuggler`
/// - Linux:   `$XDG_DATA_HOME/plotjuggler`, then `$HOME/.local/share/plotjuggler`
inline std::filesystem::path userDataDir() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  if (auto v = getEnv("LOCALAPPDATA")) {
    return fs::path(*v) / "plotjuggler";
  }
  if (auto v = getEnv("USERPROFILE")) {
    return fs::path(*v) / "AppData" / "Local" / "plotjuggler";
  }
#elif defined(__APPLE__)
  if (auto v = getEnv("HOME")) {
    return fs::path(*v) / "Library" / "Application Support" / "plotjuggler";
  }
#else
  if (auto v = getEnv("XDG_DATA_HOME")) {
    return fs::path(*v) / "plotjuggler";
  }
  if (auto v = getEnv("HOME")) {
    return fs::path(*v) / ".local" / "share" / "plotjuggler";
  }
#endif
  return fs::temp_directory_path() / "plotjuggler";
}

/// Return the directory containing the shared library (.so / .dll) that holds
/// @p fn_addr at runtime.
///
/// Pass the address of any function defined in the same translation unit as
/// the call site — the linker ensures the address resolves to the correct
/// module. Returns an empty path on failure (requires `${CMAKE_DL_LIBS}` on
/// Linux / macOS; no extra link dep on Windows or macOS).
///
/// Platform implementation:
/// - Linux / macOS: dladdr()
/// - Windows:       GetModuleHandleExW() + GetModuleFileNameW()
inline std::filesystem::path getSharedLibDir(const void* fn_addr) {
  namespace fs = std::filesystem;
#if defined(__linux__) || defined(__APPLE__)
  ::Dl_info info{};
  if (::dladdr(fn_addr, &info) && info.dli_fname) {
    return fs::path(info.dli_fname).parent_path();
  }
#elif defined(_WIN32)
  wchar_t buf[MAX_PATH] = {};
  HMODULE hm = nullptr;
  if (::GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(fn_addr), &hm)) {
    ::GetModuleFileNameW(hm, buf, MAX_PATH);
    return fs::path(buf).parent_path();
  }
#else
  (void)fn_addr;
#endif
  return {};
}

}  // namespace PJ::sdk
