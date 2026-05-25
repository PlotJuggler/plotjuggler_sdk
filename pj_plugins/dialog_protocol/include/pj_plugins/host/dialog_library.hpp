#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_plugins/dialog_protocol.h>

#include <memory>
#include <pj_plugins/host/dialog_handle.hpp>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"

namespace PJ {

/// Loads a standalone dialog plugin shared library and provides factory access.
///
/// The library is dlopen'd with RTLD_LOCAL on load(). Dialog handles created
/// from this loader keep the DSO loaded even if the DialogLibrary object is
/// destroyed. Move-only; not copyable.
class DialogLibrary {
 public:
  DialogLibrary() = default;
  ~DialogLibrary();

  DialogLibrary(DialogLibrary&& other) noexcept;
  DialogLibrary& operator=(DialogLibrary&& other) noexcept;

  DialogLibrary(const DialogLibrary&) = delete;
  DialogLibrary& operator=(const DialogLibrary&) = delete;

  /// Load a dialog plugin from @p path. Returns an error string on failure.
  [[nodiscard]] static Expected<DialogLibrary> load(std::string_view path);

  /// True if the library was loaded and the vtable resolved successfully.
  [[nodiscard]] bool valid() const {
    return handle_ != nullptr && vtable_ != nullptr;
  }

  /// Raw vtable pointer. Valid while this library or any handle created from it is alive.
  [[nodiscard]] const PJ_dialog_vtable_t* vtable() const {
    return vtable_;
  }

  /// Create a new owning plugin instance.
  [[nodiscard]] DialogHandle createHandle() const {
    return DialogHandle(vtable_, handle_);
  }

  /// Filesystem path the library was loaded from.
  [[nodiscard]] std::string path() const {
    return path_;
  }

 private:
  DialogLibrary(std::shared_ptr<void> handle, const PJ_dialog_vtable_t* vtable, std::string path);

  void reset();

  std::shared_ptr<void> handle_;
  const PJ_dialog_vtable_t* vtable_ = nullptr;
  std::string path_;
};

}  // namespace PJ
