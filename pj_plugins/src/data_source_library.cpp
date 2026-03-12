#include "pj_plugins/host/data_source_library.hpp"

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
    return unexpected(std::string("LoadLibraryA failed"));
  }
  return reinterpret_cast<void*>(module);
#else
  void* handle = dlopen(std::string(path).c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    return unexpected(std::string(dlerror()));
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

Expected<PJ_get_data_source_vtable_fn> loadEntryPoint(void* handle) {
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), "PJ_get_data_source_vtable");
  if (symbol == nullptr) {
    return unexpected(std::string("PJ_get_data_source_vtable not found"));
  }
  return reinterpret_cast<PJ_get_data_source_vtable_fn>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, "PJ_get_data_source_vtable");
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(std::string(err));
  }
  return reinterpret_cast<PJ_get_data_source_vtable_fn>(symbol);
#endif
}

}  // namespace

DataSourceLibrary::DataSourceLibrary(
    void* handle, const PJ_data_source_vtable_t* vtable, std::string path)
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
  auto handle = loadLibraryHandle(path);
  if (!handle) {
    return unexpected(handle.error());
  }

  auto entry = loadEntryPoint(*handle);
  if (!entry) {
    closeLibraryHandle(*handle);
    return unexpected(entry.error());
  }

  const PJ_data_source_vtable_t* vtable = (*entry)();
  if (vtable == nullptr) {
    closeLibraryHandle(*handle);
    return unexpected(std::string("PJ_get_data_source_vtable returned null"));
  }
  if (vtable->protocol_version != PJ_DATA_SOURCE_PROTOCOL_VERSION) {
    closeLibraryHandle(*handle);
    return unexpected(std::string("DataSource protocol version mismatch"));
  }
  if (vtable->struct_size < sizeof(PJ_data_source_vtable_t)) {
    closeLibraryHandle(*handle);
    return unexpected(std::string("DataSource vtable is smaller than expected"));
  }

  return DataSourceLibrary(*handle, vtable, std::string(path));
}

Expected<const PJ_dialog_vtable_t*> DataSourceLibrary::resolveDialogVtable() const {
  if (handle_ == nullptr) {
    return unexpected(std::string("library not loaded"));
  }

#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle_), "PJ_get_dialog_vtable");
  if (symbol == nullptr) {
    return unexpected(std::string("PJ_get_dialog_vtable not found"));
  }
  auto fn = reinterpret_cast<PJ_get_dialog_vtable_fn>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle_, "PJ_get_dialog_vtable");
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(std::string(err));
  }
  auto fn = reinterpret_cast<PJ_get_dialog_vtable_fn>(symbol);
#endif

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
    closeLibraryHandle(handle_);
    handle_ = nullptr;
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
