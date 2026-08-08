#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "pj_base/expected.hpp"

namespace PJ::detail {

using NativeModuleHandle = void*;

inline Expected<NativeModuleHandle> openNativeParserModule(std::string_view path) {
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
  HMODULE module =
      LoadLibraryExW(wide_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (module == nullptr) {
    return unexpected("LoadLibraryExW failed (error " + std::to_string(GetLastError()) + ")");
  }
  return reinterpret_cast<NativeModuleHandle>(module);
#else
  void* handle = dlopen(std::string(path).c_str(), RTLD_LOCAL | RTLD_NOW);
  if (handle == nullptr) {
    const char* error = dlerror();
    return unexpected(error == nullptr ? "dlopen failed" : error);
  }
  return handle;
#endif
}

inline Expected<void*> resolveNativeParserModuleSymbol(NativeModuleHandle handle, const char* name) {
  if (handle == nullptr) {
    return unexpected("native parser module is not loaded");
  }
#if defined(_WIN32)
  FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
  if (symbol == nullptr) {
    return unexpected(std::string(name) + " not found");
  }
  return reinterpret_cast<void*>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, name);
  const char* error = dlerror();
  if (error != nullptr) {
    return unexpected(std::string(name) + " not found: " + error);
  }
  return symbol;
#endif
}

}  // namespace PJ::detail
