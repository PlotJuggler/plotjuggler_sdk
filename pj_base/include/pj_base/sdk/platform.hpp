/**
 * @file platform.hpp
 * @brief Qt-free, header-only platform helpers for plugins and core.
 *
 * Provides small cross-platform utilities that plugins would otherwise need
 * to reimplement (and sometimes do incorrectly): converting between UTF-8
 * strings and std::filesystem::path, reading environment variables, and
 * locating the per-user data directory that mirrors Qt's
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
#include <string_view>

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

/// Decode @p utf8 as a filesystem path.
///
/// Use this for every path that reaches a plugin as a narrow string (host config
/// such as `"filepath"`, a descriptor's `local_path_utf8`, a dialog field). On
/// Windows, `std::filesystem::path(std::string)`, `std::ifstream(std::string)` and
/// `fopen` read those bytes in the active ANSI code page, so a name with
/// non-ASCII characters (a Cyrillic user name in `%LOCALAPPDATA%`) opens a
/// different file or none. On Windows, bytes that are not valid UTF-8 make this
/// throw `std::system_error`. POSIX paths are bytes, so the result is identical to
/// `std::filesystem::path(utf8)`.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

#if defined(_WIN32)
namespace detail {

/// UTF-16 to UTF-8; an unpaired surrogate becomes U+FFFD instead of an error.
inline std::string wideToUtf8(const std::wstring& wide) {
  const int size =
      ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

}  // namespace detail
#endif

/// Encode @p path as UTF-8, for error text, config values and host calls.
///
/// Use this instead of `path.string()`: on Windows that narrows through the
/// active ANSI code page and throws `std::system_error` for a character the code
/// page cannot represent. An unpaired UTF-16 surrogate (NTFS allows one in a name)
/// becomes U+FFFD. On POSIX the result is the bytes `string()` returns.
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  return detail::wideToUtf8(path.native());  // never throws, unlike path.u8string() on a lone surrogate
#else
  return path.string();
#endif
}

#if defined(_WIN32)
namespace detail {

/// An environment variable as UTF-16, read through the Win32 API so no code page
/// is involved. std::nullopt when unset or empty.
inline std::optional<std::wstring> getEnvWide(const char* name) {
  const std::wstring wide_name = pathFromUtf8(name).native();
  std::wstring value(256, L'\0');
  for (;;) {
    const DWORD size = ::GetEnvironmentVariableW(wide_name.c_str(), value.data(), static_cast<DWORD>(value.size()));
    if (size == 0) {
      return std::nullopt;
    }
    if (size < value.size()) {
      value.resize(size);
      return value;
    }
    value.resize(size);  // buffer too small: size is the length needed, terminator included
  }
}

}  // namespace detail
#endif

/// Read an environment variable; the value is UTF-8 on every platform.
///
/// Returns std::nullopt if the variable is unset or empty. Open a path taken from
/// it with pathFromUtf8().
inline std::optional<std::string> getEnv(const char* name) {
#if defined(_WIN32)
  if (auto wide = detail::getEnvWide(name)) {
    return detail::wideToUtf8(*wide);
  }
  return std::nullopt;
#else
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string{value};
#endif
}

/// Return the per-user data directory used by PlotJuggler and its plugins.
///
/// Mirrors Qt's `QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/plotjuggler"`,
/// so plugins that need on-disk state (script libraries, cached downloads,
/// etc.) share the same root as the Qt-linked host. Falls back to the
/// system temp directory when no suitable environment variable is set, so
/// the returned path is never empty. A non-ASCII profile path survives on
/// Windows: the variables are read as UTF-16, never through a narrow string.
///
/// Platform resolution:
/// - Windows: `%LOCALAPPDATA%/plotjuggler`, then `%USERPROFILE%/AppData/Local/plotjuggler`
/// - macOS:   `$HOME/Library/Application Support/plotjuggler`
/// - Linux:   `$XDG_DATA_HOME/plotjuggler`, then `$HOME/.local/share/plotjuggler`
inline std::filesystem::path userDataDir() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  if (auto v = detail::getEnvWide("LOCALAPPDATA")) {
    return fs::path(*v) / "plotjuggler";
  }
  if (auto v = detail::getEnvWide("USERPROFILE")) {
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
