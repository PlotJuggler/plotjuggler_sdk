/**
 * @file widget_library.hpp
 * @brief Host-side loader for Widget plugin shared libraries.
 *
 * WidgetLibrary wraps dlopen and resolves PJ_get_widget_vtable. Instances are
 * made with createHandle(type).
 *
 * A Widget library is never unloaded: the loader keeps the DSO mapped for the
 * process lifetime (a widget is called by the GUI toolkit at times the host
 * does not control). Pointers obtained from vtable() / buildInfo() therefore
 * stay valid after every WidgetHandle and the WidgetLibrary are destroyed, and
 * the host needs no keep-alive store.
 *
 * Typical usage:
 * @code
 *   auto lib = PJ::WidgetLibrary::load("path/to/plugin.so");
 *   if (!lib) { handle error; }
 *   auto handle = lib->createHandle("gauge");
 * @endcode
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_plugins/widget_protocol.h>

#include <filesystem>
#include <memory>
#include <pj_plugins/host/widget_handle.hpp>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"

namespace PJ {

namespace detail {
struct LibraryPathIdentity;
}

/// Loads a Widget plugin shared library (never unloaded) and provides factory access. Move-only.
class WidgetLibrary {
 public:
  WidgetLibrary() = default;
  ~WidgetLibrary();

  WidgetLibrary(WidgetLibrary&& other) noexcept;
  WidgetLibrary& operator=(WidgetLibrary&& other) noexcept;

  WidgetLibrary(const WidgetLibrary&) = delete;
  WidgetLibrary& operator=(const WidgetLibrary&) = delete;

  [[nodiscard]] static Expected<WidgetLibrary> load(std::string_view path);

  [[nodiscard]] static Expected<WidgetLibrary> load(const char* path) {
    return load(std::string_view(path));
  }

  [[nodiscard]] static Expected<WidgetLibrary> load(const std::string& path) {
    return load(std::string_view(path));
  }

  [[nodiscard]] static Expected<WidgetLibrary> load(const std::filesystem::path& path);

  /// Validate and retain an already-open @p handle whose file is @p origin. No separate native module
  /// is opened; the handle is pinned so the module stays mapped for the process lifetime.
  [[nodiscard]] static Expected<WidgetLibrary> loadFromHandle(
      std::shared_ptr<void> handle, const std::filesystem::path& origin);

  /// Wrap a statically-linked plugin vtable (no dlopen; for WASM/static builds). @p vtable must have
  /// static storage duration.
  [[nodiscard]] static Expected<WidgetLibrary> loadStatic(const PJ_widget_vtable_t* vtable);

  [[nodiscard]] bool valid() const {
    return handle_ != nullptr && vtable_ != nullptr;
  }

  /// Raw vtable pointer. Valid for the process lifetime (the library is never unloaded).
  [[nodiscard]] const PJ_widget_vtable_t* vtable() const {
    return vtable_;
  }

  /// Readable without creating an instance.
  [[nodiscard]] const PJ_widget_build_info_t* buildInfo() const {
    return vtable_ != nullptr ? vtable_->build_info() : nullptr;
  }

  /// Create one independent instance of widget @p type. Fails when the plugin does not know the type.
  [[nodiscard]] Expected<WidgetHandle> createHandle(std::string_view type) const;

  /// "static://" for loadStatic.
  [[nodiscard]] std::string path() const {
    return path_;
  }

 private:
  [[nodiscard]] static Expected<WidgetLibrary> loadFromHandleWithIdentity(
      std::shared_ptr<void> handle, const detail::LibraryPathIdentity& origin);

  WidgetLibrary(std::shared_ptr<void> handle, const PJ_widget_vtable_t* vtable, std::string path);

  void reset();

  std::shared_ptr<void> handle_;
  const PJ_widget_vtable_t* vtable_ = nullptr;
  std::string path_;
};

}  // namespace PJ
