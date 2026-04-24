#include "pj_plugins/host/message_parser_library.hpp"

#include <utility>

#include "detail/library_loader.hpp"

namespace PJ {

MessageParserLibrary::MessageParserLibrary(void* handle, const PJ_message_parser_vtable_t* vtable, std::string path)
    : handle_(handle), vtable_(vtable), path_(std::move(path)) {}

MessageParserLibrary::~MessageParserLibrary() {
  reset();
}

MessageParserLibrary::MessageParserLibrary(MessageParserLibrary&& other) noexcept
    : handle_(other.handle_), vtable_(other.vtable_), path_(std::move(other.path_)) {
  other.handle_ = nullptr;
  other.vtable_ = nullptr;
}

MessageParserLibrary& MessageParserLibrary::operator=(MessageParserLibrary&& other) noexcept {
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

Expected<MessageParserLibrary> MessageParserLibrary::load(std::string_view path) {
  auto handle = detail::loadLibraryHandle(path);
  if (!handle) {
    return unexpected(handle.error());
  }

  if (auto abi = detail::checkPluginAbiVersion(*handle); !abi) {
    detail::closeLibraryHandle(*handle);
    return unexpected(abi.error());
  }

  auto sym = detail::resolveSymbol(*handle, "PJ_get_message_parser_vtable");
  if (!sym) {
    detail::closeLibraryHandle(*handle);
    return unexpected(sym.error());
  }
  auto entry = reinterpret_cast<PJ_get_message_parser_vtable_fn>(*sym);

  const PJ_message_parser_vtable_t* vtable = entry();
  if (vtable == nullptr) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("PJ_get_message_parser_vtable returned null"));
  }
  if (vtable->protocol_version != PJ_MESSAGE_PARSER_PROTOCOL_VERSION) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("MessageParser protocol version mismatch"));
  }
  if (vtable->struct_size < PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE) {
    detail::closeLibraryHandle(*handle);
    return unexpected(std::string("MessageParser vtable smaller than v3.0 baseline"));
  }

  return MessageParserLibrary(*handle, vtable, std::string(path));
}

Expected<const PJ_dialog_vtable_t*> MessageParserLibrary::resolveDialogVtable() const {
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

void MessageParserLibrary::reset() {
  if (handle_ != nullptr) {
    detail::closeLibraryHandle(handle_);
    handle_ = nullptr;
    vtable_ = nullptr;
    path_.clear();
  }
}

}  // namespace PJ
