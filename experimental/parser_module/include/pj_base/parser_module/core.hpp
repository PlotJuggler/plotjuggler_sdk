#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core.hpp
 * @brief Standalone value, view, and allocation vocabulary for parser modules.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 201703L, "pj parser modules require C++17 or newer");
#else
static_assert(__cplusplus >= 201703L, "pj parser modules require C++17 or newer");
#endif

// Keep the same source valid for native exception-enabled builds and WASI
// reactor builds compiled with -fno-exceptions. Public operations return
// Status in both modes; no exception syntax reaches the latter compiler.
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#define PJ_PARSER_MODULE_TRY try
#define PJ_PARSER_MODULE_CATCH_BAD_ALLOC catch (const std::bad_alloc&)
#define PJ_PARSER_MODULE_CATCH_ALL catch (...)
#else
#define PJ_PARSER_MODULE_TRY if (true)
#define PJ_PARSER_MODULE_CATCH_BAD_ALLOC else if (false)
#define PJ_PARSER_MODULE_CATCH_ALL else
#endif

namespace pj {

enum class StatusCode : uint8_t {
  kOk,
  kDecline,
  kError,
};

class Status {
 public:
  static constexpr size_t kMessageCapacity = 512;

  Status() = default;

  [[nodiscard]] static Status ok() noexcept {
    return {};
  }

  [[nodiscard]] static Status decline(std::string_view message) noexcept {
    return Status(StatusCode::kDecline, message);
  }

  [[nodiscard]] static Status error(std::string_view message) noexcept {
    return Status(StatusCode::kError, message);
  }

  [[nodiscard]] bool isOk() const noexcept {
    return code_ == StatusCode::kOk;
  }

  [[nodiscard]] bool isDecline() const noexcept {
    return code_ == StatusCode::kDecline;
  }

  [[nodiscard]] bool isError() const noexcept {
    return code_ == StatusCode::kError;
  }

  [[nodiscard]] StatusCode code() const noexcept {
    return code_;
  }

  [[nodiscard]] std::string_view message() const noexcept {
    return {message_.data(), message_size_};
  }

 private:
  Status(StatusCode code, std::string_view message) noexcept : code_(code) {
    message_size_ = message.size() < message_.size() - 1 ? message.size() : message_.size() - 1;
    if (message_size_ != 0) {
      std::memcpy(message_.data(), message.data(), message_size_);
    }
    message_[message_size_] = '\0';
  }

  StatusCode code_ = StatusCode::kOk;
  std::array<char, kMessageCapacity> message_{};
  size_t message_size_ = 0;
};

template <typename T>
class Expected {
 public:
  Expected(const T& value) : storage_(value) {}
  Expected(T&& value) : storage_(std::move(value)) {}
  Expected(Status error) : storage_(std::move(error)) {}

  [[nodiscard]] bool hasValue() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return hasValue();
  }

  [[nodiscard]] T& value() & {
    return std::get<T>(storage_);
  }

  [[nodiscard]] const T& value() const& {
    return std::get<T>(storage_);
  }

  [[nodiscard]] T&& value() && {
    return std::get<T>(std::move(storage_));
  }

  [[nodiscard]] T* operator->() {
    return &value();
  }

  [[nodiscard]] const T* operator->() const {
    return &value();
  }

  [[nodiscard]] T& operator*() & {
    return value();
  }

  [[nodiscard]] const T& operator*() const& {
    return value();
  }

  [[nodiscard]] Status status() const {
    return hasValue() ? Status::ok() : std::get<Status>(storage_);
  }

 private:
  std::variant<T, Status> storage_;
};

struct ByteView {
  const uint8_t* data = nullptr;
  size_t size = 0;

  ByteView() = default;
  ByteView(const uint8_t* input_data, size_t input_size) : data(input_data), size(input_size) {}

  template <size_t Size>
  ByteView(const uint8_t (&bytes)[Size]) : data(bytes), size(Size) {}

  [[nodiscard]] bool empty() const noexcept {
    return size == 0;
  }

  [[nodiscard]] Expected<ByteView> subview(size_t offset, size_t length) const {
    if (data == nullptr && size != 0) {
      return Status::error("byte-view storage is null");
    }
    if (offset > size || length > size - offset) {
      return Status::error("byte-view range is outside its storage");
    }
    return ByteView(data == nullptr ? nullptr : data + offset, length);
  }
};

using PayloadView = ByteView;

struct MutableByteView {
  uint8_t* data = nullptr;
  size_t size = 0;
};

struct InputSpanRef {
  uint64_t offset = 0;
  uint64_t length = 0;
};

class Blob {
 public:
  Blob() = default;
  ~Blob() {
    delete[] data_;
  }

