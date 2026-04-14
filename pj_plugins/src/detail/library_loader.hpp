#pragma once

#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "pj_base/expected.hpp"

namespace PJ::detail {

inline Expected<void*> loadLibraryHandle(std::string_view path) {
#if defined(_WIN32)
  HMODULE module = LoadLibraryA(std::string(path).c_str());
  if (module == nullptr) {
    return unexpected(std::string("LoadLibraryA failed"));
  }
  return reinterpret_cast<void*>(module);
#else
  // RTLD_DEEPBIND prevents symbol conflicts (e.g. Conan OpenSSL vs system libcrypto)
  // but is a glibc extension — not available on macOS or musl.
  // TODO: consider a Platform abstraction class (like pj_marketplace/PlatformUtils)
  //       to centralize OS-specific behavior.
  int flags = RTLD_NOW | RTLD_LOCAL;
#if defined(__linux__) && defined(RTLD_DEEPBIND)
  flags |= RTLD_DEEPBIND;
#endif
  void* handle = dlopen(std::string(path).c_str(), flags);
  if (handle == nullptr) {
    return unexpected(std::string(dlerror()));
  }
  return handle;
#endif
}

/// Resolve a named symbol from a loaded library handle.
inline Expected<void*> resolveSymbol(void* handle, const char* symbol_name) {
  if (handle == nullptr) {
    return unexpected(std::string("library not loaded"));
  }
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name);
  if (symbol == nullptr) {
    return unexpected(std::string(symbol_name) + " not found");
  }
  return reinterpret_cast<void*>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, symbol_name);
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(std::string(err));
  }
  return symbol;
#endif
}

inline void closeLibraryHandle(void* handle) {
  if (handle == nullptr) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

}  // namespace PJ::detail
