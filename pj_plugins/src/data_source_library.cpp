#include "pj_plugins/host/data_source_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"

namespace PJ {

DataSourceLibrary::DataSourceLibrary(void* handle, const PJ_data_source_vtable_t* vtable, std::string path)
    : handle_(handle), vtable_(vtable), path_(std::move(path)) {}

DataSourceLibrary::~DataSourceLibrary() {
  reset();
}

DataSourceLibrary::DataSourceLibrary(DataSourceLibrary&& other) noexcept
    : handle_(other.handle_), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.handle_ = nullptr;
  other.vtable_ = nullptr;
}

DataSourceLibrary& DataSourceLibrary::operator=(DataSourceLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = other.handle_;
    vtable_ = other.vtable_;
    path_ = std::move(other.path_);
    other.handle_ = nullptr;
    other.vtable_ = nullptr;
  }
  return *this;
}

Expected<DataSourceLibrary> DataSourceLibrary::load(std::string_view path) {
  auto handle = detail::loadLibraryHandle(path);
  if (!handle) {
    return unexpected(handle.error());
  }

  auto sym = detail::resolveSymbol(*handle, "PJ_get_data_source_vtable");
  if (!sym) {
    detail::closeLibraryHandle(*handle);
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_data_source_vtable_fn>(*sym);

  const PJ_data_source_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("PJ_get_data_source_vtable returned null"));
  }
  if (vtable->protocol_version != PJ_DATA_SOURCE_PROTOCOL_VERSION) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("DataSource protocol version mismatch"));
  }
  if (vtable->struct_size < sizeof(PJ_data_source_vtable_t)) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("DataSource vtable is smaller than expected"));
  }

  return DataSourceLibrary(*handle, vtable, std::string(path));
}

Expected<const PJ_dialog_vtable_t*> DataSourceLibrary::resolveDialogVtable() const {
  auto sym = detail::resolveSymbol(handle_, "PJ_get_dialog_vtable");
  if (!sym) {
    return unexpected(sym.error());
  }
  auto fn = reinterpret_cast<PJ_get_dialog_vtable_fn>(*sym);
  const PJ_dialog_vtable_t* vt = fn();
  if (vt == nullptr) {
    return unexpected(std::string("PJ_get_dialog_vtable returned null"));
  }
  if (vt->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
    return unexpected(std::string("Dialog protocol version mismatch"));
  }
  if (vt->struct_size < sizeof(PJ_dialog_vtable_t)) {
    return unexpected(std::string("Dialog vtable is smaller than expected"));
  }
  return vt;
}

void DataSourceLibrary::reset() {
  if (handle_ != nullptr) {
    detail::closeLibraryHandle(handle_);
    handle_ = nullptr;
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
