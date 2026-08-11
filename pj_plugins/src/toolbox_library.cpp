// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/toolbox_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"
#include "detail/vtable_validation.hpp"

namespace PJ {

ToolboxLibrary::ToolboxLibrary(
    std::shared_ptr<void> handle, const PJ_toolbox_vtable_t* vtable, std::string path, std::string resolved_path,
    const PJ_dialog_vtable_t* static_dialog_vtable)
    : handle_(std::move(handle)),
      vtable_(vtable),
      static_dialog_vtable_(static_dialog_vtable),
      path_(std::move(path)),
      resolved_path_(std::move(resolved_path)) {}

ToolboxLibrary::~ToolboxLibrary() {
  reset();
}

ToolboxLibrary::ToolboxLibrary(ToolboxLibrary&& other) noexcept
    : handle_(std::move(other.handle_)),
      vtable_(other.vtable_),
      static_dialog_vtable_(other.static_dialog_vtable_),
      path_(std::move(other.path_)),
      resolved_path_(std::move(other.resolved_path_)) {
  other.vtable_ = nullptr;
  other.static_dialog_vtable_ = nullptr;
}

ToolboxLibrary& ToolboxLibrary::operator=(ToolboxLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = std::move(other.handle_);
    vtable_ = other.vtable_;
    static_dialog_vtable_ = other.static_dialog_vtable_;
    path_ = std::move(other.path_);
    resolved_path_ = std::move(other.resolved_path_);
    other.vtable_ = nullptr;
    other.static_dialog_vtable_ = nullptr;
  }
  return *this;
}

Expected<ToolboxLibrary> ToolboxLibrary::load(std::string_view path) {
  return load(std::filesystem::path(path));
}

Expected<ToolboxLibrary> ToolboxLibrary::load(const std::filesystem::path& path) {
  detail::LibraryPathIdentity recorded_path;
  auto raw_handle = detail::loadLibraryHandle(path, &recorded_path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  return loadFromHandleWithIdentity(detail::adoptLibraryHandle(*raw_handle), recorded_path);
}

Expected<ToolboxLibrary> ToolboxLibrary::loadFromHandle(
    std::shared_ptr<void> handle, const std::filesystem::path& origin) {
  auto recorded_path = detail::recordLibraryPathIdentity(origin);
  if (!recorded_path) {
    return unexpected(recorded_path.error());
  }
  return loadFromHandleWithIdentity(std::move(handle), *recorded_path);
}

Expected<ToolboxLibrary> ToolboxLibrary::loadFromHandleWithIdentity(
    std::shared_ptr<void> handle, const detail::LibraryPathIdentity& recorded_path) {
  if (handle == nullptr) {
    return unexpected("library not loaded");
  }
  if (auto abi = detail::checkPluginAbiVersion(handle.get(), recorded_path); !abi) {
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(handle.get(), "PJ_get_toolbox_vtable", recorded_path);
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_toolbox_vtable_fn>(*sym);

  const PJ_toolbox_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    return unexpected("PJ_get_toolbox_vtable returned null");
  }
  if (vtable->protocol_version != PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION) {
    return unexpected("Toolbox protocol version mismatch");
  }
  if (vtable->struct_size < PJ_TOOLBOX_MIN_VTABLE_SIZE) {
    return unexpected("Toolbox vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vtable); !status) {
    return unexpected(status.error());
  }

  return ToolboxLibrary(
      std::move(handle), vtable, detail::pathForLegacyAccessor(recorded_path.load_path),
      detail::pathForLegacyAccessor(recorded_path.resolved_path));
}

Expected<ToolboxLibrary> ToolboxLibrary::loadStatic(
    const PJ_toolbox_vtable_t* vtable, const PJ_dialog_vtable_t* dialog_vtable) {
  if (vtable == nullptr) {
    return unexpected("static Toolbox vtable is null");
  }
  if (vtable->protocol_version != PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION) {
    return unexpected("Toolbox protocol version mismatch");
  }
  if (vtable->struct_size < PJ_TOOLBOX_MIN_VTABLE_SIZE) {
    return unexpected("Toolbox vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vtable); !status) {
    return unexpected(status.error());
  }
  if (dialog_vtable != nullptr) {
    if (dialog_vtable->protocol_version != PJ_DIALOG_PROTOCOL_VERSION) {
      return unexpected("Dialog protocol version mismatch");
    }
    if (dialog_vtable->struct_size < PJ_DIALOG_MIN_VTABLE_SIZE) {
      return unexpected("Dialog vtable smaller than v4.0 baseline");
    }
    if (auto status = detail::validateRequiredSlots(dialog_vtable); !status) {
      return unexpected(status.error());
    }
  }
  static char anchor = 0;
  std::shared_ptr<void> handle(&anchor, [](void*) {});
  return ToolboxLibrary(std::move(handle), vtable, "static://", "", dialog_vtable);
}

Expected<const PJ_dialog_vtable_t*> ToolboxLibrary::resolveDialogVtable() const {
  if (static_dialog_vtable_ != nullptr) {
    return static_dialog_vtable_;
  }
  if (path_ == "static://") {
    return unexpected("static Toolbox has no registered dialog vtable");
  }
#if defined(_WIN32)
  auto sym = detail::resolveSymbol(handle_.get(), "PJ_get_dialog_vtable", {});
#else
  auto sym = detail::resolveSymbol(
      handle_.get(), "PJ_get_dialog_vtable", {std::filesystem::path(path_), std::filesystem::path(resolved_path_)});
#endif
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

void ToolboxLibrary::reset() {
  if (handle_ != nullptr) {
    handle_.reset();
    vtable_ = nullptr;
    static_dialog_vtable_ = nullptr;
    path_.clear();
    resolved_path_.clear();
  }
}

}  // namespace PJ
