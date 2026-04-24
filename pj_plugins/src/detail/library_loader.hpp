#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"

namespace PJ::detail {

inline Expected<void*> loadLibraryHandle(std::string_view path) {
#if defined(_WIN32)
  // LOAD_WITH_ALTERED_SEARCH_PATH adds the directory of the loaded DLL to the
  // search path for resolving its dependencies — matches dlopen's default on
  // Linux. Without it, deps are only searched in the .exe directory, System32
  // and PATH, so plugins cannot ship their own sibling DLLs 
  HMODULE module = LoadLibraryExA(std::string(path).c_str(), nullptr,
                                  LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    return unexpected("LoadLibraryExA failed (error " +
                      std::to_string(GetLastError()) + ")");
  }
  return reinterpret_cast<void*>(module);
#else
  // RTLD_NOW  — resolve all symbols now; fail-fast on missing ones.
  // RTLD_LOCAL — keep plugin symbols out of the global symbol pool; each
  //              plugin resolves its own copies of bundled statics in
  //              isolation from other plugins and from the host.
  //
  // Historical note: we USED to also set RTLD_DEEPBIND on glibc to force
  // the plugin's own symbol scope ahead of the global one (Conan OpenSSL
  // vs system libcrypto, etc.). That flag is a documented trap — it
  // breaks LD_PRELOAD'd malloc interposition, which makes every plugin
  // dlopen fail under AddressSanitizer (and similarly for jemalloc /
  // tcmalloc interposition in production). Plugin-local symbol isolation
  // is instead achieved by building plugins with -fvisibility=hidden and
  // explicitly marking only the boot-level exports
  // (pj_plugin_abi_version + PJ_get_<family>_vtable) as default visible.
  // See cmake/PjPluginManifest.cmake for the plugin build flags.
  int flags = RTLD_NOW | RTLD_LOCAL;
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

/// Verify the plugin exports `pj_plugin_abi_version` and its value equals
/// PJ_ABI_VERSION. Must be called BEFORE the family vtable is fetched — the
/// vtable layout is only meaningful once the boot-level ABI matches.
inline Expected<void> checkPluginAbiVersion(void* handle) {
  auto sym = resolveSymbol(handle, "pj_plugin_abi_version");
  if (!sym) {
    return unexpected(std::string("plugin missing pj_plugin_abi_version symbol"));
  }
  const auto* plugin_abi = static_cast<const uint32_t*>(*sym);
  if (plugin_abi == nullptr || *plugin_abi != PJ_ABI_VERSION) {
    return unexpected(std::string("plugin pj_plugin_abi_version mismatch (expected 4)"));
  }
  return {};
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
