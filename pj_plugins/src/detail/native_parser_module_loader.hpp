#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <climits>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "detail/library_loader.hpp"
#include "pj_base/expected.hpp"

namespace PJ::detail {

using NativeModuleHandle = void*;

/// Open a narrow native parser-module path. Narrow parser-module paths are
/// UTF-8 by contract, including on Windows where filesystem::path(char*) would
/// otherwise interpret them using the active ANSI code page.
inline Expected<NativeModuleHandle> openNativeParserModule(
    std::string_view path, LibraryPathIdentity* recorded_path = nullptr) {
#if defined(_WIN32)
  if (path.size() > static_cast<size_t>(INT_MAX)) {
    return unexpected("native parser-module path is too long");
  }
  const int required =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
  if (required <= 0) {
    return unexpected("native parser-module path is not valid UTF-8");
  }
  std::wstring wide_path(static_cast<size_t>(required), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), wide_path.data(), required) == 0) {
    return unexpected("native parser-module path conversion failed");
  }
  return loadLibraryHandle(std::filesystem::path(wide_path), recorded_path);
#else
  return loadLibraryHandle(std::filesystem::path(std::string(path)), recorded_path);
#endif
}

/// Resolve a required parser-module export and apply the same defining-module
/// provenance policy as the family plugin loaders.
inline Expected<void*> resolveNativeParserModuleSymbol(
    NativeModuleHandle handle, const char* name, const LibraryPathIdentity& candidate_path) {
  if (handle == nullptr) {
    return unexpected("native parser module is not loaded");
  }
  return resolveSymbol(handle, name, candidate_path);
}

}  // namespace PJ::detail
