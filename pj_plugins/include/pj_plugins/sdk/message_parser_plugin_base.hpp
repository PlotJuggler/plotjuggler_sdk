/**
 * @file message_parser_plugin_base.hpp
 * @brief C++ SDK for implementing MessageParser plugins (protocol v4).
 *
 * Plugin authors subclass MessageParserPluginBase, override `parse()`, and
 * export with PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest).
 *
 * The default `bind()` implementation acquires the parser write host from
 * the service registry plus optional object-write and parser-runtime services.
 * Override to additionally acquire optional services.
 * All trampolines are noexcept at the ABI boundary.
 *
 * The "maximum array size + clamp/skip" parser option is a cross-plugin config
 * contract: read and write it with PJ::sdk::arrayLimitFromJson /
 * arrayLimitToJson from pj_plugins/sdk/parser_array_policy.hpp rather than
 * inventing per-parser keys.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/parser_functional_protocol.h"
#include "pj_base/parser_route_claims_protocol.h"
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

namespace testing {
struct MessageParserPluginBaseLayoutSentinel;
}

}  // namespace sdk

#if defined(__GNUC__) || defined(__clang__)
#define PJ_MESSAGE_PARSER_DSO_LOCAL __attribute__((visibility("hidden")))
#else
#define PJ_MESSAGE_PARSER_DSO_LOCAL
#endif

/**
 * Base class for MessageParser plugins (protocol v4).
 *
 * TRANSITIONAL ABI LAYOUT CONTRACT: SDK 0.21 hosts use the
 * `pj.parser_functional.v1` C extension and never cast a newly-built plugin's
 * opaque context to this class. Hosts may retain a deprecated direct-C++
 * bridge only for pre-0.21 plugins that do not expose the extension. Therefore
 * the existing member prefix remains frozen within PJ_ABI_VERSION 5: members
 * must not move, and additions are tail-only. SDK 1.0 may remove that bridge
 * and this layout constraint together.
 */
class MessageParserPluginBase {
 public:
  virtual ~MessageParserPluginBase() = default;

  /// Acquire host-provided services.
  ///
  /// Default implementation pulls:
  ///   - "pj.parser_write.v1"        → ParserWriteHost       (mandatory)
  ///   - "pj.parser_object_write.v1" → ObjectWriteHost       (optional)
  ///   - "pj.parser_runtime.v1"      → ParserRuntimeHost     (optional)
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

