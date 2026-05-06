// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_plugins/host/dialog_library.hpp"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace PJ {
namespace {

Expected<void*> loadLibraryHandle(std::string_view path) {
#if defined(_WIN32)
  HMODULE module = LoadLibraryA(std::string(path).c_str());
  if (module == nullptr) {
    return unexpected("LoadLibraryA failed");
  }
  return reinterpret_cast<void*>(module);
#else
  void* handle = dlopen(std::string(path).c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* error = dlerror();
    return unexpected(error == nullptr ? "" : error);
  }
  return handle;
#endif
}

void closeLibraryHandle(void* handle) {
  if (handle == nullptr) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

std::shared_ptr<void> adoptLibraryHandle(void* handle) {
  return std::shared_ptr<void>(handle, [](void* loaded_handle) { closeLibraryHandle(loaded_handle); });
}

Expected<PJ_get_dialog_vtable_fn> loadEntryPoint(void* handle) {
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), "PJ_get_dialog_vtable");
  if (symbol == nullptr) {
    return unexpected("PJ_get_dialog_vtable not found");
  }
  return reinterpret_cast<PJ_get_dialog_vtable_fn>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, "PJ_get_dialog_vtable");
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(err);
  }
  return reinterpret_cast<PJ_get_dialog_vtable_fn>(symbol);
#endif
}

}  // namespace

DialogLibrary::DialogLibrary(std::shared_ptr<void> handle, const PJ_dialog_vtable_t* vtable, std::string path)
    : handle_(std::move(handle)), vtable_(vtable), path_(std::move(path)) {}

DialogLibrary::~DialogLibrary() {
  reset();
}

DialogLibrary::DialogLibrary(DialogLibrary&& other) noexcept
    : handle_(std::move(other.handle_)), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.vtable_ = nullptr;
}

DialogLibrary& DialogLibrary::operator=(DialogLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = std::move(other.handle_);
    vtable_ = other.vtable_;
    path_ = std::move(other.path_);
    other.vtable_ = nullptr;
  }
  return *this;
}

Expected<DialogLibrary> DialogLibrary::load(std::string_view path) {
  auto raw_handle = loadLibraryHandle(path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  auto handle = adoptLibraryHandle(*raw_handle);

  auto entry = loadEntryPoint(handle.get());
  if (!entry) {
    return unexpected(entry.error());
  }

  const PJ_dialog_vtable_t* vtable = (*entry)();
  if (vtable == nullptr) {
    return unexpected("PJ_get_dialog_vtable returned null");
  }
  if (vtable->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
    return unexpected("Dialog protocol version mismatch");
  }
  if (vtable->struct_size < PJ_DIALOG_MIN_VTABLE_SIZE) {
    return unexpected("Dialog vtable smaller than v4.0 baseline");
  }

  return DialogLibrary(std::move(handle), vtable, std::string(path));
}

void DialogLibrary::reset() {
  if (handle_ != nullptr) {
    handle_.reset();
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
