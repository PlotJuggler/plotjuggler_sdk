/**
 * @file message_parser_handle.hpp
 * @brief RAII wrapper around a single MessageParser plugin instance (v4).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_base/builtin_object_abi.h>
#include <pj_base/message_parser_protocol.h>

#include <cassert>
#include <memory>
#include <pj_base/builtin/builtin_object.hpp>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/data_source_host_views.hpp>
#include <pj_base/span.hpp>
#include <pj_base/types.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/// RAII handle owning a MessageParser plugin instance.
class MessageParserHandle {
 public:
  explicit MessageParserHandle(const PJ_message_parser_vtable_t* vt, std::shared_ptr<void> library_owner = {})
      : vt_(vt), library_owner_(std::move(library_owner)) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_MESSAGE_PARSER_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~MessageParserHandle() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
  }

  MessageParserHandle(MessageParserHandle&& other) noexcept
      : vt_(other.vt_), ctx_(other.ctx_), library_owner_(std::move(other.library_owner_)) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  MessageParserHandle& operator=(MessageParserHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
      std::swap(library_owner_, other.library_owner_);
    }
    return *this;
  }

  MessageParserHandle(const MessageParserHandle&) = delete;
  MessageParserHandle& operator=(const MessageParserHandle&) = delete;

  [[nodiscard]] bool valid() const {
    return vt_ != nullptr && ctx_ != nullptr;
  }

  [[nodiscard]] std::string manifest() const {
    return vt_->manifest_json != nullptr ? std::string(vt_->manifest_json) : std::string();
  }

  [[nodiscard]] Status bind(PJ_service_registry_t registry) {
    PJ_error_t err{};
    if (!vt_->bind(ctx_, registry, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    PJ_string_view_t tn{type_name.data(), type_name.size()};
    PJ_bytes_view_t sc{schema.data(), schema.size()};
    PJ_error_t err{};
    if (!vt_->bind_schema(ctx_, tn, sc, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status saveConfig(std::string& out_json) {
    PJ_string_view_t sv{};
    PJ_error_t err{};
    if (!vt_->save_config(ctx_, &sv, &err)) {
      return unexpected(errorToString(err));
    }
    out_json.assign(sv.data == nullptr ? "" : sv.data, sv.size);
    return okStatus();
  }

  [[nodiscard]] Status loadConfig(std::string_view config_json) {
    PJ_string_view_t sv{config_json.data(), config_json.size()};
    PJ_error_t err{};
    if (!vt_->load_config(ctx_, sv, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status parse(Timestamp timestamp_ns, Span<const uint8_t> payload) {
    PJ_bytes_view_t bytes{payload.data(), payload.size()};
    PJ_error_t err{};
    if (!vt_->parse(ctx_, timestamp_ns, bytes, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// A priori classification of the bound schema. Tail-slot gated; when
  /// the plugin doesn't expose classify_schema (older protocol header)
  /// returns kNone, matching the host contract documented in
  /// message_parser_protocol.h.
  [[nodiscard]] sdk::BuiltinObjectType classifySchema(std::string_view type_name, Span<const uint8_t> schema) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_message_parser_vtable_t, vt_, classify_schema)) {
      return sdk::BuiltinObjectType::kNone;
    }
    PJ_string_view_t tn{type_name.data(), type_name.size()};
    PJ_bytes_view_t sc{schema.data(), schema.size()};
    PJ_schema_classification_t out{};
    PJ_error_t err{};
    if (!vt_->classify_schema(ctx_, tn, sc, &out, &err)) {
      return sdk::BuiltinObjectType::kNone;
    }
    return static_cast<sdk::BuiltinObjectType>(out.object_type);
  }

  /// Query a plugin-exposed extension by reverse-DNS id. Tail-slot gated.
  [[nodiscard]] const void* getPluginExtension(std::string_view id) const {
    if (!PJ_HAS_TAIL_SLOT(PJ_message_parser_vtable_t, vt_, get_plugin_extension)) {
      return nullptr;
    }
    PJ_string_view_t sv{id.data(), id.size()};
    return vt_->get_plugin_extension(ctx_, sv);
  }

  [[nodiscard]] const PJ_message_parser_vtable_t* vtable() const {
    return vt_;
  }

  [[nodiscard]] void* context() const {
    return ctx_;
  }

 private:
  const PJ_message_parser_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;
  std::shared_ptr<void> library_owner_;
};

}  // namespace PJ