    // Runtime diagnostics are optional. Older/minimal hosts omit the service;
    // ParserRuntimeHostView then remains a safe no-op view.
    parser_runtime_host_view_ = {};
    if (auto runtime = services.get<sdk::ParserRuntimeHostService>()) {
      parser_runtime_host_view_ = *runtime;
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
  /// New host code invokes the pure-functional pair through
  /// MessageParserHandle and `pj.parser_functional.v1`, never by casting the
  /// opaque plugin context. The pair enables lazy materialization because the
  /// caller (DataSource / app) receives the result rather than a side effect.
  /// Once every plugin migrates to SchemaHandler, parse() can be deprecated.
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
    return writeHost().appendRecord(ts, Span<const sdk::NamedFieldValue>(record->fields.data(), record->fields.size()));
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
  // table. The functional C trampolines invoke these methods inside the plugin
  // DSO, then synchronously translate results to C ABI sinks.

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
  /// Classification is side-effect free: implementations must not report
  /// parser runtime diagnostics from this path.
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
  virtual Expected<sdk::ScalarRecord> parseScalars(Timestamp timestamp_ns, Span<const uint8_t> payload) final {
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
  /// nullptr if unknown. Default returns nullptr. The SDK reserves
  /// `pj.parser_route_claims.v1` plus `pj.parser_functional.v1` and v2. Route
  /// claims are always advertised; both functional revisions are advertised
  /// automatically after at least one SchemaHandler has been registered.
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

  /// Optional non-fatal diagnostics channel. The returned view is always safe
  /// to call; when `pj.parser_runtime.v1` is absent, reports are discarded.
  /// Use diagnostics for recoverable/aggregated conditions inside parse(),
  /// never from classifySchema(); fatal parse failure still uses Status.
  ///
  /// @since 0.21.0
  [[nodiscard]] const sdk::ParserRuntimeHostView& parserRuntimeHost() const noexcept {
    return parser_runtime_host_view_;
  }

  [[nodiscard]] bool writeHostBound() const {
    return write_host_view_.valid();
  }

  /// Whether the host supplied `pj.parser_runtime.v1` for this binding.
  ///
  /// @since 0.21.0
  [[nodiscard]] bool parserRuntimeHostBound() const noexcept {
    return parser_runtime_host_view_.valid();
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
  friend struct sdk::testing::MessageParserPluginBaseLayoutSentinel;

  sdk::ServiceRegistry service_registry_{};
  sdk::ParserWriteHostView write_host_view_{PJ_parser_write_host_t{}};
  sdk::ParserObjectWriteHostView object_write_host_view_{};
  std::string config_buf_;

  // Schema handler table populated by the plugin via registerSchemaHandler().
  std::unordered_map<std::string, sdk::SchemaHandler> handlers_;

  // ABI-APPENDED in 0.21.0. New data members must be appended after this one;
  // never insert into the frozen prefix above.
  sdk::ParserRuntimeHostView parser_runtime_host_view_{};

  static void storeError(PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message) {
    sdk::fillError(out_error, code, domain, message);
  }

  static void storeErrorKind(
      PJ_error_t* out_error, int32_t code, std::string_view domain, std::string_view message, std::string_view kind) {
    sdk::fillError(out_error, code, domain, message);
    sdk::setExtended(out_error, kind, nullptr);
  }

  static void trampoline_destroy(void* ctx) noexcept;
  static bool trampoline_bind(void* ctx, PJ_service_registry_t registry, PJ_error_t* out_error) noexcept;
  static bool trampoline_bind_schema(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_error_t* out_error) noexcept;
  static bool trampoline_save_config(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_load_config(void* ctx, PJ_string_view_t config_json, PJ_error_t* out_error) noexcept;
  static bool trampoline_parse(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, PJ_error_t* out_error) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static const void* trampoline_get_plugin_extension(
      void* ctx, PJ_string_view_t id) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static bool trampoline_parse_scalars_functional(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, const PJ_parser_scalar_sink_v1_t* sink,
      PJ_error_t* out_error) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static bool trampoline_parse_scalars_functional_v2(
      void* ctx, int64_t timestamp_ns, PJ_bytes_view_t payload, const PJ_parser_scalar_sink_v1_t* sink,
      PJ_error_t* out_error) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static bool trampoline_parse_object_functional(
      void* ctx, int64_t timestamp_ns, PJ_payload_t payload, const PJ_parser_object_sink_v1_t* sink,
      PJ_error_t* out_error) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static bool trampoline_parse_object_functional_v2(
      void* ctx, int64_t timestamp_ns, PJ_payload_t payload, const PJ_parser_object_sink_v2_t* sink,
      PJ_error_t* out_error) noexcept;
  PJ_MESSAGE_PARSER_DSO_LOCAL static bool trampoline_classify_routes(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_route_classification_v1_t* out,
      PJ_error_t* out_error) noexcept;
  static bool trampoline_classify_schema(
      void* ctx, PJ_string_view_t type_name, PJ_bytes_view_t schema, PJ_schema_classification_t* out_classification,
      PJ_error_t* out_error) noexcept;
};

}  // namespace PJ

#undef PJ_MESSAGE_PARSER_DSO_LOCAL

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

// Variant for namespaced plugin classes. SymbolName must be an unqualified
// identifier and is used only to form the unique static getter name.
#define PJ_MESSAGE_PARSER_PLUGIN_NAMED(ClassName, SymbolName, manifest) PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest)

// --- Static-link variant (WASM / no dlopen) ---  see data_source_plugin_base.hpp
#ifdef PJ_STATIC_PLUGINS
#undef PJ_MESSAGE_PARSER_PLUGIN
#undef PJ_MESSAGE_PARSER_PLUGIN_NAMED
#define PJ_MESSAGE_PARSER_PLUGIN_NAMED(ClassName, SymbolName, manifest)                           \
  const PJ_message_parser_vtable_t* pj_static_get_message_parser_vtable_##SymbolName() noexcept { \
    static const PJ_message_parser_vtable_t* vt = PJ::MessageParserPluginBase::vtableWithCreate(  \
        []() noexcept -> void* {                                                                  \
          try {                                                                                   \
            return new ClassName();                                                               \
          } catch (...) {                                                                         \
            return nullptr;                                                                       \
          }                                                                                       \
        },                                                                                        \
        manifest);                                                                                \
    return vt;                                                                                    \
  }
#define PJ_MESSAGE_PARSER_PLUGIN(ClassName, manifest) PJ_MESSAGE_PARSER_PLUGIN_NAMED(ClassName, ClassName, manifest)
#endif
