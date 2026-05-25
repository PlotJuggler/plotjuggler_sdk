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
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/plugin_abi_export.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/sdk/service_traits.hpp"

namespace PJ {
namespace sdk {

/// Per-schema handler bundle: classification + the two parse routes for one
/// schema type. Plugins build a table of these in their constructor; the
/// MessageParserPluginBase base class then implements classifySchema /
/// parseScalars / parseObject as final lookups into the table.
///
/// Either parse_scalars or parse_object may be null (or both), reflecting
/// schemas that produce only scalars, only objects, or that the plugin
/// recognizes but routes through the legacy parse() path.
struct SchemaHandler {
  BuiltinObjectType object_type = BuiltinObjectType::kNone;

  /// Scalar route: returns one row of decoded fields with an optional
  /// parser-controlled timestamp. When ScalarRecord::ts is nullopt the
  /// host uses the message's own timestamp. Set it to extract a timestamp
  /// embedded inside the payload (e.g. a ROS Header stamp or a JSON
  /// "timestamp" field).
  std::function<Expected<ScalarRecord>(Timestamp, Span<const uint8_t>)> parse_scalars;

  /// Canonical-object route: returns an ObjectRecord with an optional
  /// parser-controlled timestamp. When ObjectRecord::ts is nullopt the host
  /// uses the message's own timestamp. Set it to use the sensor time embedded
  /// in the payload (e.g. ROS Header.stamp) so objects align with scalars on
  /// the time axis. The parser propagates `payload.anchor` into the returned
  /// object so its bytes outlive this call.
  std::function<Expected<ObjectRecord>(Timestamp, PayloadView)> parse_object;
};

}  // namespace sdk

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

