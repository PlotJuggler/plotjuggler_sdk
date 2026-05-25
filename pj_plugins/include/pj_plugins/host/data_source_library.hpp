/**
 * @file data_source_library.hpp
 * @brief Host-side loader for DataSource plugin shared libraries.
 *
 * DataSourceLibrary wraps dlopen and resolves the plugin's vtable entry point.
 * Create instances of the plugin via createHandle(); those handles keep the
 * shared library loaded until their plugin instances are destroyed.
 *
 * Typical usage:
 * @code
 *   auto lib = PJ::DataSourceLibrary::load("path/to/plugin.so");
 *   if (!lib) { handle error; }
 *   auto handle = lib->createHandle();
 * @endcode
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_base/data_source_protocol.h>
#include <pj_plugins/dialog_protocol.h>

#include <memory>
#include <pj_plugins/host/data_source_handle.hpp>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"

namespace PJ {

/**
 * Loads a DataSource plugin shared library and provides factory access.
 *
 * The library is dlopen'd with RTLD_LOCAL on load(). Plugin handles created
 * from this loader keep the DSO loaded even if the DataSourceLibrary object is
 * destroyed or moved out of a runtime catalog. Move-only; not copyable.
 */
class DataSourceLibrary {
 public:
  DataSourceLibrary() = default;
  ~DataSourceLibrary();

  DataSourceLibrary(DataSourceLibrary&& other) noexcept;
  DataSourceLibrary& operator=(DataSourceLibrary&& other) noexcept;

  DataSourceLibrary(const DataSourceLibrary&) = delete;
  DataSourceLibrary& operator=(const DataSourceLibrary&) = delete;

  /// Load a plugin from @p path. Returns an error string on failure.
  [[nodiscard]] static Expected<DataSourceLibrary> load(std::string_view path);

  /// True if the library was loaded and the vtable resolved successfully.
  [[nodiscard]] bool valid() const {
    return handle_ != nullptr && vtable_ != nullptr;
  }

  /// Raw vtable pointer. Valid while this library or any handle created from it is alive.
  [[nodiscard]] const PJ_data_source_vtable_t* vtable() const {
    return vtable_;
  }

  /// Create a new plugin instance. Each handle is independent.
  [[nodiscard]] DataSourceHandle createHandle() const {
    return DataSourceHandle(vtable_, handle_);
  }

  /// Resolve the dialog vtable from this .so. Returns error if not exported.
  [[nodiscard]] Expected<const PJ_dialog_vtable_t*> resolveDialogVtable() const;

  /// Filesystem path the library was loaded from.
  [[nodiscard]] std::string path() const {
    return path_;
  }

 private:
  DataSourceLibrary(std::shared_ptr<void> handle, const PJ_data_source_vtable_t* vtable, std::string path);

  void reset();

  std::shared_ptr<void> handle_;
  const PJ_data_source_vtable_t* vtable_ = nullptr;
  std::string path_;
};

}  // namespace PJ
