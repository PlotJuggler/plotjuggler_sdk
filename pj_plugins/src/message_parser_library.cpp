// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/message_parser_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"
#include "detail/vtable_validation.hpp"

namespace PJ {

MessageParserLibrary::MessageParserLibrary(
    std::shared_ptr<void> handle, const PJ_message_parser_vtable_t* vtable, std::string path,
    const PJ_dialog_vtable_t* static_dialog_vtable)
    : handle_(std::move(handle)),
      vtable_(vtable),
      static_dialog_vtable_(static_dialog_vtable),
      path_(std::move(path)) {}

MessageParserLibrary::~MessageParserLibrary() {
  reset();
}

MessageParserLibrary::MessageParserLibrary(MessageParserLibrary&& other) noexcept
    : handle_(std::move(other.handle_)),
      vtable_(other.vtable_),
      static_dialog_vtable_(other.static_dialog_vtable_),
      path_(std::move(other.path_)) {
  other.vtable_ = nullptr;
  other.static_dialog_vtable_ = nullptr;
}

MessageParserLibrary& MessageParserLibrary::operator=(MessageParserLibrary&& other) noexcept {
  if (this != &other) {
    reset();
    handle_ = std::move(other.handle_);
    vtable_ = other.vtable_;
    static_dialog_vtable_ = other.static_dialog_vtable_;
    path_ = std::move(other.path_);
    other.vtable_ = nullptr;
    other.static_dialog_vtable_ = nullptr;
  }
  return *this;
}

Expected<MessageParserLibrary> MessageParserLibrary::load(std::string_view path) {
  auto library = load(std::filesystem::path(path));
  if (library) {
    library->path_ = std::string(path);
  }
  return library;
}

Expected<MessageParserLibrary> MessageParserLibrary::load(const std::filesystem::path& path) {
  auto raw_handle = detail::loadLibraryHandle(path);
  if (!raw_handle) {
    return unexpected(raw_handle.error());
  }
  return loadFromHandle(detail::adoptLibraryHandle(*raw_handle), path);
}

Expected<MessageParserLibrary> MessageParserLibrary::loadFromHandle(
    std::shared_ptr<void> handle, const std::filesystem::path& origin) {
  if (handle == nullptr) {
    return unexpected("library not loaded");
  }
  if (auto abi = detail::checkPluginAbiVersion(handle.get(), origin); !abi) {
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(handle.get(), "PJ_get_message_parser_vtable", origin);
  if (!sym) {
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_message_parser_vtable_fn>(*sym);

  const PJ_message_parser_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    return unexpected("PJ_get_message_parser_vtable returned null");
  }
  if (vtable->protocol_version != PJ_MESSAGE_PARSER_PROTOCOL_VERSION) {
    return unexpected("MessageParser protocol version mismatch");
  }
  if (vtable->struct_size < PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE) {
    return unexpected("MessageParser vtable smaller than v4.0 baseline");
  }
  if (auto status = detail::validateRequiredSlots(vtable); !status) {
    return unexpected(status.error());
  }

  return MessageParserLibrary(std::move(handle), vtable, detail::pathForLegacyAccessor(origin));
}

Expected<MessageParserLibrary> MessageParserLibrary::loadStatic(
    const PJ_message_parser_vtable_t* vtable, const PJ_dialog_vtable_t* dialog_vtable) {
  if (vtable == nullptr) {
    return unexpected("static MessageParser vtable is null");
  }
  if (vtable->protocol_version != PJ_MESSAGE_PARSER_PROTOCOL_VERSION) {
    return unexpected("MessageParser protocol version mismatch");
  }
  if (vtable->struct_size < PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE) {
    return unexpected("MessageParser vtable smaller than v4.0 baseline");
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
  return MessageParserLibrary(std::move(handle), vtable, "static://", dialog_vtable);
}

Expected<const PJ_dialog_vtable_t*> MessageParserLibrary::resolveDialogVtable() const {
  if (static_dialog_vtable_ != nullptr) {
    return static_dialog_vtable_;
  }
  if (path_ == "static://") {
    return unexpected("static MessageParser has no registered dialog vtable");
  }
#if defined(_WIN32)
  auto sym = detail::resolveSymbol(handle_.get(), "PJ_get_dialog_vtable", {});
#else
  auto sym = detail::resolveSymbol(handle_.get(), "PJ_get_dialog_vtable", std::filesystem::path(path_));
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

void MessageParserLibrary::reset() {
  if (handle_ != nullptr) {
    handle_.reset();
    vtable_ = nullptr;
    static_dialog_vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
