/**
 * @file message_parser_plugin_base.hpp
 * @brief C++ SDK for implementing MessageParser plugins (protocol v4).
 *
 * Plugin authors subclass MessageParserPluginBase, override `parse()`, and
 * export with PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest).
 *
 * The default `bind()` implementation acquires the parser write host from
 * the service registry. Override to additionally acquire optional services.
 * All trampolines are noexcept at the ABI boundary.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/plugin_abi_export.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/sdk/service_traits.hpp"

namespace PJ {

/**
 * Base class for MessageParser plugins (protocol v4).
 */
class MessageParserPluginBase {
 public:
  virtual ~MessageParserPluginBase() = default;

  /// Acquire host-provided services.
  ///
  /// Default implementation pulls:
  ///   - "pj.parser_write.v1"        → ParserWriteHost       (mandatory)
  ///   - "pj.parser_object_write.v1" → ObjectWriteHost       (optional)
  ///
  /// A media-capable parser checks `objectWriteHost()` inside parse() and
  /// writes the scalar portion of the message to `writeHost()` and the
  /// media payload to `objectWriteHost()` from a single parse() call.
  virtual Status bind(sdk::ServiceRegistry services) {
    auto write = services.require<sdk::ParserWriteHostService>();
    if (!write) {
      return unexpected(std::move(write).error());
    }
    write_host_view_ = *write;

    // Object-write is optional — only registered by the host when the
    // parser is bound to a media topic alongside a scalar one.
    if (auto obj = services.get<sdk::ParserObjectWriteHostService>()) {
      object_write_host_view_ = *obj;
    }

    service_registry_ = services;
    return okStatus();
  }

  /// Bind a message schema. Default is no-op (for parsers that don't need schema).
  virtual Status bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    (void)type_name;
    (void)schema;
    return okStatus();
  }

  virtual std::string saveConfig() const {
    return "{}";
  }

  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Parse one raw message and write decoded fields via writeHost(). PURE VIRTUAL.
  virtual Status parse(Timestamp timestamp_ns, Span<const uint8_t> payload) = 0;

  /// Return a pointer to a static plugin-exposed extension for @p id, or
  /// nullptr if unknown. Default returns nullptr.
  virtual const void* pluginExtension(std::string_view id) {
    (void)id;
    return nullptr;
  }

  template <typename CreateFn>
  static const PJ_message_parser_vtable_t* vtableWithCreate(CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"id\"") != nullptr, "manifest must contain an \"id\" key");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    PJ_ASSERT(std::strstr(manifest, "\"encoding\"") != nullptr, "manifest must contain an \"encoding\" key");
    static const PJ_message_parser_vtable_t vt = {
        PJ_MESSAGE_PARSER_PROTOCOL_VERSION,
        sizeof(PJ_message_parser_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_bind,
        trampoline_bind_schema,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_parse,
        trampoline_get_plugin_extension,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] sdk::ServiceRegistry services() const {
    return service_registry_;
  }

  [[nodiscard]] const sdk::ParserWriteHostView& writeHost() const {
    return write_host_view_;
  }

  /// Optional — returns nullptr when the host did not register
  /// `pj.parser_object_write.v1` for this parser's binding (scalar-only
  /// case). Media-capable parsers check this and, if non-null, emit the
  /// payload via `objectWriteHost()->pushOwned(ts, bytes)` alongside the
  /// scalar fields written through `writeHost()`.
  [[nodiscard]] const sdk::ParserObjectWriteHostView* objectWriteHost() const {
    return object_write_host_view_.valid() ? &object_write_host_view_ : nullptr;
  }

  [[nodiscard]] bool writeHostBound() const {
    return write_host_view_.valid();
  }

 private:
  sdk::ServiceRegistry service_registry_{};
  sdk::ParserWriteHostView write_host_view_{PJ_parser_write_host_t{}};
  sdk::ParserObjectWriteHostView object_write_host_view_{};
  std::string config_buf_;

  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    sdk::fillError(out_error, code, domain, message);
  }

  static void trampoline_destroy(void* ctx) noexcept;
  static bool trampoline_bind(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept;
  static bool trampoline_bind_schema(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_error_t* out_error) noexcept;
  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_parse(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_error_t* out_error) noexcept;
  static const void* trampoline_get_plugin_extension(void* ctx, PJ_string_view_t id) noexcept;
};

}  // namespace PJ

#include "pj_base/sdk/detail/message_parser_trampolines.hpp"

#define PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest)                                                             \
  PJ_EXPORT_PLUGIN_ABI_VERSION(PJ_MESSAGE_PARSER_EXPORT)                                                          \
  extern "C" PJ_MESSAGE_PARSER_EXPORT const PJ_message_parser_vtable_t* PJ_get_message_parser_vtable() noexcept { \
    static const PJ_message_parser_vtable_t* vt = PJ::MessageParserPluginBase::vtableWithCreate(                  \
        []() noexcept -> void* {                                                                                  \
          try {                                                                                                   \
            return new ClassName();                                                                               \
          } catch (...) {                                                                                         \
            return nullptr;                                                                                       \
          }                                                                                                       \
        },                                                                                                        \
        manifest);                                                                                                \
    return vt;                                                                                                    \
  }
