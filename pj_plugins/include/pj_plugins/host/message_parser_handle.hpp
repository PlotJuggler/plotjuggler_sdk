/**
 * @file message_parser_handle.hpp
 * @brief RAII wrapper around a single MessageParser plugin instance.
 *
 * Obtained from MessageParserLibrary::createHandle(). Owns the plugin context
 * and destroys it on scope exit. Move-only; not copyable.
 */
#pragma once

#include <pj_base/message_parser_protocol.h>
#include <pj_base/span.hpp>
#include <pj_base/types.hpp>

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/**
 * RAII handle owning a MessageParser plugin instance.
 *
 * Each method delegates to the corresponding vtable function pointer.
 * The destructor calls vt_->destroy(ctx_).
 */
class MessageParserHandle {
 public:
  explicit MessageParserHandle(const PJ_message_parser_vtable_t* vt) : vt_(vt) {
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

  MessageParserHandle(MessageParserHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  MessageParserHandle& operator=(MessageParserHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
    }
    return *this;
  }

  MessageParserHandle(const MessageParserHandle&) = delete;
  MessageParserHandle& operator=(const MessageParserHandle&) = delete;

  [[nodiscard]] bool valid() const { return vt_ != nullptr && ctx_ != nullptr; }

  [[nodiscard]] std::string manifest() const { return safeString(vt_->manifest_json); }

  [[nodiscard]] bool bindWriteHost(PJ_parser_write_host_t write_host) {
    return vt_->bind_write_host(ctx_, write_host);
  }

  [[nodiscard]] bool bindSchema(std::string_view type_name, Span<const uint8_t> schema) {
    PJ_string_view_t tn = {type_name.data(), type_name.size()};
    PJ_bytes_view_t sc = {schema.data(), schema.size()};
    return vt_->bind_schema(ctx_, tn, sc);
  }

  [[nodiscard]] std::string saveConfig() const { return safeString(vt_->save_config(ctx_)); }

  [[nodiscard]] bool loadConfig(std::string_view config_json) {
    return vt_->load_config(ctx_, std::string(config_json).c_str());
  }

  [[nodiscard]] bool parse(Timestamp timestamp_ns, Span<const uint8_t> payload) {
    PJ_bytes_view_t bytes = {payload.data(), payload.size()};
    return vt_->parse(ctx_, timestamp_ns, bytes);
  }

  [[nodiscard]] std::string lastError() const { return safeString(vt_->get_last_error(ctx_)); }

  [[nodiscard]] const PJ_message_parser_vtable_t* vtable() const { return vt_; }

  [[nodiscard]] void* context() const { return ctx_; }

 private:
  const PJ_message_parser_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;

  static std::string safeString(const char* str) {
    return str != nullptr ? std::string(str) : std::string();
  }
};

}  // namespace PJ
