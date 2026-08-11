#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

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

inline Expected<NativeModuleHandle> openNativeParserModule(const std::filesystem::path& path) {
#if defined(_WIN32)
  HMODULE module =
      LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (module == nullptr) {
    return unexpected("LoadLibraryExW failed (error " + std::to_string(GetLastError()) + ")");
  }
  return reinterpret_cast<NativeModuleHandle>(module);
#else
  void* handle = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
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
