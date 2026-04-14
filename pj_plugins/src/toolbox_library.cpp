#include "pj_plugins/host/toolbox_library.hpp"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "detail/library_loader.hpp"

namespace PJ {
namespace {

Expected<PJ_get_toolbox_vtable_fn> loadEntryPoint(void* handle) {
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), "PJ_get_toolbox_vtable");
  if (symbol == nullptr) {
    return unexpected(std::string("PJ_get_toolbox_vtable not found"));
  }
  return reinterpret_cast<PJ_get_toolbox_vtable_fn>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, "PJ_get_toolbox_vtable");
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(std::string(err));
  }
  return reinterpret_cast<PJ_get_toolbox_vtable_fn>(symbol);
#endif
}

}  // namespace

ToolboxLibrary::ToolboxLibrary(void* handle, const PJ_toolbox_vtable_t* vtable, std::string path)
    : handle_(handle), vtable_(vtable), path_(std::move(path)) {}

ToolboxLibrary::~ToolboxLibrary() {
  reset();
}

ToolboxLibrary::ToolboxLibrary(ToolboxLibrary&& other) noexcept
    : handle_(other.handle_), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.handle_ = nullptr;
  other.vtable_ = nullptr;
}

ToolboxLibrary& ToolboxLibrary::operator=(ToolboxLibrary&& other) noexcept {
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

Expected<ToolboxLibrary> ToolboxLibrary::load(std::string_view path) {
  auto handle = detail::loadLibraryHandle(path);
  if (!handle) {
    return unexpected(handle.error());
  }

  auto entry = loadEntryPoint(*handle);
  if (!entry) {
    detail::closeLibraryHandle(*handle);
    return unexpected(entry.error());
  }

  const PJ_toolbox_vtable_t* vtable = (*entry)();
  if (vtable == nullptr) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("PJ_get_toolbox_vtable returned null"));
  }
  if (vtable->protocol_version != PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("Toolbox protocol version mismatch"));
  }
  if (vtable->struct_size < sizeof(PJ_toolbox_vtable_t)) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("Toolbox vtable is smaller than expected"));
  }

  return ToolboxLibrary(*handle, vtable, std::string(path));
}

Expected<const PJ_dialog_vtable_t*> ToolboxLibrary::resolveDialogVtable() const {
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

void ToolboxLibrary::reset() {
  if (handle_ != nullptr) {
    detail::closeLibraryHandle(handle_);
    handle_ = nullptr;
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
