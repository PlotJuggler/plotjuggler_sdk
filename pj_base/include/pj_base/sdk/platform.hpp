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
#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace PJ::sdk {

/// Read an environment variable.
///
/// Returns std::nullopt if the variable is unset or empty. Wraps std::getenv
/// with a local MSVC C4996 suppression so the call compiles under /W4 /WX
/// without forcing _CRT_SECURE_NO_WARNINGS project-wide.
inline std::optional<std::string> getEnv(const char* name) {
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4996)
#endif
  const char* value = std::getenv(name);
#if defined(_MSC_VER)
#  pragma warning(pop)
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

}  // namespace PJ::sdk
