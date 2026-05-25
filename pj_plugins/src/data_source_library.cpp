// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/data_source_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"
#include "detail/vtable_validation.hpp"

namespace PJ {

DataSourceLibrary::DataSourceLibrary(
    std::shared_ptr<void> handle, const PJ_data_source_vtable_t* vtable, std::string path)
    : handle_(std::move(handle)), vtable_(vtable), path_(std::move(path)) {}

DataSourceLibrary::~DataSourceLibrary() {
  reset();
}

DataSourceLibrary::DataSourceLibrary(DataSourceLibrary&& other) noexcept
    : handle_(std::move(other.handle_)), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.vtable_ = nullptr;
}

DataSourceLibrary& DataSourceLibrary::operator=(DataSourceLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = std::move(other.handle_);
    vtable_ = other.vtable_;
    path_ = std::move(other.path_);
    other.vtable_ = nullptr;
  }
  return *this;
}

Expected<DataSourceLibrary> DataSourceLibrary::load(std::string_view path) {
  auto raw_handle = detail::loadLibraryHandle(path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  auto handle = detail::adoptLibraryHandle(*raw_handle);

  if (auto abi = detail::checkPluginAbiVersion(handle.get()); !abi) {
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(handle.get(), "PJ_get_data_source_vtable");
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_data_source_vtable_fn>(*sym);

  const PJ_data_source_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    return unexpected("PJ_get_data_source_vtable returned null");
  }
  if (vtable->protocol_version != PJ_DATA_SOURCE_PROTOCOL_VERSION) {
    return unexpected("DataSource protocol version mismatch");
  }
  // Use MIN_VTABLE_SIZE (pinned at v4.0), NOT sizeof() which grows per host
  // release and would falsely reject plugins compiled against older headers.
  if (vtable->struct_size < PJ_DATA_SOURCE_MIN_VTABLE_SIZE) {
    return unexpected("DataSource vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vtable); !status) {
    return unexpected(status.error());
  }

  return DataSourceLibrary(std::move(handle), vtable, std::string(path));
}

Expected<const PJ_dialog_vtable_t*> DataSourceLibrary::resolveDialogVtable() const {
  auto sym = detail::resolveSymbol(handle_.get(), "PJ_get_dialog_vtable");
  if (!sym) {
    return unexpected(sym.error());
  }
  auto fn = reinterpret_cast<PJ_get_dialog_vtable_fn>(*sym);
  const PJ_dialog_vtable_t* vt = fn();
  if (vt == nullptr) {
    return unexpected("PJ_get_dialog_vtable returned null");
  }
  if (vt->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
    return unexpected("Dialog protocol version mismatch");
  }
  if (vt->struct_size < PJ_DIALOG_MIN_VTABLE_SIZE) {
    return unexpected("Dialog vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vt); !status) {
    return unexpected(status.error());
  }
  return vt;
}

void DataSourceLibrary::reset() {
  if (handle_ != nullptr) {
    handle_.reset();
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
