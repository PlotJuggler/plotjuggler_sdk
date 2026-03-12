/**
 * @file data_source_handle.hpp
 * @brief RAII wrapper around a single DataSource plugin instance.
 *
 * Obtained from DataSourceLibrary::createHandle(). Owns the plugin context
 * and destroys it on scope exit. Move-only; not copyable.
 *
 * Typical usage:
 * @code
 *   auto handle = library.createHandle();
 *   handle.bindWriteHost(write_host);
 *   handle.bindRuntimeHost(runtime_host);
 *   handle.loadConfig(json);
 *   handle.start();
 *   while (handle.currentState() == PJ_DATA_SOURCE_STATE_RUNNING) {
 *     handle.poll();
 *   }
 *   handle.stop();
 * @endcode
 */
#pragma once

#include <pj_base/data_source_protocol.h>

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace PJ {

/**
 * RAII handle owning a DataSource plugin instance.
 *
 * Each method delegates to the corresponding vtable function pointer.
 * The destructor calls vt_->destroy(ctx_).
 */
class DataSourceHandle {
 public:
  explicit DataSourceHandle(const PJ_data_source_vtable_t* vt) : vt_(vt) {
    if (vt_ != nullptr) {
      assert(vt_->protocol_version == PJ_DATA_SOURCE_PROTOCOL_VERSION);
      ctx_ = vt_->create();
    }
  }

  ~DataSourceHandle() {
    if (vt_ != nullptr && ctx_ != nullptr) {
      vt_->destroy(ctx_);
    }
  }

  DataSourceHandle(DataSourceHandle&& other) noexcept : vt_(other.vt_), ctx_(other.ctx_) {
    other.vt_ = nullptr;
    other.ctx_ = nullptr;
  }

  DataSourceHandle& operator=(DataSourceHandle&& other) noexcept {
    if (this != &other) {
      std::swap(vt_, other.vt_);
      std::swap(ctx_, other.ctx_);
    }
    return *this;
  }

  DataSourceHandle(const DataSourceHandle&) = delete;
  DataSourceHandle& operator=(const DataSourceHandle&) = delete;

  [[nodiscard]] bool valid() const { return vt_ != nullptr && ctx_ != nullptr; }

  [[nodiscard]] std::string manifest() const { return safeString(vt_->manifest_json); }

  [[nodiscard]] uint64_t capabilities() const { return vt_->capabilities(ctx_); }

  [[nodiscard]] bool bindWriteHost(PJ_source_write_host_t write_host) {
    return vt_->bind_write_host(ctx_, write_host);
  }

  [[nodiscard]] bool bindRuntimeHost(PJ_data_source_runtime_host_t runtime_host) {
    return vt_->bind_runtime_host(ctx_, runtime_host);
  }

  [[nodiscard]] std::string saveConfig() const { return safeString(vt_->save_config(ctx_)); }

  [[nodiscard]] bool loadConfig(std::string_view config_json) {
    return vt_->load_config(ctx_, std::string(config_json).c_str());
  }

  [[nodiscard]] bool start() { return vt_->start(ctx_); }

  void stop() { vt_->stop(ctx_); }

  [[nodiscard]] bool pause() { return vt_->pause(ctx_); }

  [[nodiscard]] bool resume() { return vt_->resume(ctx_); }

  [[nodiscard]] bool poll() { return vt_->poll(ctx_); }

  [[nodiscard]] PJ_data_source_state_t currentState() const { return vt_->current_state(ctx_); }

  [[nodiscard]] std::string lastError() const { return safeString(vt_->get_last_error(ctx_)); }

  [[nodiscard]] void* dialogContext() const {
    return vt_->get_dialog_context ? vt_->get_dialog_context(ctx_) : nullptr;
  }

  [[nodiscard]] const PJ_data_source_vtable_t* vtable() const { return vt_; }

  [[nodiscard]] void* context() const { return ctx_; }

 private:
  const PJ_data_source_vtable_t* vt_ = nullptr;
  void* ctx_ = nullptr;

  static std::string safeString(const char* str) {
    return str != nullptr ? std::string(str) : std::string();
  }
};

}  // namespace PJ
