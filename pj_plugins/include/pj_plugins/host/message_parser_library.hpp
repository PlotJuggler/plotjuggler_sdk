/**
 * @file message_parser_library.hpp
 * @brief Host-side loader for MessageParser plugin shared libraries.
 *
 * MessageParserLibrary wraps dlopen/dlclose and resolves the plugin's vtable
 * entry point. Create instances of the plugin via createHandle().
 *
 * Typical usage:
 * @code
 *   auto lib = PJ::MessageParserLibrary::load("path/to/plugin.so");
 *   if (!lib) { handle error; }
 *   auto handle = lib->createHandle();
 * @endcode
 */
#pragma once

#include <pj_base/message_parser_protocol.h>
#include <pj_plugins/dialog_protocol.h>
#include <pj_plugins/host/message_parser_handle.hpp>

#include <string>
#include <string_view>

#include "pj_base/expected.hpp"

namespace PJ {

/**
 * Loads a MessageParser plugin shared library and provides factory access.
 *
 * The library is dlopen'd with RTLD_LOCAL on load() and dlclose'd on
 * destruction. The vtable pointer remains valid for the library's lifetime.
 * Move-only; not copyable.
 */
class MessageParserLibrary {
 public:
  MessageParserLibrary() = default;
  ~MessageParserLibrary();

  MessageParserLibrary(MessageParserLibrary&& other) noexcept;
  MessageParserLibrary& operator=(MessageParserLibrary&& other) noexcept;

  MessageParserLibrary(const MessageParserLibrary&) = delete;
  MessageParserLibrary& operator=(const MessageParserLibrary&) = delete;

  /// Load a plugin from @p path. Returns an error string on failure.
  [[nodiscard]] static Expected<MessageParserLibrary> load(std::string_view path);

  /// True if the library was loaded and the vtable resolved successfully.
  [[nodiscard]] bool valid() const { return handle_ != nullptr && vtable_ != nullptr; }

  /// Raw vtable pointer. Valid for the lifetime of this MessageParserLibrary.
  [[nodiscard]] const PJ_message_parser_vtable_t* vtable() const { return vtable_; }

  /// Create a new plugin instance. Each handle is independent.
  [[nodiscard]] MessageParserHandle createHandle() const {
    return MessageParserHandle(vtable_);
  }

  /// Resolve the dialog vtable from this .so. Returns error if not exported.
  [[nodiscard]] Expected<const PJ_dialog_vtable_t*> resolveDialogVtable() const;

  /// Filesystem path the library was loaded from.
  [[nodiscard]] std::string path() const { return path_; }

 private:
  MessageParserLibrary(void* handle, const PJ_message_parser_vtable_t* vtable, std::string path);

  void reset();

  void* handle_ = nullptr;
  const PJ_message_parser_vtable_t* vtable_ = nullptr;
  std::string path_;
};

}  // namespace PJ
