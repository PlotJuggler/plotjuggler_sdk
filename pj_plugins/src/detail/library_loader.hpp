#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

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
#include "pj_base/plugin_data_api.h"

namespace PJ::detail {

/// Encode a native path for the legacy narrow `Library::path()` accessor.
inline std::string pathForLegacyAccessor(const std::filesystem::path& path) {
#if defined(_WIN32)
  const auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
#else
  return path.string();
#endif
}

/// The two immutable path spellings captured while a candidate still exists.
/// `load_path` is the exact normalized absolute argument passed to the native
/// loader. `resolved_path` is its best-effort weakly-canonical spelling.
struct LibraryPathIdentity {
  std::filesystem::path load_path;
  std::filesystem::path resolved_path;
};

/// Produce the normalized absolute spelling used for the native loader call.
/// This is deliberately lexical: loading does not require the candidate to
/// remain stat-able after the native module handle has been acquired.
inline Expected<std::filesystem::path> normalizedAbsoluteLibraryPath(const std::filesystem::path& path) {
  std::error_code path_error;
  std::filesystem::path absolute_path = std::filesystem::absolute(path, path_error);
  if (path_error) {
#if defined(_WIN32)
    return unexpected("cannot make library path absolute: " + path_error.message());
#else
    return unexpected("cannot make library path absolute '" + path.string() + "': " + path_error.message());
#endif
  }
  return absolute_path.lexically_normal();
}

inline Expected<LibraryPathIdentity> recordLibraryPathIdentity(const std::filesystem::path& path) {
  auto load_path = normalizedAbsoluteLibraryPath(path);
  if (!load_path) {
    return unexpected(load_path.error());
  }

  std::error_code canonical_error;
  std::filesystem::path resolved_path = std::filesystem::weakly_canonical(*load_path, canonical_error);
  if (canonical_error) {
    resolved_path.clear();
  }
  return LibraryPathIdentity{std::move(*load_path), std::move(resolved_path)};
}

inline Expected<void*> loadLibraryHandle(
    const std::filesystem::path& path, LibraryPathIdentity* recorded_path = nullptr) {
  auto identity = recordLibraryPathIdentity(path);
  if (!identity) {
    return unexpected(identity.error());
  }
#if defined(_WIN32)
  HMODULE module = LoadLibraryExW(
      identity->load_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (module == nullptr) {
    return unexpected("LoadLibraryExW failed (error " + std::to_string(GetLastError()) + ")");
  }
  if (recorded_path != nullptr) {
    *recorded_path = *identity;
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
  // uses two build-time mechanisms (cmake/PjPluginManifest.cmake):
  // 1. -fvisibility=hidden: hides symbols defined in plugin source files.
  // 2. -Wl,-Bsymbolic-functions (Linux): function calls within the .so
  //    resolve to the embedded static copies, bypassing PLT. This covers
  //    deps compiled without -fvisibility=hidden (e.g. libssl.a from Conan)
  //    whose symbols enter the .so with default visibility and whose calls
  //    would otherwise resolve to the host's namespace first via PLT.
  // malloc/pthread/system calls are NOT defined in the plugin so they still
  // reach the host — ASAN malloc interposition works correctly.
  int flags = RTLD_NOW | RTLD_LOCAL;
#if defined(__APPLE__)
  // Restrict handle-scoped lookups to the candidate image. Dylibs that rely on
  // -reexport_library no longer resolve through this handle; that stricter
  // admission behavior is intentional and provenance diagnostics stay explicit.
  flags |= RTLD_FIRST;
#endif
  void* handle = dlopen(identity->load_path.c_str(), flags);
  if (handle == nullptr) {
    const char* error = dlerror();
    return unexpected(error == nullptr ? "" : error);
  }
  if (recorded_path != nullptr) {
    *recorded_path = *identity;
  }
  return handle;
#endif
}

/// Return the filesystem object that defines @p symbol on POSIX platforms.
inline Expected<std::filesystem::path> symbolOwner(void* symbol) {
#if defined(_WIN32)
  (void)symbol;
  return std::filesystem::path{};
#else
  Dl_info info{};
  if (symbol == nullptr || dladdr(symbol, &info) == 0 || info.dli_fname == nullptr || info.dli_fname[0] == '\0') {
    return unexpected("dladdr failed to identify the defining object");
  }
  return std::filesystem::path(info.dli_fname);
#endif
}

/// Verify that @p symbol is defined by @p candidate_handle/path, not a
/// dependency. POSIX accepts an exact recorded loader-path match without any
/// filesystem access, then uses equivalent() only for different spellings.
inline Expected<void> verifySymbolProvenance(
    void* candidate_handle, void* symbol, const char* symbol_name, const LibraryPathIdentity& candidate_path) {
#if defined(_WIN32)
  HMODULE owner = nullptr;
  if (symbol == nullptr || GetModuleHandleExW(
                               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(symbol), &owner) == 0) {
    return unexpected(
        "cannot prove provenance for symbol '" + std::string(symbol_name) + "': GetModuleHandleExW failed (error " +
        std::to_string(GetLastError()) + ")");
  }
  const auto candidate = reinterpret_cast<HMODULE>(candidate_handle);
  if (owner != candidate) {
    auto module_path = [](HMODULE module) {
      std::wstring buffer(32768, L'\0');
      const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
      if (length == 0 || length >= static_cast<DWORD>(buffer.size())) {
        return std::string("<unknown module>");
      }
      buffer.resize(length);
      return pathForLegacyAccessor(std::filesystem::path(buffer));
    };
    const std::string candidate_name =
        candidate_path.load_path.empty() ? module_path(candidate) : pathForLegacyAccessor(candidate_path.load_path);
    return unexpected(
        "symbol '" + std::string(symbol_name) + "' resolved from dependency '" + module_path(owner) +
        "', not candidate '" + candidate_name + "'");
  }
  return {};
#else
  (void)candidate_handle;
  auto owner = symbolOwner(symbol);
  if (!owner) {
    return unexpected(
        "cannot prove provenance for symbol '" + std::string(symbol_name) + "' in candidate '" +
        candidate_path.load_path.string() + "': " + owner.error());
  }

  if (owner->native() == candidate_path.load_path.native() ||
      (!candidate_path.resolved_path.empty() && owner->native() == candidate_path.resolved_path.native())) {
    return {};
  }

  std::error_code equivalent_error;
  const bool equivalent = std::filesystem::equivalent(*owner, candidate_path.load_path, equivalent_error);
  if (equivalent_error) {
    return unexpected(
        "cannot prove provenance for symbol '" + std::string(symbol_name) + "': defining object '" + owner->string() +
        "', candidate '" + candidate_path.load_path.string() + "': " + equivalent_error.message());
  }
  if (!equivalent) {
    return unexpected(
        "symbol '" + std::string(symbol_name) + "' resolved from dependency '" + owner->string() +
        "', not candidate '" + candidate_path.load_path.string() + "'");
  }
  return {};
#endif
}

/// Resolve a named symbol and prove that it is defined by @p candidate_path.
inline Expected<void*> resolveSymbol(void* handle, const char* symbol_name, const LibraryPathIdentity& candidate_path) {
  if (handle == nullptr) {
    return unexpected("library not loaded");
  }
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name);
  if (symbol == nullptr) {
    std::string name(symbol_name);
    return unexpected(name + " not found");
  }
  void* resolved = reinterpret_cast<void*>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, symbol_name);
  const char* err = dlerror();
  if (err != nullptr) {
#if defined(__APPLE__)
    return unexpected(
        "cannot prove provenance for symbol '" + std::string(symbol_name) + "' in candidate '" +
        candidate_path.load_path.string() + "': RTLD_FIRST lookup failed: " + err);
#else
    return unexpected(err);
#endif
  }
  void* resolved = symbol;
#endif
  if (auto provenance = verifySymbolProvenance(handle, resolved, symbol_name, candidate_path); !provenance) {
    return unexpected(provenance.error());
  }
  return resolved;
}

/// Verify the plugin exports `pj_plugin_abi_version` and its value equals
/// PJ_ABI_VERSION. Must be called BEFORE the family vtable is fetched — the
/// vtable layout is only meaningful once the boot-level ABI matches.
inline Expected<void> checkPluginAbiVersion(void* handle, const LibraryPathIdentity& candidate_path) {
  auto sym = resolveSymbol(handle, "pj_plugin_abi_version", candidate_path);
  if (!sym) {
    return unexpected("plugin missing pj_plugin_abi_version symbol: " + sym.error());
  }
  const auto* plugin_abi = static_cast<const uint32_t*>(*sym);
  if (plugin_abi == nullptr || *plugin_abi != PJ_ABI_VERSION) {
    const std::string actual = plugin_abi == nullptr ? "null" : std::to_string(*plugin_abi);
    return unexpected(
        "plugin pj_plugin_abi_version mismatch (expected " + std::to_string(PJ_ABI_VERSION) + ", got " + actual + ")");
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

inline std::shared_ptr<void> adoptLibraryHandle(void* handle) {
  return std::shared_ptr<void>(handle, [](void* loaded_handle) { closeLibraryHandle(loaded_handle); });
}

/// Wrap an already-open library handle with a no-op deleter so process exit can
/// reclaim it after all SDK admission passes share the same native open.
inline std::shared_ptr<void> adoptLibraryHandleNonOwning(void* handle) {
  return std::shared_ptr<void>(handle, [](void*) {});
}

}  // namespace PJ::detail
