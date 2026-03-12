/**
 * @file message_parser_plugin_base.hpp
 * @brief C++ SDK for implementing MessageParser plugins.
 *
 * Plugin authors subclass MessageParserPluginBase, override parse(),
 * and export with the PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest) macro.
 * The SDK handles C ABI trampoline generation and exception safety.
 *
 * See pj_plugins/examples/mock_json_parser.cpp for a complete example.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {

/**
 * Base class for MessageParser plugins.
 *
 * Subclass and override the pure-virtual parse() method. Optionally override
 * bindSchema, saveConfig/loadConfig for richer behaviour.
 *
 * Use writeHost() (protected) to write decoded fields during parse().
 * Export with PJ_MESSAGE_PARSER_PLUGIN(YourClass, manifest).
 *
 * The base class generates C ABI trampolines with full exception safety —
 * any exception thrown from a virtual is caught, stored via setLastError(),
 * and converted to a false/null return across the ABI boundary.
 */
class MessageParserPluginBase {
 public:
  virtual ~MessageParserPluginBase() = default;

  /// Bind the data-plane write host. Override only if you need custom validation.
  virtual Status bindWriteHost(PJ_parser_write_host_t write_host) {
    if (write_host.ctx == nullptr || write_host.vtable == nullptr) {
      return unexpected("write host is not bound");
    }
    write_host_ = write_host;
    return okStatus();
  }

  /// Bind a message schema. Default is no-op (for parsers that don't need schema).
  virtual Status bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    (void)type_name;
    (void)schema;
    return okStatus();
  }

  /// Serialize plugin configuration to JSON. Default returns "{}".
  virtual std::string saveConfig() const { return "{}"; }

  /// Restore plugin configuration from JSON. Default accepts any input.
  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Parse one raw message and write decoded fields via writeHost(). PURE VIRTUAL.
  virtual Status parse(Timestamp timestamp_ns, Span<const uint8_t> payload) = 0;

  /// Return the last error message. Override for custom error reporting.
  virtual std::string lastError() const { return last_error_; }

  template <typename CreateFn>
  static const PJ_message_parser_vtable_t* vtableWithCreate(
      CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr,
              "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr,
              "manifest must contain a \"version\" key");
    PJ_ASSERT(std::strstr(manifest, "\"encoding\"") != nullptr,
              "manifest must contain an \"encoding\" key");
    static const PJ_message_parser_vtable_t vt = {
        PJ_MESSAGE_PARSER_PROTOCOL_VERSION,
        sizeof(PJ_message_parser_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_bind_write_host,
        trampoline_bind_schema,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_parse,
        trampoline_get_last_error,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] bool writeHostBound() const {
    return write_host_.ctx != nullptr && write_host_.vtable != nullptr;
  }

  [[nodiscard]] sdk::ParserWriteHostView writeHost() const {
    return sdk::ParserWriteHostView(write_host_);
  }

  void setLastError(std::string error) { last_error_ = std::move(error); }

 private:
  PJ_parser_write_host_t write_host_{};
  std::string config_buf_;
  mutable std::string last_error_;

  // C ABI trampolines — exception-safe bridges between host vtable calls and
  // C++ virtuals. Implementations live in detail/message_parser_trampolines.hpp.
  static void trampoline_destroy(void* ctx);
  static bool trampoline_bind_write_host(void* ctx, PJ_parser_write_host_t write_host);
  static bool trampoline_bind_schema(void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema);
  static const char* trampoline_save_config(void* ctx);
  static bool trampoline_load_config(void* ctx, const char* config_json);
  static bool trampoline_parse(void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload);
  static const char* trampoline_get_last_error(void* ctx);
};

}  // namespace PJ

// Out-of-line trampoline definitions — separated to keep the public API header concise.
#include "pj_base/sdk/detail/message_parser_trampolines.hpp"

/**
 * Export a MessageParserPluginBase subclass as a shared-library plugin.
 *
 * Place at file scope (after the class definition). Generates the extern "C"
 * entry point `PJ_get_message_parser_vtable` that the host resolves via dlsym.
 *
 * @param ClassName   The MessageParserPluginBase subclass to instantiate.
 * @param manifest    A string literal containing the JSON manifest
 *                    (must have "name", "version", and "encoding" keys).
 *
 * Usage:
 * @code
 *   PJ_MESSAGE_PARSER_PLUGIN(MyParser, R"({"name":"My Parser","version":"1.0.0","encoding":"json"})")
 * @endcode
 */
#define PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest)                                                       \
  extern "C" PJ_MESSAGE_PARSER_EXPORT const PJ_message_parser_vtable_t* PJ_get_message_parser_vtable() {    \
    static const PJ_message_parser_vtable_t* vt =                                                           \
        PJ::MessageParserPluginBase::vtableWithCreate(                                                      \
            []() -> void* { return new ClassName(); }, manifest);                                            \
    return vt;                                                                                              \
  }