  Blob(Blob&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)),
        capacity_(std::exchange(other.capacity_, 0)) {}

  Blob& operator=(Blob&& other) noexcept {
    if (this != &other) {
      delete[] data_;
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
      capacity_ = std::exchange(other.capacity_, 0);
    }
    return *this;
  }

  Blob(const Blob&) = delete;
  Blob& operator=(const Blob&) = delete;

  [[nodiscard]] const uint8_t* data() const noexcept {
    return data_;
  }

  [[nodiscard]] uint8_t* data() noexcept {
    return data_;
  }

  [[nodiscard]] size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] ByteView view() const noexcept {
    return {data_, size_};
  }

  [[nodiscard]] ByteView bytes() const noexcept {
    return view();
  }

  [[nodiscard]] Status reserve(size_t size) noexcept {
    if (size <= capacity_) {
      return Status::ok();
    }
    auto* grown = new (std::nothrow) uint8_t[size];
    if (grown == nullptr) {
      return Status::error("parser-module allocation failed");
    }
    if (size_ != 0) {
      std::memcpy(grown, data_, size_);
    }
    delete[] data_;
    data_ = grown;
    capacity_ = size;
    return Status::ok();
  }

  [[nodiscard]] Status resize(size_t size) noexcept {
    Status reserved = reserve(size);
    if (!reserved.isOk()) {
      return reserved;
    }
    if (size > size_) {
      std::memset(data_ + size_, 0, size - size_);
    }
    size_ = size;
    return Status::ok();
  }

  [[nodiscard]] Status append(ByteView bytes) noexcept {
    if (bytes.size != 0 && bytes.data == nullptr) {
      return Status::error("cannot append a null non-empty byte view");
    }
    if (bytes.size == 0) {
      return Status::ok();
    }
    if (bytes.size > std::numeric_limits<size_t>::max() - size_) {
      return Status::error("parser-module output size overflow");
    }
    const size_t required = size_ + bytes.size;
    if (required > capacity_) {
      size_t next_capacity = capacity_ == 0 ? size_t{64} : capacity_;
      while (next_capacity < required) {
        if (next_capacity > std::numeric_limits<size_t>::max() / 2) {
          next_capacity = required;
          break;
        }
        next_capacity *= 2;
      }
      Status reserved = reserve(next_capacity);
      if (!reserved.isOk()) {
        return reserved;
      }
    }
    std::memcpy(data_ + size_, bytes.data, bytes.size);
    size_ = required;
    return Status::ok();
  }

  [[nodiscard]] Status push(uint8_t byte) noexcept {
    if (size_ == capacity_) {
      const size_t next_capacity = capacity_ == 0 ? size_t{64} : capacity_ * 2;
      if (next_capacity < capacity_) {
        return Status::error("parser-module output size overflow");
      }
      Status reserved = reserve(next_capacity);
      if (!reserved.isOk()) {
        return reserved;
      }
    }
    data_[size_++] = byte;
    return Status::ok();
  }

 private:
  uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
};

[[nodiscard]] inline Expected<Blob> allocateBlob(size_t size) noexcept {
  Blob blob;
  Status status = blob.resize(size);
  if (!status.isOk()) {
    return status;
  }
  return blob;
}

/// Realloc-backed monotonic storage for bind-time plans and temporary output.
/// Every growth reports allocation failure; individual allocations are freed
/// together when the arena is destroyed or reset. A growth may relocate the
/// arena, so previously returned views are valid only until the next allocate.
class BumpArena {
 public:
  BumpArena() = default;
  ~BumpArena() {
    std::free(data_);
  }

  BumpArena(const BumpArena&) = delete;
  BumpArena& operator=(const BumpArena&) = delete;

  BumpArena(BumpArena&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        capacity_(std::exchange(other.capacity_, 0)),
        used_(std::exchange(other.used_, 0)) {}

  BumpArena& operator=(BumpArena&& other) noexcept {
    if (this != &other) {
      std::free(data_);
      data_ = std::exchange(other.data_, nullptr);
      capacity_ = std::exchange(other.capacity_, 0);
      used_ = std::exchange(other.used_, 0);
    }
    return *this;
  }

  [[nodiscard]] Expected<MutableByteView> allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      return Status::error("arena alignment must be a nonzero power of two");
    }
    if (alignment > alignof(std::max_align_t)) {
      return Status::error("arena alignment exceeds malloc alignment");
    }
    if (size == 0) {
      return MutableByteView{};
    }
    const size_t padding = (alignment - (used_ & (alignment - 1))) & (alignment - 1);
    if (padding > std::numeric_limits<size_t>::max() - used_ ||
        size > std::numeric_limits<size_t>::max() - used_ - padding) {
      return Status::error("arena allocation size overflow");
    }
    const size_t required = used_ + padding + size;
    if (required > capacity_) {
      size_t next_capacity = capacity_ == 0 ? size_t{256} : capacity_;
      while (next_capacity < required) {
        if (next_capacity > std::numeric_limits<size_t>::max() / 2) {
          next_capacity = required;
          break;
        }
        next_capacity *= 2;
      }
      void* grown = std::realloc(data_, next_capacity);
      if (grown == nullptr) {
        return Status::error("arena allocation failed");
      }
      data_ = static_cast<uint8_t*>(grown);
      capacity_ = next_capacity;
    }
    used_ += padding;
    MutableByteView result{data_ + used_, size};
    used_ += size;
    return result;
  }

  void reset() noexcept {
    used_ = 0;
  }

  [[nodiscard]] size_t used() const noexcept {
    return used_;
  }

 private:
  uint8_t* data_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
};

namespace detail {

inline void copyMessage(char* destination, size_t capacity, std::string_view message) noexcept {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  const size_t length = message.size() < capacity - 1 ? message.size() : capacity - 1;
  if (length != 0) {
    std::memcpy(destination, message.data(), length);
  }
  destination[length] = '\0';
}

}  // namespace detail
}  // namespace pj
