// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/dialog_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"
#include "detail/vtable_validation.hpp"

namespace PJ {

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
  auto raw_handle = detail::loadLibraryHandle(path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  auto handle = detail::adoptLibraryHandle(*raw_handle);

  if (auto abi = detail::checkPluginAbiVersion(handle.get()); !abi) {
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(handle.get(), "PJ_get_dialog_vtable");
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_dialog_vtable_fn>(*sym);

  const PJ_dialog_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    return unexpected("PJ_get_dialog_vtable returned null");
  }
  if (vtable->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
    return unexpected("Dialog protocol version mismatch");
  }
  if (vtable->struct_size < PJ_DIALOG_MIN_VTABLE_SIZE) {
    return unexpected("Dialog vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vtable); !status) {
    return unexpected(status.error());
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
