#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "pj_base/assert.hpp"

namespace PJ {

/// Wrapper used to explicitly construct error states in Expected.
template <typename E>
class Unexpected {
 public:
  /// Construct from an error payload.
  constexpr explicit Unexpected(E error) : error_(std::move(error)) {}

  [[nodiscard]] constexpr const E& value() const& noexcept {
    return error_;
  }
  [[nodiscard]] constexpr E& value() & noexcept {
    return error_;
  }
  [[nodiscard]] constexpr E&& value() && noexcept {
    return std::move(error_);
  }

 private:
  E error_;
};

/// Helper to build an `Unexpected<E>` with type deduction.
template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> unexpected(E&& error) {
  return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

/// Convenience overload: accept a string literal without requiring std::string("...").
[[nodiscard]] inline Unexpected<std::string> unexpected(const char* error) {
  return Unexpected<std::string>(std::string(error));
}

/// Minimal value-or-error container.
template <typename T, typename E = std::string>
class Expected {
 public:
  /// Construct a success value.
  constexpr Expected(const T& value) : storage_(value) {}
  /// Construct a success value.
  constexpr Expected(T&& value) : storage_(std::move(value)) {}

  /// Construct an error state.
  constexpr Expected(const Unexpected<E>& error) : storage_(error.value()) {}
  /// Construct an error state.
  constexpr Expected(Unexpected<E>&& error) : storage_(std::move(error).value()) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  /// Dereference operator — access success payload.
  [[nodiscard]] constexpr T& operator*() & {
    return value();
  }
  [[nodiscard]] constexpr const T& operator*() const& {
    return value();
  }
  [[nodiscard]] constexpr T&& operator*() && {
    return std::move(*this).value();
  }

  /// Arrow operator — access success payload members.
  [[nodiscard]] constexpr T* operator->() {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return &std::get<T>(storage_);
  }
  [[nodiscard]] constexpr const T* operator->() const {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return &std::get<T>(storage_);
  }

  /// Access success payload. Asserts if this contains an error.
  [[nodiscard]] constexpr T& value() & {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::get<T>(storage_);
  }

  /// Access success payload. Asserts if this contains an error.
  [[nodiscard]] constexpr const T& value() const& {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::get<T>(storage_);
  }

  /// Access success payload. Asserts if this contains an error.
  [[nodiscard]] constexpr T&& value() && {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::move(std::get<T>(storage_));
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr E& error() & {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::get<E>(storage_);
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr const E& error() const& {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::get<E>(storage_);
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr E&& error() && {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::move(std::get<E>(storage_));
  }

 private:
  std::variant<T, E> storage_;
};

/// Specialization for void value type (replaces absl::Status).
template <typename E>
class Expected<void, E> {
 public:
  /// Construct a success (no-error) state.
  constexpr Expected() noexcept : error_(), has_error_(false) {}

  /// Construct an error state.
  constexpr Expected(const Unexpected<E>& error) : error_(error.value()), has_error_(true) {}
  /// Construct an error state.
  constexpr Expected(Unexpected<E>&& error) : error_(std::move(error).value()), has_error_(true) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return !has_error_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr E& error() & {
    PJ_ASSERT(has_error_, "Expected does not contain an error");
    return error_;
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr const E& error() const& {
    PJ_ASSERT(has_error_, "Expected does not contain an error");
    return error_;
  }

  /// Access error payload. Asserts if this contains a value.
  [[nodiscard]] constexpr E&& error() && {
    PJ_ASSERT(has_error_, "Expected does not contain an error");
    return std::move(error_);
  }

 private:
  E error_;
  bool has_error_;
};

/// Alias for Expected<void> — a lightweight status type.
using Status = Expected<void, std::string>;

/// Construct a success Status.
[[nodiscard]] inline Status okStatus() {
  return Status{};
}

}  // namespace PJ
