#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <utility>

#include "pj_base/plugin_data_api.h"
#include "pj_base/span.hpp"

namespace PJ::sdk {

/// RAII holder for an opaque `PJ_object_bytes_handle_t` returned by the
/// toolbox object-read host. Move-only; destructor releases the handle via
/// the vtable that allocated it. Matches the `shared_ptr<const vector<
/// uint8_t>>` semantics on the host side: the handle keeps bytes alive
/// independent of the ObjectStore's internal state.
///
/// Typical use:
///   auto bytes = read_host.readLatestAt(topic, ts);
///   if (bytes && !bytes->empty()) {
///     decode(bytes->view());   // Span<const uint8_t>
///   }
///   // bytes goes out of scope → release_bytes runs exactly once.
class ObjectBytes {
 public:
  ObjectBytes() = default;
  ObjectBytes(PJ_object_bytes_handle_t handle, const PJ_object_read_host_vtable_t* vtable) noexcept
      : handle_(handle), vtable_(vtable) {}

  ~ObjectBytes() {
    reset();
  }

  ObjectBytes(const ObjectBytes&) = delete;
  ObjectBytes& operator=(const ObjectBytes&) = delete;

  ObjectBytes(ObjectBytes&& other) noexcept : handle_(other.handle_), vtable_(other.vtable_) {
    other.handle_ = nullptr;
    other.vtable_ = nullptr;
  }

  ObjectBytes& operator=(ObjectBytes&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      vtable_ = other.vtable_;
      other.handle_ = nullptr;
      other.vtable_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool empty() const noexcept {
    return handle_ == nullptr;
  }

  explicit operator bool() const noexcept {
    return handle_ != nullptr;
  }

  /// View into the bytes. Valid until this holder is moved-from or destroyed.
  [[nodiscard]] Span<const uint8_t> view() const noexcept {
    if (handle_ == nullptr || vtable_ == nullptr || vtable_->get_bytes == nullptr) {
      return {};
    }
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    vtable_->get_bytes(handle_, &data, &size);
    return Span<const uint8_t>(data, size);
  }

  [[nodiscard]] PJ_object_bytes_handle_t raw() const noexcept {
    return handle_;
  }

 private:
  void reset() noexcept {
    if (handle_ != nullptr && vtable_ != nullptr && vtable_->release_bytes != nullptr) {
      vtable_->release_bytes(handle_);
    }
    handle_ = nullptr;
    vtable_ = nullptr;
  }

  PJ_object_bytes_handle_t handle_ = nullptr;
  const PJ_object_read_host_vtable_t* vtable_ = nullptr;
};

}  // namespace PJ::sdk
