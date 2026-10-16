// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/widget_library.hpp"

#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "detail/library_loader.hpp"
#include "detail/vtable_validation.hpp"

namespace PJ {

namespace {

// Size of PJ_widget_build_info_t as shipped in v1 (last field: qt_version_str). Like the vtable minimum,
// this never grows: a newer plugin reports a larger struct_size, an older one cannot exist.
constexpr size_t kMinBuildInfoSize = offsetof(PJ_widget_build_info_t, qt_version_str) + sizeof(PJ_string_view_t);
constexpr size_t kMaxBuildInfoTextBytes = 4096;

// build_info is constant plugin data, so it is validated once here and every later reader
// (WidgetLibrary::buildInfo, WidgetHandle::buildInfo) can trust its size and views.
Status checkBuildInfo(const PJ_widget_vtable_t* vtable) {
  const PJ_widget_build_info_t* info = vtable->build_info();
  if (info == nullptr) {
    return unexpected("Widget build_info returned null");
  }
  if (info->struct_size < kMinBuildInfoSize) {
    return unexpected("Widget build_info smaller than the v1 layout");
  }
  if (auto view = detail::checkedWidgetView(info->compiler, kMaxBuildInfoTextBytes, "build_info compiler"); !view) {
    return unexpected(view.error());
  }
  if (auto view = detail::checkedWidgetView(info->qt_version_str, kMaxBuildInfoTextBytes, "build_info version");
      !view) {
    return unexpected(view.error());
  }
  return okStatus();
}

Status checkVtable(const PJ_widget_vtable_t* vtable) {
  if (vtable->protocol_version != PJ_WIDGET_PLUGIN_PROTOCOL_VERSION) {
    return unexpected("Widget protocol version mismatch");
  }
  if (vtable->struct_size < PJ_WIDGET_MIN_VTABLE_SIZE) {
    return unexpected("Widget vtable smaller than v1.0 baseline");
  }
  if (auto slots = detail::validateRequiredSlots(vtable); !slots) {
    return slots;
  }
  return checkBuildInfo(vtable);
}

// A Widget library is never unloaded: Qt calls into the widget (events, repaint, destruction) at
// times the host does not control. Handles loaded by load() carry a no-op deleter; a handle supplied
// by the caller is pinned here with a copy that is intentionally never released, once it passed
// validation (a rejected library is not pinned, so the caller may still close it).
void pinForProcessLifetime(const std::shared_ptr<void>& handle) {
  static std::mutex mutex;
  static auto* pinned = new std::vector<std::shared_ptr<void>>();
  std::lock_guard<std::mutex> lock(mutex);
  pinned->push_back(handle);
}

}  // namespace

WidgetLibrary::WidgetLibrary(std::shared_ptr<void> handle, const PJ_widget_vtable_t* vtable, std::string path)
    : handle_(std::move(handle)), vtable_(vtable), path_(std::move(path)) {}

WidgetLibrary::~WidgetLibrary() {
  reset();
}

WidgetLibrary::WidgetLibrary(WidgetLibrary&& other) noexcept
    : handle_(std::move(other.handle_)), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.vtable_ = nullptr;
}

WidgetLibrary& WidgetLibrary::operator=(WidgetLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = std::move(other.handle_);
    vtable_ = other.vtable_;
    path_ = std::move(other.path_);
    other.vtable_ = nullptr;
  }
  return *this;
}

Expected<WidgetLibrary> WidgetLibrary::load(std::string_view path) {
  return load(std::filesystem::path(path));
}

Expected<WidgetLibrary> WidgetLibrary::load(const std::filesystem::path& path) {
  detail::LibraryPathIdentity recorded_path;
  auto raw_handle = detail::loadLibraryHandle(path, &recorded_path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  return loadFromHandleWithIdentity(detail::adoptLibraryHandleNonOwning(*raw_handle), recorded_path);
}

Expected<WidgetLibrary> WidgetLibrary::loadFromHandle(
    std::shared_ptr<void> handle, const std::filesystem::path& origin) {
  auto recorded_path = detail::recordLibraryPathIdentity(origin);
  if (!recorded_path) {
    return unexpected(recorded_path.error());
  }
  return loadFromHandleWithIdentity(std::move(handle), *recorded_path);
}

Expected<WidgetLibrary> WidgetLibrary::loadFromHandleWithIdentity(
    std::shared_ptr<void> handle, const detail::LibraryPathIdentity& recorded_path) {
  if (handle == nullptr) {
    return unexpected("library not loaded");
  }
  if (auto abi = detail::checkPluginAbiVersion(handle.get(), recorded_path); !abi) {
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(handle.get(), "PJ_get_widget_vtable", recorded_path);
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_widget_vtable_fn>(*sym);

  const PJ_widget_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    return unexpected("PJ_get_widget_vtable returned null");
  }
  if (auto status = checkVtable(vtable); !status) {
    return unexpected(status.error());
  }
  pinForProcessLifetime(handle);
  return WidgetLibrary(std::move(handle), vtable, detail::pathForLegacyAccessor(recorded_path.load_path));
}

Expected<WidgetLibrary> WidgetLibrary::loadStatic(const PJ_widget_vtable_t* vtable) {
  if (vtable == nullptr) {
    return unexpected("static Widget vtable is null");
  }
  if (auto status = checkVtable(vtable); !status) {
    return unexpected(status.error());
  }
  static char anchor = 0;
  std::shared_ptr<void> handle(&anchor, [](void*) {});
  return WidgetLibrary(std::move(handle), vtable, "static://");
}

Expected<WidgetHandle> WidgetLibrary::createHandle(std::string_view type) const {
  if (!valid()) {
    return unexpected("widget library not loaded");
  }
  WidgetHandle handle(vtable_, type, handle_);
  if (!handle.valid()) {
    return unexpected("widget plugin could not create type '" + std::string(type) + "'");
  }
  return handle;
}

void WidgetLibrary::reset() {
  if (handle_ != nullptr) {
    handle_.reset();
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