  /// Bind a message schema. The base implementation records the type name
  /// verbatim so subsequent parseScalars / parseObject calls can dispatch
  /// against the registered handler table without needing it as a parameter.
  ///
  /// The base does NO domain-specific normalization on the type name —
  /// the SDK has no idea whether a name like \"pkg/msg/Type\" is valid or
  /// equivalent to \"pkg/Type\" in some plugin's domain (that\'s a ROS-2
  /// convention, not a general one). Plugins that have their own naming
  /// convention should apply it here, in their override, before delegating
  /// to MessageParserPluginBase::bindSchema with the canonical form. They
  /// must also use that same canonical form when calling
  /// registerSchemaHandler.
  ///
  /// Subclasses that override this MUST call MessageParserPluginBase::bindSchema()
  /// first (or set bound_type_name_ themselves) before any plugin-specific
  /// schema setup, otherwise the table-based dispatch will fail to find the
  /// schema's handler.
  virtual Status bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    (void)schema;
    bound_type_name_.assign(type_name);
    return okStatus();
  }

  virtual std::string saveConfig() const {
    return "{}";
  }

  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Parse one raw message and write decoded fields via writeHost().
  ///
  /// The default implementation dispatches through the SchemaHandler table:
  /// it invokes parseScalars() (which looks up the registered handler for
  /// bound_type_name_) and shovels the returned vector to
  /// writeHost().appendRecord(). Plugins that register all their schemas
  /// via registerSchemaHandler() therefore inherit a working parse() for
  /// free — no override needed.
  ///
  /// Subclasses MAY override to (a) add a fallback for type names not in
  /// the registered table (e.g. a ROS-style generic flattener that handles
  /// any message whose schema definition is known to the plugin), or
  /// (b) retain a fully imperative implementation during migration to the
  /// table-based dispatch. Plugins that have already migrated do not need
  /// to override.
  ///
  /// This entry point exists for compatibility with the legacy v4 ingest
  /// path (host calls parser.parse() directly to push fields to writeHost).
  /// New host code should prefer pushing through parseScalars() / parseObject()
  /// — the pure-functional pair enables lazy materialization, because the
  /// caller (DataSource / app) needs the result returned, not pushed. Once
  /// every host migrates to that path, parse() will be deprecated.
  virtual Status parse(Timestamp timestamp_ns, Span<const uint8_t> payload) {
    if (!writeHostBound()) {
      return unexpected(std::string("write host not bound"));
    }
    auto record = parseScalars(timestamp_ns, payload);
    if (!record) {
      return unexpected(std::move(record).error());
    }
    if (record->fields.empty()) {
      return okStatus();
    }
    // Use the parser-provided timestamp if set, otherwise fall back to the
    // host-provided one (the message receive time).
    const Timestamp ts = record->ts.value_or(timestamp_ns);
    return writeHost().appendRecord(
        ts, Span<const sdk::NamedFieldValue>(record->fields.data(), record->fields.size()));
  }

  // ---------------------------------------------------------------------------
  // Pure-functional API
  // ---------------------------------------------------------------------------
  //
  // Design principle: the parser does NOT decide push policy (eager vs lazy)
  // and does NOT decide where the result goes (Datastore, ObjectStore, none).
  // Both decisions belong to the caller (DataSource / app). The parser is
  // strictly a translator: bytes in, typed values out. Always eager when
  // invoked — there is no internal deferral. Lazyness is modeled by callers
  // wrapping these methods inside a lambda that fires on pull.
  //
  // Plugins extend the parser by populating a per-schema handler table in
  // the constructor (registerSchemaHandler). The base class implements
  // classifySchema / parseScalars / parseObject as `final` lookups into that
  // table, invoked by the host directly on a MessageParserPluginBase* pointer
  // (no vtable indirection, no cross-ABI copy).

  /// Register a handler for one schema type name. Typically called once per
  /// supported schema in the plugin's constructor.
  ///
  /// The type_name is stored verbatim — the base class does no domain-
  /// specific normalization. Plugins that have their own naming convention
  /// (e.g. ROS-2 \"pkg/msg/Type\" vs ROS-1 \"pkg/Type\") must register and
  /// look up using a single canonical form they pick. The base class will
  /// look up handlers using the bound_type_name_ value the plugin set in
  /// bindSchema, so the two must agree on the convention.
  ///
  /// Either `handler.parse_scalars` or `handler.parse_object` may be null —
  /// the base class returns the appropriate unexpected when an absent route
  /// is invoked for that schema.
  void registerSchemaHandler(std::string_view type_name, sdk::SchemaHandler handler) {
    handlers_.insert_or_assign(std::string(type_name), std::move(handler));
  }

  /// Strict lookup — returns nullptr if no handler is registered for this
  /// exact type name. Caller must not retain the pointer past the next
  /// mutation of the handler table. There is no fallback / default
  /// mechanism in the SDK: a plugin that wants behaviour for unknown
  /// types is expected to register a handler under the bound name itself
  /// (typically inside its bindSchema override).
  [[nodiscard]] const sdk::SchemaHandler* findSchemaHandler(std::string_view type_name) const {
    auto it = handlers_.find(std::string(type_name));
    if (it == handlers_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  /// Lookup against the registered handler table. Marked `final`: plugins
  /// populate the table via registerSchemaHandler() rather than overriding.
  /// The C ABI trampolines call this on MessageParserPluginBase*; a derived
  /// override would never be invoked, so the compiler rejects it explicitly.
  /// Returns kNone when no handler is registered for this type name.
  ///
  /// `type_name` is passed as a parameter (rather than using bound_type_name_)
  /// because classification may be queried for any schema this parser handles,
  /// including before bindSchema has fixed the instance to one.
  virtual sdk::SchemaClassification classifySchema(std::string_view type_name, Span<const uint8_t> schema) const final {
    (void)schema;
    if (const auto* h = findSchemaHandler(type_name)) {
      return {h->object_type};
    }
    return {};
  }

  /// Invoke the registered scalar handler for the currently-bound schema.
  /// Returns unexpected if no handler is registered, or if the registered
  /// handler did not provide a parse_scalars callable. Marked `final` — see
  /// classifySchema above for the rationale.
  virtual Expected<sdk::ScalarRecord> parseScalars(
      Timestamp timestamp_ns, Span<const uint8_t> payload) final {
    const auto* h = findSchemaHandler(bound_type_name_);
    if (h == nullptr) {
      return unexpected(std::string("parser does not register schema: ") + bound_type_name_);
    }
    if (!h->parse_scalars) {
      return unexpected(std::string("registered handler has no parse_scalars: ") + bound_type_name_);
    }
    return h->parse_scalars(timestamp_ns, payload);
  }

  /// Invoke the registered object handler for the currently-bound schema.
  /// Returns unexpected if no handler is registered, or if the registered
  /// handler did not provide a parse_object callable (i.e. this schema
  /// produces only scalars). Marked `final` — see classifySchema above.
  ///
  /// `payload.anchor` may be empty; in that case the parser is expected to
  /// materialize anything it wants to outlive this call. In-process callers
  /// that already own the payload buffer should pass a non-empty anchor so
  /// the parser can return a zero-copy BuiltinObject.
  virtual Expected<sdk::ObjectRecord> parseObject(Timestamp timestamp_ns, sdk::PayloadView payload) final {
    const auto* h = findSchemaHandler(bound_type_name_);
    if (h == nullptr) {
      return unexpected(std::string("parser does not register schema: ") + bound_type_name_);
    }
    if (!h->parse_object) {
      return unexpected(std::string("registered handler has no parse_object: ") + bound_type_name_);
    }
    return h->parse_object(timestamp_ns, payload);
  }

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
        trampoline_classify_schema,
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

 protected:
  /// Last type name received by bindSchema, stored verbatim. Used by the
  /// table-based dispatch in classifySchema / parseScalars / parseObject:
  /// the base looks up the handler for this string in the registered table.
  ///
  /// Subclasses that override bindSchema must either call the base class
  /// implementation or set this member themselves. If the plugin has its
  /// own naming convention, the canonical form it picks must be the same
  /// here and at registerSchemaHandler — the base does not normalize.
  std::string bound_type_name_;

 private:
  sdk::ServiceRegistry service_registry_{};
  sdk::ParserWriteHostView write_host_view_{PJ_parser_write_host_t{}};
  sdk::ParserObjectWriteHostView object_write_host_view_{};
  std::string config_buf_;

  // Schema handler table populated by the plugin via registerSchemaHandler().
  std::unordered_map<std::string, sdk::SchemaHandler> handlers_;

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
  static bool trampoline_classify_schema(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_schema_classification_t* out_classification,
      PJ_error_t* out_error) noexcept;
};

}  // namespace PJ

#include "pj_plugins/sdk/detail/message_parser_trampolines.hpp"

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
